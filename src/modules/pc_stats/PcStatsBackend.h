#pragma once

#include "PcStatsSwap.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QVariantMap>

class QThread;
class QTimer;
class PcStatsWorker;

// Polls LibreHardwareMonitor and the frame-time companion on a worker thread
// while the PC Stats screen is open. The timer starts in start() and stops in
// stop(), so a closed module does not talk to the PC and does not touch the
// idle tracker — stat refreshes are not key events, and the app screensaver
// still blanks this screen on its own timeout.
//
// Section swap is a second clock. It runs only while this screen is open and
// the swap setting is on, and stop() keeps the elapsed time, so leaving and
// coming back continues the same interval. Offline replies (the PC asleep)
// do not touch it. The readings and the frame history live in m_hardware /
// m_frames and are not cleared when the halves trade places.
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
    Q_PROPERTY(bool sectionSwapEnabled READ sectionSwapEnabled NOTIFY sectionSwapConfigChanged)
    Q_PROPERTY(int swapIntervalMs READ swapIntervalMs NOTIFY sectionSwapConfigChanged)
    Q_PROPERTY(bool gpuOnTop READ gpuOnTop NOTIFY gpuOnTopChanged)

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
    bool sectionSwapEnabled() const;
    int swapIntervalMs() const;
    bool gpuOnTop() const { return m_gpuOnTop; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

signals:
    void targetChanged();
    void statusChanged();
    void hardwareChanged();
    void framesChanged();
    void driftIntervalChanged();
    void sectionSwapConfigChanged();
    void gpuOnTopChanged();

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
    void syncSwapClock();

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
    bool m_screenOpen = false;
    bool m_gpuOnTop = false;
    pcstats::SectionSwapClock m_swapClock;
    QElapsedTimer m_swapTick;
    QTimer *m_swapTimer = nullptr;
};
