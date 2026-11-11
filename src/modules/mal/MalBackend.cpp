#include "MalBackend.h"

#include "MalClient.h"
#include "MalMappingsStore.h"
#include "MalOAuthServer.h"
#include "MalScrobble.h"

#include "modules/emby/EmbyBackend.h"
#include "modules/jellyfin/JellyfinBackend.h"
#include "modules/plex/PlexBackend.h"
#include "third_party/qrcodegen.hpp"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

namespace {
constexpr auto kModuleId = "com.240mp.mal";
constexpr int kDefaultPort = 8740;
constexpr int kLoginWindowMs = 10 * 60 * 1000;
constexpr int kRefreshSkewSec = 300;
constexpr int kRetryMs = 30000;
constexpr int kMaxFailures = 3;

QString randomToken(int length)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    QString out;
    out.reserve(length);
    auto *rng = QRandomGenerator::system();
    for (int i = 0; i < length; ++i)
        out.append(QLatin1Char(alphabet[rng->bounded(int(sizeof(alphabet) - 1))]));
    return out;
}

QString detectLanIPv4()
{
    QString other;
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : ifaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || !(iface.flags() & QNetworkInterface::IsRunning))
            continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack)
            continue;
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            const QHostAddress ip = entry.ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            const quint32 v = ip.toIPv4Address();
            if ((v & 0xffff0000u) == 0xa9fe0000u)
                continue; // 169.254/16
            const bool rfc1918 = (v & 0xff000000u) == 0x0a000000u
                || (v & 0xfff00000u) == 0xac100000u
                || (v & 0xffff0000u) == 0xc0a80000u;
            if (rfc1918)
                return ip.toString();
            if (other.isEmpty())
                other = ip.toString();
        }
    }
    return other;
}

bool jsonEnabled(const QJsonValue &value)
{
    if (value.isBool())
        return value.toBool();
    if (value.isString())
        return value.toString().compare(QLatin1String("ON"), Qt::CaseInsensitive) == 0;
    return false;
}
} // namespace

MalBackend::MalBackend(const QString &dataRoot, PlexBackend *plex, EmbyBackend *emby,
                       JellyfinBackend *jellyfin, QObject *parent)
    : QObject(parent)
    , m_dataRoot(dataRoot)
    , m_plex(plex)
    , m_emby(emby)
    , m_jellyfin(jellyfin)
    , m_mappings(new mal::MappingsStore(dataRoot, this))
    , m_oauth(new mal::OAuthServer(this))
    , m_client(new mal::MalClient(this))
    , m_loginTimer(new QTimer(this))
{
    mal::setProgressSink(this);
    m_loginTimer->setSingleShot(true);
    connect(m_loginTimer, &QTimer::timeout, this, [this]() {
        if (m_phase == QLatin1String("waiting"))
            setPhase(QStringLiteral("error"), QStringLiteral("LOGIN TIMED OUT — TRY AGAIN"));
        m_oauth->stop();
    });
    connect(m_oauth, &mal::OAuthServer::authorizationCode, this,
            [this](const QString &code, const QString &state, QTcpSocket *socket) {
                if (state != m_state) {
                    qWarning("[MAL] oauth state did not match");
                    m_oauth->finish(socket, false, QStringLiteral("This login session is no longer valid. Start again on the TV."));
                    return;
                }
                beginExchange(code, socket);
            });
    connect(m_mappings, &mal::MappingsStore::loadFinished, this, [this](bool ok) {
        QStringList waiting;
        for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
            if (it->waitingMappings)
                waiting.append(it.key());
        }
        if (!ok)
            qWarning("[MAL] anime mappings are unavailable; scrobble will retry");
        for (const QString &key : waiting) {
            auto item = m_items.find(key);
            if (item == m_items.end())
                continue;
            item->waitingMappings = false;
            if (!m_mappings->ready())
                finishItem(key, false, QStringLiteral("mappings unavailable"));
            else
                step(key);
        }
    });
    m_client->setRefresher([this](std::function<void(bool)> done) { refreshTokens(std::move(done)); });

    reloadSettings();
    loadTokens();
    m_client->setAccessToken(m_accessToken);
    if (m_enabled)
        m_mappings->ensureLoaded();
    if (!m_refreshToken.isEmpty() && QDateTime::currentSecsSinceEpoch() >= m_expiresAt - kRefreshSkewSec)
        refreshTokens([](bool) {});
    publish();
}

MalBackend::~MalBackend()
{
    mal::setProgressSink(nullptr);
}

QString MalBackend::get_auth_state()
{
    return m_refreshToken.isEmpty() ? QStringLiteral("none") : QStringLiteral("authed");
}

void MalBackend::logout()
{
    cancelConnect();
    clearTokens();
    setPhase(m_clientId.isEmpty() ? QStringLiteral("need_client") : QStringLiteral("ready"),
             QStringLiteral("DISCONNECTED"));
    emit authStateChanged();
}

void MalBackend::onSettingChanged(const QString &moduleId, const QString &key, const QVariant &)
{
    if (moduleId != QLatin1String(kModuleId))
        return;
    reloadSettings();
    if (m_enabled)
        m_mappings->ensureLoaded();
    if (key == QLatin1String("enabled") || key == QLatin1String("client_id"))
        publish();
}

void MalBackend::connectAccount()
{
    reloadSettings();
    if (m_clientId.isEmpty()) {
        setPhase(QStringLiteral("need_client"), QStringLiteral("SET CLIENT_ID IN CONFIG"));
        return;
    }
    const QString host = lanHost();
    if (host.isEmpty()) {
        setPhase(QStringLiteral("error"), QStringLiteral("NO LAN ADDRESS — SET REDIRECT_HOST"));
        return;
    }
    QString error;
    if (!m_oauth->start(quint16(m_port), &error)) {
        qWarning("[MAL] oauth listen failed: %s", qPrintable(error));
        setPhase(QStringLiteral("error"), QStringLiteral("PORT IN USE — CHANGE OAUTH_PORT"));
        return;
    }
    m_verifier = randomToken(64);
    m_state = randomToken(32);
    QUrl authorize(QStringLiteral("https://myanimelist.net/v1/oauth2/authorize"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("client_id"), m_clientId);
    query.addQueryItem(QStringLiteral("state"), m_state);
    query.addQueryItem(QStringLiteral("redirect_uri"), redirectUri());
    query.addQueryItem(QStringLiteral("code_challenge"), m_verifier);
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("plain"));
    authorize.setQuery(query);
    m_oauth->setAuthorizeUrl(authorize.toString(QUrl::FullyEncoded));
    buildQr(shortUrl());
    setPhase(QStringLiteral("waiting"), QStringLiteral("SCAN THE CODE WITH YOUR PHONE"));
    m_loginTimer->start(kLoginWindowMs);
}

void MalBackend::cancelConnect()
{
    m_loginTimer->stop();
    m_oauth->stop();
    m_verifier.clear();
    m_state.clear();
    m_qrBits.clear();
    m_qrSide = 0;
    if (m_phase == QLatin1String("waiting") || m_phase == QLatin1String("error"))
        setPhase(m_clientId.isEmpty() ? QStringLiteral("need_client") : QStringLiteral("ready"), QString());
}

void MalBackend::submitCodeFile()
{
    if (m_phase != QLatin1String("waiting"))
        return;
    QFile file(m_dataRoot + QStringLiteral("/mal_oauth_code.txt"));
    if (!file.open(QIODevice::ReadOnly)) {
        setPhase(QStringLiteral("waiting"), QStringLiteral("NO CODE FILE YET"));
        return;
    }
    const QString raw = QString::fromUtf8(file.readAll()).trimmed();
    file.close();
    QFile::remove(file.fileName());
    QString code = raw;
    if (raw.contains(QLatin1String("code="))) {
        const int queryAt = raw.indexOf(QLatin1Char('?'));
        const QUrlQuery query(queryAt >= 0 ? raw.mid(queryAt + 1) : raw);
        code = query.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded);
    }
    if (code.size() < 20) {
        setPhase(QStringLiteral("waiting"), QStringLiteral("CODE FILE WAS EMPTY"));
        return;
    }
    beginExchange(code, nullptr);
}

void MalBackend::observe(const mal::ProgressSample &sample)
{
    if (!m_enabled || m_refreshToken.isEmpty() || sample.itemId.isEmpty() || !sample.service)
        return;
    noteSample(sample);
}

void MalBackend::reloadSettings()
{
    m_enabled = false;
    m_clientId.clear();
    m_clientSecret.clear();
    m_port = kDefaultPort;
    m_redirectHost.clear();
    QFile file(m_dataRoot + QStringLiteral("/config.json"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject mod = QJsonDocument::fromJson(file.readAll()).object()
                                .value(QStringLiteral("modules")).toObject()
                                .value(QLatin1String(kModuleId)).toObject();
    m_enabled = jsonEnabled(mod.value(QStringLiteral("enabled")));
    m_clientId = mod.value(QStringLiteral("client_id")).toString().trimmed();
    m_clientSecret = mod.value(QStringLiteral("client_secret")).toString().trimmed();
    const int port = mod.value(QStringLiteral("oauth_port")).toInt();
    if (port > 0 && port < 65536)
        m_port = port;
    m_redirectHost = mod.value(QStringLiteral("redirect_host")).toString().trimmed();
}

void MalBackend::loadTokens()
{
    QFile file(m_dataRoot + QStringLiteral("/mal_auth.json"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject auth = QJsonDocument::fromJson(file.readAll()).object();
    m_accessToken = auth.value(QStringLiteral("access_token")).toString();
    m_refreshToken = auth.value(QStringLiteral("refresh_token")).toString();
    m_expiresAt = auth.value(QStringLiteral("expires_at")).toInteger(0);
    m_username = auth.value(QStringLiteral("username")).toString();
    if (m_refreshToken.isEmpty())
        clearTokens();
}

void MalBackend::saveTokens() const
{
    QJsonObject auth;
    auth.insert(QStringLiteral("access_token"), m_accessToken);
    auth.insert(QStringLiteral("refresh_token"), m_refreshToken);
    auth.insert(QStringLiteral("expires_at"), m_expiresAt);
    auth.insert(QStringLiteral("username"), m_username);
    QSaveFile file(m_dataRoot + QStringLiteral("/mal_auth.json"));
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("[MAL] could not store tokens");
        return;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(auth).toJson(QJsonDocument::Compact));
    if (!file.commit())
        qWarning("[MAL] could not store tokens");
    QFile::setPermissions(m_dataRoot + QStringLiteral("/mal_auth.json"),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

void MalBackend::clearTokens()
{
    m_accessToken.clear();
    m_refreshToken.clear();
    m_expiresAt = 0;
    m_username.clear();
    m_client->setAccessToken(QString());
    QFile::remove(m_dataRoot + QStringLiteral("/mal_auth.json"));
}

void MalBackend::publish()
{
    if (m_phase == QLatin1String("waiting"))
        return;
    if (!m_refreshToken.isEmpty())
        setPhase(QStringLiteral("linked"), QString());
    else if (m_clientId.isEmpty())
        setPhase(QStringLiteral("need_client"), QStringLiteral("ADD CLIENT_ID TO CONFIG"));
    else
        setPhase(QStringLiteral("ready"), QString());
}

void MalBackend::setPhase(const QString &phase, const QString &status)
{
    m_phase = phase;
    if (!status.isNull())
        m_statusLine = status;
    emit updated();
}

QString MalBackend::lanHost() const
{
    return m_redirectHost.isEmpty() ? detectLanIPv4() : m_redirectHost;
}

QString MalBackend::redirectUri() const
{
    return QStringLiteral("http://%1:%2/callback").arg(lanHost()).arg(m_port);
}

QString MalBackend::shortUrl() const
{
    return QStringLiteral("http://%1:%2/").arg(lanHost()).arg(m_port);
}

QString MalBackend::clientHint() const
{
    if (m_clientId.size() < 4)
        return m_clientId.isEmpty() ? QStringLiteral("NO CLIENT ID") : m_clientId;
    return QStringLiteral("CLIENT …") + m_clientId.right(4)
        + (m_clientSecret.isEmpty() ? QStringLiteral("  NO SECRET") : QStringLiteral("  SECRET SET"));
}

void MalBackend::buildQr(const QString &text)
{
    m_qrBits.clear();
    m_qrSide = 0;
    try {
        const QByteArray utf8 = text.toUtf8();
        const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(
            utf8.constData(), qrcodegen::QrCode::Ecc::MEDIUM);
        constexpr int quiet = 4;
        m_qrSide = qr.getSize() + quiet * 2;
        m_qrBits.reserve(m_qrSide * m_qrSide);
        for (int y = 0; y < m_qrSide; ++y) {
            for (int x = 0; x < m_qrSide; ++x) {
                const int mx = x - quiet;
                const int my = y - quiet;
                const bool dark = mx >= 0 && my >= 0 && mx < qr.getSize() && my < qr.getSize()
                    && qr.getModule(mx, my);
                m_qrBits.append(dark ? QLatin1Char('1') : QLatin1Char('0'));
            }
        }
    } catch (const std::exception &ex) {
        qWarning("[MAL] qr encode failed: %s", ex.what());
    }
}

void MalBackend::beginExchange(const QString &code, QTcpSocket *socket)
{
    QPointer<QTcpSocket> held(socket);
    QList<QPair<QString, QString>> fields = {
        {QStringLiteral("client_id"), m_clientId},
        {QStringLiteral("grant_type"), QStringLiteral("authorization_code")},
        {QStringLiteral("code"), code},
        {QStringLiteral("redirect_uri"), redirectUri()},
        {QStringLiteral("code_verifier"), m_verifier},
    };
    if (!m_clientSecret.isEmpty())
        fields.append({QStringLiteral("client_secret"), m_clientSecret});
    postToken(fields, [this, held](bool ok, int status) {
        if (!ok) {
            qWarning("[MAL] authorization code exchange failed (HTTP %d)", status);
            if (held)
                m_oauth->finish(held, false, QStringLiteral("240-MP could not exchange the code. Check the client id and secret."));
            setPhase(QStringLiteral("error"), QStringLiteral("MAL REJECTED THE LOGIN"));
            return;
        }
        m_loginTimer->stop();
        if (held)
            m_oauth->finish(held, true, QStringLiteral("240-MP is connected. You can close this page."));
        m_oauth->stop();
        m_verifier.clear();
        m_state.clear();
        m_client->setAccessToken(m_accessToken);
        m_client->getUsername([this](const QString &name, bool named) {
            if (named && !name.isEmpty()) {
                m_username = name;
                saveTokens();
            }
            setPhase(QStringLiteral("linked"), named ? name : QStringLiteral("CONNECTED"));
            emit authStateChanged();
            if (m_enabled)
                m_mappings->ensureLoaded();
        });
    });
}

void MalBackend::refreshTokens(const std::function<void(bool)> &done)
{
    if (m_refreshToken.isEmpty()) {
        done(false);
        return;
    }
    if (m_refreshing) {
        m_refreshWaiters.append(done);
        return;
    }
    m_refreshing = true;
    QList<QPair<QString, QString>> fields = {
        {QStringLiteral("client_id"), m_clientId},
        {QStringLiteral("grant_type"), QStringLiteral("refresh_token")},
        {QStringLiteral("refresh_token"), m_refreshToken},
    };
    if (!m_clientSecret.isEmpty())
        fields.append({QStringLiteral("client_secret"), m_clientSecret});
    postToken(fields, [this, done](bool ok, int status) {
        const bool permanent = status == 400 || status == 401 || status == 403;
        if (!ok && permanent) {
            qWarning("[MAL] refresh token rejected (HTTP %d) — sign in again", status);
            const bool wasSignedIn = !m_refreshToken.isEmpty();
            clearTokens();
            if (wasSignedIn)
                emit authStateChanged();
            publish();
        } else if (!ok) {
            qWarning("[MAL] token refresh failed (HTTP %d); keeping the saved session", status);
        } else {
            m_client->setAccessToken(m_accessToken);
        }
        m_refreshing = false;
        const auto waiters = m_refreshWaiters;
        m_refreshWaiters.clear();
        done(ok);
        for (const auto &waiter : waiters)
            waiter(ok);
    });
}

void MalBackend::postToken(const QList<QPair<QString, QString>> &fields,
                           const std::function<void(bool, int)> &done)
{
    QUrlQuery form;
    for (const auto &field : fields)
        form.addQueryItem(field.first, field.second);
    QNetworkRequest req(QUrl(QStringLiteral("https://myanimelist.net/v1/oauth2/token")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    req.setTransferTimeout(20000);
    auto *nam = new QNetworkAccessManager(this);
    auto *reply = nam->post(req, form.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, done]() {
        reply->deleteLater();
        nam->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError && status == 0) {
            done(false, 0);
            return;
        }
        if (status < 200 || status >= 300) {
            done(false, status);
            return;
        }
        const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
        if (body.value(QStringLiteral("access_token")).toString().isEmpty()) {
            done(false, status);
            return;
        }
        applyTokenResponse(body, true);
        done(true, status);
    });
}

void MalBackend::applyTokenResponse(const QJsonObject &body, bool keepRefresh)
{
    m_accessToken = body.value(QStringLiteral("access_token")).toString();
    const QString refresh = body.value(QStringLiteral("refresh_token")).toString();
    if (!refresh.isEmpty())
        m_refreshToken = refresh;
    else if (!keepRefresh)
        m_refreshToken.clear();
    const int expiresIn = body.value(QStringLiteral("expires_in")).toInt(3600);
    m_expiresAt = QDateTime::currentSecsSinceEpoch() + qMax(60, expiresIn);
    saveTokens();
}

void MalBackend::noteSample(const mal::ProgressSample &sample)
{
    const QString key = QString::fromUtf8(sample.service) + QLatin1Char('\n') + sample.itemId;
    if (m_done.contains(key))
        return;
    ItemState &state = m_items[key];
    state.service = QString::fromUtf8(sample.service);
    state.itemId = sample.itemId;
    if (sample.positionMs > 0)
        state.positionMs = sample.positionMs;
    if (sample.durationMs > 0)
        state.durationMs = sample.durationMs;
    if (state.inflight)
        return;
    if (QDateTime::currentMSecsSinceEpoch() < state.retryNotBefore)
        return;
    const qint64 duration = state.durationMs > 0 ? state.durationMs
                                                 : (state.runtimeKnown ? state.runtimeMs : 0);
    if (duration > 0 && !mal::crossedWatchedPoint(state.positionMs, duration))
        return;
    state.inflight = true;
    step(key);
}

void MalBackend::step(const QString &key)
{
    auto it = m_items.find(key);
    if (it == m_items.end() || m_done.contains(key) || !m_enabled || m_refreshToken.isEmpty()) {
        if (it != m_items.end())
            it->inflight = false;
        return;
    }
    ItemState &state = it.value();
    const qint64 duration = state.durationMs > 0 ? state.durationMs
                                                 : (state.runtimeKnown ? state.runtimeMs : 0);
    if (!state.haveIdentity) {
        fetchIdentity(key);
        return;
    }
    if (duration <= 0) {
        finishItem(key, true, QStringLiteral("no duration"));
        return;
    }
    if (!state.playable) {
        finishItem(key, true, QStringLiteral("not a library movie or episode"));
        return;
    }
    if (!mal::crossedWatchedPoint(state.positionMs, duration)) {
        state.inflight = false;
        return;
    }
    if (!m_mappings->ready()) {
        state.waitingMappings = true;
        m_mappings->ensureLoaded();
        return;
    }
    const mal::MalTarget target = mal::resolveMalTarget(m_mappings->index(), playbackRef(state));
    if (!target.valid()) {
        finishItem(key, true, QStringLiteral("no MAL id"));
        return;
    }
    m_client->getSnapshot(target.malId, [this, key, target](mal::ListSnapshot snapshot, bool ok) {
        if (!m_items.contains(key) || m_done.contains(key))
            return;
        if (!ok) {
            finishItem(key, false, QStringLiteral("could not read the MAL list entry"));
            return;
        }
        const mal::ScrobbleWrite write = mal::decideScrobble(target, snapshot, true);
        if (!write.write) {
            finishItem(key, true, QStringLiteral("progress already at or ahead of this episode"));
            return;
        }
        m_client->updateList(target.malId, write.progress, write.status,
                             [this, key, target, write](bool updated, int) {
                                 if (!updated) {
                                     finishItem(key, false, QStringLiteral("MAL list update failed"));
                                     return;
                                 }
                                 qInfo("[MAL] updated mal=%d episodes=%d status=%s",
                                       target.malId, write.progress, qPrintable(write.status));
                                 finishItem(key, true, QString());
                             });
    });
}

void MalBackend::finishItem(const QString &key, bool done, const QString &reason)
{
    auto it = m_items.find(key);
    if (it == m_items.end())
        return;
    if (done) {
        if (!reason.isEmpty())
            qInfo("[MAL] skip %s (%s)", qPrintable(it->itemId), qPrintable(reason));
        m_done.insert(key);
        it->inflight = false;
        it->waitingMappings = false;
        return;
    }
    it->failures += 1;
    it->inflight = false;
    it->waitingMappings = false;
    qWarning("[MAL] %s — %s", qPrintable(reason), qPrintable(it->itemId));
    if (it->failures >= kMaxFailures) {
        m_done.insert(key);
        return;
    }
    it->retryNotBefore = QDateTime::currentMSecsSinceEpoch() + kRetryMs;
}

void MalBackend::fetchIdentity(const QString &key)
{
    auto it = m_items.find(key);
    if (it == m_items.end())
        return;
    const QString service = it->service;
    const QString itemId = it->itemId;
    const auto apply = [this, key](const QVariantMap &raw) {
        auto item = m_items.find(key);
        if (item == m_items.end())
            return;
        if (!raw.value(QStringLiteral("ok")).toBool()) {
            finishItem(key, false, QStringLiteral("could not read library ids"));
            return;
        }
        const QString type = raw.value(QStringLiteral("type")).toString().toLower();
        item->movie = type == QLatin1String("movie");
        item->playable = item->movie || type == QLatin1String("episode");
        item->haveIdentity = true;
        item->runtimeKnown = true;
        item->runtimeMs = raw.value(QStringLiteral("runtimeMs")).toLongLong();
        if (raw.value(QStringLiteral("hasSeason")).toBool())
            item->season = raw.value(QStringLiteral("season")).toInt();
        else
            item->season.reset();
        if (raw.value(QStringLiteral("hasEpisode")).toBool())
            item->episode = raw.value(QStringLiteral("episode")).toInt();
        else
            item->episode.reset();
        if (raw.contains(QStringLiteral("providers")))
            item->ids = mal::externalIdsFromProviders(raw.value(QStringLiteral("providers")).toMap());
        else
            item->ids = mal::externalIdsFromPlex(raw.value(QStringLiteral("guids")).toStringList(),
                                                 raw.value(QStringLiteral("legacyGuid")).toString());
        step(key);
    };
    if (service == QLatin1String("plex") && m_plex)
        m_plex->fetchExternalIdentity(itemId, apply);
    else if (service == QLatin1String("emby") && m_emby)
        m_emby->fetchExternalIdentity(itemId, apply);
    else if (service == QLatin1String("jellyfin") && m_jellyfin)
        m_jellyfin->fetchExternalIdentity(itemId, apply);
    else
        finishItem(key, true, QStringLiteral("no library client"));
}

mal::PlaybackRef MalBackend::playbackRef(const ItemState &state) const
{
    mal::PlaybackRef ref;
    ref.movie = state.movie;
    ref.season = state.season;
    ref.episode = state.episode;
    ref.ids = state.ids;
    return ref;
}
