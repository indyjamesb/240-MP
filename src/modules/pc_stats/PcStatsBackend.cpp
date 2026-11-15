#include "PcStatsBackend.h"

#include "PcStatsParser.h"

#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariant>

namespace {

const char *kModuleId = "com.240mp.pc_stats";
const char *kDefaultHost = "10.0.0.44";
const int kLhmPort = 8085;
const int kFramesPort = 8086;
const int kRequestTimeoutMs = 1200;

int leadingInt(const QString &text, int fallback)
{
    bool ok = false;
    const int n = text.trimmed().section(QLatin1Char(' '), 0, 0).toInt(&ok);
    return ok && n > 0 ? n : fallback;
}

QString cleanHost(QString host)
{
    host = host.trimmed();
    if (host.startsWith(QLatin1String("http://"), Qt::CaseInsensitive))
        host = host.mid(7);
    else if (host.startsWith(QLatin1String("https://"), Qt::CaseInsensitive))
        host = host.mid(8);
    const int slash = host.indexOf(QLatin1Char('/'));
    if (slash >= 0)
        host = host.left(slash);
    const int colon = host.lastIndexOf(QLatin1Char(':'));
    if (colon > 0) {
        bool ok = false;
        host.mid(colon + 1).toInt(&ok);
        if (ok)
            host = host.left(colon);
    }
    return host.trimmed();
}

} // namespace

class PcStatsWorker : public QObject {
    Q_OBJECT
public:
    explicit PcStatsWorker(QObject *parent = nullptr) : QObject(parent) {}

public slots:
    void begin(QString host, int lhmPort, int framesPort, int intervalMs)
    {
        host = cleanHost(host);
        if (host.isEmpty() || lhmPort <= 0 || framesPort <= 0 || intervalMs < 250)
            return;

        m_host = host;
        m_lhmPort = lhmPort;
        m_framesPort = framesPort;
        m_active = true;
        ++m_generation;
        abortReply(m_lhmReply);
        abortReply(m_framesReply);

        if (!m_nam)
            m_nam = new QNetworkAccessManager(this);
        if (!m_timer) {
            m_timer = new QTimer(this);
            connect(m_timer, &QTimer::timeout, this, &PcStatsWorker::poll);
        }
        m_timer->setInterval(intervalMs);
        if (!m_timer->isActive())
            m_timer->start();
        poll();
    }

    void end()
    {
        m_active = false;
        ++m_generation;
        if (m_timer)
            m_timer->stop();
        abortReply(m_lhmReply);
        abortReply(m_framesReply);
    }

signals:
    void hardwareReady(QVariantMap snapshot, bool ok);
    void framesReady(QVariantMap snapshot, bool ok);

private:
    void abortReply(QNetworkReply *&reply)
    {
        if (!reply)
            return;
        QNetworkReply *old = reply;
        reply = nullptr;
        old->abort();
        old->deleteLater();
    }

    QNetworkReply *get(const QString &path, int port)
    {
        QUrl url;
        url.setScheme(QStringLiteral("http"));
        url.setHost(m_host);
        url.setPort(port);
        url.setPath(path);
        QNetworkRequest request(url);
        request.setTransferTimeout(kRequestTimeoutMs);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("240-MP"));
        return m_nam->get(request);
    }

    void poll()
    {
        if (!m_active || !m_nam)
            return;
        const quint64 generation = m_generation;
        if (!m_lhmReply) {
            m_lhmReply = get(QStringLiteral("/data.json"), m_lhmPort);
            QNetworkReply *reply = m_lhmReply;
            connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
                if (m_lhmReply == reply)
                    m_lhmReply = nullptr;
                reply->deleteLater();
                if (!m_active || generation != m_generation)
                    return;
                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const bool httpOk = reply->error() == QNetworkReply::NoError && (status == 0 || status < 400);
                if (!httpOk) {
                    emit hardwareReady({}, false);
                    return;
                }
                const pcstats::HardwareSnapshot snapshot = pcstats::parseLibreHardwareMonitor(reply->readAll());
                if (!snapshot.parsed)
                    emit hardwareReady({}, false);
                else
                    emit hardwareReady(pcstats::hardwareToVariant(snapshot), true);
            });
        }
        if (!m_framesReply) {
            m_framesReply = get(QStringLiteral("/stats.json"), m_framesPort);
            QNetworkReply *reply = m_framesReply;
            connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
                if (m_framesReply == reply)
                    m_framesReply = nullptr;
                reply->deleteLater();
                if (!m_active || generation != m_generation)
                    return;
                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const bool httpOk = reply->error() == QNetworkReply::NoError && (status == 0 || status < 400);
                if (!httpOk) {
                    emit framesReady({}, false);
                    return;
                }
                const pcstats::FrameSnapshot snapshot = pcstats::parseFrameStats(reply->readAll());
                if (!snapshot.parsed)
                    emit framesReady({}, false);
                else
                    emit framesReady(pcstats::framesToVariant(snapshot), true);
            });
        }
    }

    QString m_host;
    int m_lhmPort = kLhmPort;
    int m_framesPort = kFramesPort;
    bool m_active = false;
    quint64 m_generation = 0;
    QNetworkAccessManager *m_nam = nullptr;
    QTimer *m_timer = nullptr;
    QNetworkReply *m_lhmReply = nullptr;
    QNetworkReply *m_framesReply = nullptr;
};

PcStatsBackend::PcStatsBackend(const QString &dataRoot, QObject *parent)
    : QObject(parent)
    , m_dataRoot(dataRoot)
{
}

PcStatsBackend::~PcStatsBackend()
{
    stop();
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(3000);
    }
}

QJsonObject PcStatsBackend::moduleConfig() const
{
    QFile file(m_dataRoot + QStringLiteral("/config.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return {};
    return doc.object().value(QStringLiteral("modules")).toObject()
        .value(QLatin1String(kModuleId)).toObject();
}

QString PcStatsBackend::resolvedHost() const
{
    const QJsonObject config = moduleConfig();
    // An explicit empty host is "not set" (the setup screen). A missing key
    // keeps James's PC, which has a static LAN address.
    if (config.contains(QStringLiteral("host")))
        return cleanHost(config.value(QStringLiteral("host")).toString());
    return QString::fromLatin1(kDefaultHost);
}

int PcStatsBackend::resolvedPort(const char *key, int fallback) const
{
    const QJsonObject config = moduleConfig();
    if (!config.contains(QLatin1String(key)))
        return fallback;
    const QJsonValue value = config.value(QLatin1String(key));
    int port = value.isDouble() ? value.toInt() : value.toString().trimmed().toInt();
    if (port <= 0 || port > 65535)
        return fallback;
    return port;
}

int PcStatsBackend::pollIntervalMs() const
{
    const int seconds = qBound(1, leadingInt(moduleConfig().value(QStringLiteral("poll_interval")).toString(), 2), 30);
    return seconds * 1000;
}

int PcStatsBackend::driftIntervalMs() const
{
    const int minutes = qBound(1, leadingInt(moduleConfig().value(QStringLiteral("drift_interval")).toString(), 3), 60);
    return minutes * 60 * 1000;
}

bool PcStatsBackend::configured() const
{
    return !resolvedHost().isEmpty();
}

QString PcStatsBackend::host() const
{
    return resolvedHost();
}

QString PcStatsBackend::configPath() const
{
    return m_dataRoot + QStringLiteral("/config.json");
}

void PcStatsBackend::ensureThread()
{
    if (m_thread)
        return;
    m_thread = new QThread(this);
    m_worker = new PcStatsWorker;
    m_worker->moveToThread(m_thread);
    connect(m_worker, &PcStatsWorker::hardwareReady, this, &PcStatsBackend::onHardware);
    connect(m_worker, &PcStatsWorker::framesReady, this, &PcStatsBackend::onFrames);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread->start();
}

void PcStatsBackend::pushTarget()
{
    ensureThread();
    const QString host = resolvedHost();
    m_activeHost = host;
    QMetaObject::invokeMethod(m_worker, "begin", Qt::QueuedConnection,
                              Q_ARG(QString, host),
                              Q_ARG(int, resolvedPort("lhm_port", kLhmPort)),
                              Q_ARG(int, resolvedPort("frames_port", kFramesPort)),
                              Q_ARG(int, pollIntervalMs()));
}

void PcStatsBackend::start()
{
    if (resolvedHost().isEmpty()) {
        m_running = false;
        m_connecting = false;
        m_offline = false;
        emit statusChanged();
        emit targetChanged();
        return;
    }
    m_running = true;
    // Connecting until this visit gets a reply, so a previous reading is not
    // shown as if it were live.
    m_connecting = true;
    m_offline = false;
    m_failStreak = 0;
    emit statusChanged();
    emit targetChanged();
    pushTarget();
}

void PcStatsBackend::stop()
{
    m_running = false;
    if (!m_worker || !m_thread || !m_thread->isRunning())
        return;
    const bool invoked = QMetaObject::invokeMethod(m_worker, "end", Qt::BlockingQueuedConnection);
    if (!invoked)
        qWarning("[pc-stats] failed to stop the poller");
}

void PcStatsBackend::onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value)
{
    Q_UNUSED(moduleId)
    Q_UNUSED(value)
    if (key == QLatin1String("drift_interval"))
        emit driftIntervalChanged();
    if (!m_running)
        return;
    if (key == QLatin1String("poll_interval") || key == QLatin1String("host")
            || key == QLatin1String("lhm_port") || key == QLatin1String("frames_port")) {
        emit targetChanged();
        if (resolvedHost().isEmpty())
            stop();
        else
            pushTarget();
    }
}

void PcStatsBackend::onHardware(QVariantMap snapshot, bool ok)
{
    if (!m_running)
        return;
    if (ok) {
        const bool wasDown = m_offline || m_connecting || m_hardware.isEmpty();
        m_failStreak = 0;
        m_connecting = false;
        m_offline = false;
        snapshot.insert(QStringLiteral("host"), m_activeHost);
        m_hardware = snapshot;
        emit hardwareChanged();
        emit statusChanged();
        if (wasDown)
            qInfo("[pc-stats] %s online", qPrintable(m_activeHost));
        return;
    }

    ++m_failStreak;
    m_connecting = false;
    // One dropped reply keeps the last good sample on screen. Two in a row,
    // or a failure before anything has arrived, is the offline state.
    if (m_hardware.isEmpty() || m_failStreak >= 2) {
        const bool changed = !m_offline;
        m_offline = true;
        emit statusChanged();
        if (changed)
            qInfo("[pc-stats] %s offline", qPrintable(m_activeHost));
    }
}

void PcStatsBackend::onFrames(QVariantMap snapshot, bool ok)
{
    if (!m_running)
        return;
    if (!ok) {
        QVariantMap unavailable;
        unavailable.insert(QStringLiteral("state"), QStringLiteral("unavailable"));
        unavailable.insert(QStringLiteral("process"), QString());
        unavailable.insert(QStringLiteral("fpsAvg"), QStringLiteral("--"));
        unavailable.insert(QStringLiteral("fpsLow"), QStringLiteral("--"));
        unavailable.insert(QStringLiteral("frameMs"), QVariantList());
        if (m_frames.value(QStringLiteral("state")).toString() == QLatin1String("unavailable")
                && m_frames.value(QStringLiteral("frameMs")).toList().isEmpty())
            return;
        m_frames = unavailable;
        emit framesChanged();
        return;
    }
    m_frames = snapshot;
    emit framesChanged();
}

#include "PcStatsBackend.moc"
