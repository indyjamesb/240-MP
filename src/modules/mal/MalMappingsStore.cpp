#include "MalMappingsStore.h"
#include "MalPackedList.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>

namespace mal {

namespace {
constexpr auto kAssetUrl = "https://github.com/edde746/anime-mappings/releases/latest/download/anime-mappings.bin.gz";
constexpr auto kSumsUrl = "https://github.com/edde746/anime-mappings/releases/latest/download/SHA256SUMS";
constexpr qint64 kRefreshIntervalMs = 24ll * 60 * 60 * 1000;
constexpr int kTimeoutMs = 60000;

QNetworkRequest githubRequest(const QUrl &url)
{
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "240-MP");
    req.setTransferTimeout(kTimeoutMs);
    return req;
}

void armTimeout(QNetworkReply *reply)
{
    QTimer::singleShot(kTimeoutMs, reply, [reply]() {
        if (reply->isRunning())
            reply->abort();
    });
}
} // namespace

MappingsStore::MappingsStore(const QString &dataRoot, QObject *parent)
    : QObject(parent)
    , m_dataRoot(dataRoot)
    , m_nam(new QNetworkAccessManager(this))
{
}

void MappingsStore::ensureLoaded()
{
    if (m_loading || ready()) {
        if (ready() && !m_refreshing)
            download(true);
        return;
    }
    m_loading = true;
    const QString path = m_dataRoot + QStringLiteral("/anime-mappings.bin.gz");
    if (QFile::exists(path)) {
        decodeFile(path);
        m_loading = false;
        if (ready()) {
            emit loadFinished(true);
            download(true);
            return;
        }
    }
    download(false);
}

void MappingsStore::decodeFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("[MAL] mappings cache could not be read");
        return;
    }
    try {
        AnimeMappings decoded = decodeAnimeMappings(file.readAll());
        if (!decoded.isEmpty()) {
            m_index = std::move(decoded);
            qInfo("[MAL] mappings ready (%d tvdb series)", m_index.fribb.tvdbSeriesCount());
        }
    } catch (const std::exception &ex) {
        qWarning("[MAL] mappings cache is unreadable (%s) — it will be downloaded again", ex.what());
        QFile::remove(path);
    }
}

void MappingsStore::download(bool conditional)
{
    if (conditional) {
        const qint64 last = lastCheckMs();
        if (last > 0 && QDateTime::currentMSecsSinceEpoch() - last < kRefreshIntervalMs)
            return;
        if (!ready())
            return;
    }
    if (m_refreshing)
        return;
    m_refreshing = true;

    QNetworkRequest req = githubRequest(QUrl(QString::fromLatin1(kAssetUrl)));
    if (conditional) {
        const QByteArray etag = storedEtag();
        if (!etag.isEmpty())
            req.setRawHeader("If-None-Match", etag);
    }
    auto *reply = m_nam->get(req);
    armTimeout(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, conditional]() {
        reply->deleteLater();
        const auto finish = [this, conditional](bool ok) {
            m_refreshing = false;
            m_loading = false;
            if (!conditional || ok)
                emit loadFinished(ok || ready());
        };
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError && status != 304) {
            qWarning("[MAL] mappings download failed: %s", qPrintable(reply->errorString()));
            finish(false);
            return;
        }
        const QByteArray etag = reply->rawHeader("ETag");
        if (status == 304) {
            writeMeta(etag);
            qInfo("[MAL] mappings unchanged");
            finish(true);
            return;
        }
        const QByteArray body = reply->readAll();
        if (body.isEmpty()) {
            qWarning("[MAL] mappings download was empty");
            finish(false);
            return;
        }

        // Checksum is best-effort. A missing sums file does not block a
        // successful download; a mismatch does.
        auto *sums = m_nam->get(githubRequest(QUrl(QString::fromLatin1(kSumsUrl))));
        armTimeout(sums);
        connect(sums, &QNetworkReply::finished, this, [this, sums, body, finish, etag]() {
            sums->deleteLater();
            if (sums->error() == QNetworkReply::NoError) {
                const QString expected = QString::fromUtf8(sums->readAll()).trimmed().left(64).toLower();
                const QString actual = QString::fromLatin1(
                    QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex());
                if (expected.size() == 64 && expected != actual) {
                    qWarning("[MAL] mappings checksum did not match SHA256SUMS");
                    finish(false);
                    return;
                }
            }
            try {
                AnimeMappings decoded = decodeAnimeMappings(body);
                if (decoded.isEmpty()) {
                    qWarning("[MAL] mappings file decoded empty");
                    finish(false);
                    return;
                }
                QSaveFile out(m_dataRoot + QStringLiteral("/anime-mappings.bin.gz"));
                if (!out.open(QIODevice::WriteOnly) || out.write(body) != body.size() || !out.commit()) {
                    qWarning("[MAL] mappings could not be cached; using the download for this session");
                }
                m_index = std::move(decoded);
                writeMeta(etag);
                qInfo("[MAL] mappings updated (%d tvdb series)", m_index.fribb.tvdbSeriesCount());
                finish(true);
            } catch (const std::exception &ex) {
                qWarning("[MAL] mappings download could not be decoded (%s)", ex.what());
                finish(false);
            }
        });
    });
}

void MappingsStore::writeMeta(const QByteArray &etag)
{
    QJsonObject meta;
    QFile existing(m_dataRoot + QStringLiteral("/mal_mappings_meta.json"));
    if (existing.open(QIODevice::ReadOnly))
        meta = QJsonDocument::fromJson(existing.readAll()).object();
    if (!etag.isEmpty())
        meta.insert(QStringLiteral("etag"), QString::fromUtf8(etag));
    meta.insert(QStringLiteral("lastCheck"), QDateTime::currentMSecsSinceEpoch());
    QSaveFile out(m_dataRoot + QStringLiteral("/mal_mappings_meta.json"));
    if (!out.open(QIODevice::WriteOnly))
        return;
    out.write(QJsonDocument(meta).toJson(QJsonDocument::Compact));
    out.commit();
}

QByteArray MappingsStore::storedEtag() const
{
    QFile file(m_dataRoot + QStringLiteral("/mal_mappings_meta.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("etag")).toString().toUtf8();
}

qint64 MappingsStore::lastCheckMs() const
{
    QFile file(m_dataRoot + QStringLiteral("/mal_mappings_meta.json"));
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("lastCheck")).toInteger(0);
}

} // namespace mal
