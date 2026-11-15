#pragma once

#include <QJsonObject>
#include <QObject>
#include <QVariantMap>

class QThread;
class PcStatsWorker;

// Polls LibreHardwareMonitor and the frame-time companion on a worker thread
// while the PC Stats screen is open. The timer starts in start() and stops in
// stop(), so a closed module does not talk to the PC and does not touch the
// idle tracker — stat refreshes are not key events, and the app screensaver
// still blanks this screen on its own timeout.
class PcStatsBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured NOTIFY targetChanged)
    Q_PROPERTY(QString host READ host NOTIFY targetChanged)
    Q_PROPERTY(QString configPath READ configPath CONSTANT)
    Q_PROPERTY(bool connecting READ connecting NOTIFY statusChanged)
    Q_PROPERTY(bool offline READ offline NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap hardware READ hardware NOTIFY hardwareChanged)
    Q_PROPERTY(QVariantMap frames READ frames NOTIFY framesChanged)
    Q_PROPERTY(int driftIntervalMs READ driftIntervalMs NOTIFY driftIntervalChanged)

public:
    explicit PcStatsBackend(const QString &dataRoot, QObject *parent = nullptr);
    ~PcStatsBackend() override;

    bool configured() const;
    QString host() const;
    QString configPath() const;
    bool connecting() const { return m_connecting; }
    bool offline() const { return m_offline; }
    QVariantMap hardware() const { return m_hardware; }
    QVariantMap frames() const { return m_frames; }
    int driftIntervalMs() const;

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

signals:
    void targetChanged();
    void statusChanged();
    void hardwareChanged();
    void framesChanged();
    void driftIntervalChanged();

public slots:
    void onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value);

private slots:
    void onHardware(QVariantMap snapshot, bool ok);
    void onFrames(QVariantMap snapshot, bool ok);

private:
    QJsonObject moduleConfig() const;
    QString resolvedHost() const;
    int resolvedPort(const char *key, int fallback) const;
    int pollIntervalMs() const;
    void ensureThread();
    void pushTarget();

    QString m_dataRoot;
    QThread *m_thread = nullptr;
    PcStatsWorker *m_worker = nullptr;
    bool m_running = false;
    bool m_connecting = false;
    bool m_offline = false;
    int m_failStreak = 0;
    QVariantMap m_hardware;
    QVariantMap m_frames;
    QString m_activeHost;
};
