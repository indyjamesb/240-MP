#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QVariantMap>

// LibreHardwareMonitor's /data.json is a tree, and the companion agent's
// /stats.json is a small flat object. Both are parsed here, with no network,
// so the tests can feed fixtures without standing up a server.
//
// Sensors are chosen by type and name (and the SensorId path when LHM sent
// one). Nothing here indexes Children — a LibreHardwareMonitor update that
// reorders the tree has to keep producing the same snapshot.

namespace pcstats {

struct FanReading {
    QString hardware;
    QString name;
    double rpm = 0;
};

struct DriveReading {
    QString name;
    bool hasTemp = false;
    double tempC = 0;
    bool hasRead = false;
    double readMBps = 0;
    bool hasWrite = false;
    double writeMBps = 0;
};

struct HardwareSnapshot {
    bool parsed = false;

    QString machine;

    QString cpuName;
    bool hasCpuTemp = false;
    double cpuTempC = 0;
    bool hasCpuLoad = false;
    double cpuLoadPct = 0;
    bool hasCpuClock = false;
    double cpuClockMHz = 0;
    bool hasCpuPower = false;
    double cpuPowerW = 0;

    QString gpuName;
    bool hasGpuTemp = false;
    double gpuTempC = 0;
    bool hasGpuHotspot = false;
    double gpuHotspotC = 0;
    bool hasGpuLoad = false;
    double gpuLoadPct = 0;
    bool hasGpuClock = false;
    double gpuClockMHz = 0;
    bool hasGpuMemClock = false;
    double gpuMemClockMHz = 0;
    bool hasGpuPower = false;
    double gpuPowerW = 0;
    bool hasVramUsed = false;
    double vramUsedMb = 0;
    bool hasVramTotal = false;
    double vramTotalMb = 0;

    QList<FanReading> fans;

    bool hasRamUsed = false;
    double ramUsedMb = 0;
    bool hasRamTotal = false;
    double ramTotalMb = 0;
    bool hasRamLoad = false;
    double ramLoadPct = 0;

    QList<DriveReading> drives;

    bool hasNet = false;
    QString netName;
    double netDownMBps = 0;
    double netUpMBps = 0;
};

struct FrameSnapshot {
    bool parsed = false;
    // Set when the companion answered but has nothing to graph (PresentMon
    // missing, no fresh samples). Empty when the body simply isn't JSON.
    QString error;
    QString process;
    bool hasFps = false;
    double fpsAvg = 0;
    double fpsOnePctLow = 0;
    // Oldest first. Capped for the graph; the FPS figures are computed from
    // the full list before that cap, unless the payload already has them.
    QList<double> frameMs;
};

// Y range for the frame-time graph. Fits the samples (with a little padding)
// instead of a fixed 0–33 ms scale, so a run of ~8 ms frames is not a flat
// line along the bottom and a stutter still reaches the top of the box.
struct FrameGraphScale {
    double low = 0;
    double high = 1;
};

FrameGraphScale frameGraphScale(const QList<double> &samples);

HardwareSnapshot parseLibreHardwareMonitor(const QByteArray &json);
FrameSnapshot parseFrameStats(const QByteArray &json);

// Display strings for the CRT view. Missing readings are "--" so the row
// stays put instead of collapsing when one sensor is absent.
QVariantMap hardwareToVariant(const HardwareSnapshot &snapshot);
QVariantMap framesToVariant(const FrameSnapshot &snapshot);

} // namespace pcstats
