#include "AppCore.h"
#include "PcStatsBackend.h"
#include "PcStatsParser.h"
#include "PcStatsSwap.h"

#include <QtTest>

#include <algorithm>
#include <cmath>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

using namespace pcstats;

namespace {

QByteArray readFixture(const char *name)
{
    const QString path = QString::fromLatin1(PC_STATS_FIXTURE_DIR) + QLatin1Char('/')
        + QLatin1String(name);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool near(double actual, double expected, double tolerance = 0.05)
{
    return std::abs(actual - expected) <= tolerance;
}

QJsonValue reverseChildren(const QJsonValue &value)
{
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        QJsonArray reversed;
        for (int i = array.size() - 1; i >= 0; --i)
            reversed.append(reverseChildren(array.at(i)));
        return reversed;
    }
    if (!value.isObject())
        return value;
    QJsonObject object = value.toObject();
    const QJsonValue children = object.value(QStringLiteral("Children"));
    if (children.isArray()) {
        const QJsonArray array = children.toArray();
        QJsonArray reversed;
        for (int i = array.size() - 1; i >= 0; --i)
            reversed.append(reverseChildren(array.at(i)));
        object.insert(QStringLiteral("Children"), reversed);
    }
    return object;
}

QSet<int> fanRpms(const HardwareSnapshot &snapshot)
{
    QSet<int> rpms;
    for (const FanReading &fan : snapshot.fans)
        rpms.insert(qRound(fan.rpm));
    return rpms;
}

QSet<int> driveTemps(const HardwareSnapshot &snapshot)
{
    QSet<int> temps;
    for (const DriveReading &drive : snapshot.drives)
        temps.insert(qRound(drive.tempC));
    return temps;
}

} // namespace

class PcStatsTests : public QObject {
    Q_OBJECT
private slots:
    void lhmFixtureParsesByNameNotIndex();
    void lhmIgnoresChildOrder();
    void lhmCommaDecimalAndNumericValue();
    void lhmMalformedIsRejected();
    void frameGraphUsesFixedScale();
    void driveRatesAndFanLabels();
    void framesFixtureKeepsReportedFps();
    void framesComputesAverageAndOnePercentLow();
    void framesNoGameAndCompanionError();
    void homeMenuPlacesPcStatsUnderPlex();
    void sectionSwapPhaseFollowsElapsed();
    void sectionSwapClockSurvivesPause();
    void sectionSwapConfigDefaults();
    void backendSwapKeysDefaultWhenAbsent();
};

void PcStatsTests::lhmFixtureParsesByNameNotIndex()
{
    const QByteArray json = readFixture("lhm_data.json");
    QVERIFY(!json.isEmpty());

    const HardwareSnapshot snapshot = parseLibreHardwareMonitor(json);
    QVERIFY(snapshot.parsed);
    QCOMPARE(snapshot.machine, QStringLiteral("JAMES5080"));

    // Package, not the hotter per-core reading and not the motherboard voltage
    // that sits at the front of the tree.
    QCOMPARE(snapshot.cpuName, QStringLiteral("AMD Ryzen 7 9800X3D"));
    QVERIFY(snapshot.hasCpuTemp);
    QVERIFY(near(snapshot.cpuTempC, 62.0));
    QVERIFY(snapshot.hasCpuLoad);
    QVERIFY(near(snapshot.cpuLoadPct, 48.4));
    QVERIFY(snapshot.hasCpuClock);
    QVERIFY(near(snapshot.cpuClockMHz, 5000.0));
    QVERIFY(snapshot.hasCpuPower);
    QVERIFY(near(snapshot.cpuPowerW, 142.2));

    // Core temperature and clock, not the memory junction, shader clock, or
    // the D3D memory counter that is listed first.
    QCOMPARE(snapshot.gpuName, QStringLiteral("NVIDIA GeForce RTX 5080"));
    QVERIFY(snapshot.hasGpuTemp);
    QVERIFY(near(snapshot.gpuTempC, 67.0));
    QVERIFY(snapshot.hasGpuHotspot);
    QVERIFY(near(snapshot.gpuHotspotC, 81.0));
    QVERIFY(snapshot.hasGpuLoad);
    QVERIFY(near(snapshot.gpuLoadPct, 97.0));
    QVERIFY(snapshot.hasGpuClock);
    QVERIFY(near(snapshot.gpuClockMHz, 2760.0));
    QVERIFY(snapshot.hasGpuMemClock);
    QVERIFY(near(snapshot.gpuMemClockMHz, 14001.0));
    QVERIFY(snapshot.hasGpuPower);
    QVERIFY(near(snapshot.gpuPowerW, 320.0));
    QVERIFY(snapshot.hasVramUsed);
    QVERIFY(near(snapshot.vramUsedMb, 11264.0));
    QVERIFY(snapshot.hasVramTotal);
    QVERIFY(near(snapshot.vramTotalMb, 16384.0));

    QCOMPARE(fanRpms(snapshot), QSet<int>({1600, 900}));
    QVERIFY(snapshot.hasRamUsed);
    QVERIFY(near(snapshot.ramUsedMb, 15.0 * 1024.0));
    QVERIFY(snapshot.hasRamTotal);
    QVERIFY(near(snapshot.ramTotalMb, 32.0 * 1024.0));
    QVERIFY(snapshot.hasRamLoad);
    QVERIFY(near(snapshot.ramLoadPct, 47.0));
    QCOMPARE(driveTemps(snapshot), QSet<int>({42, 36}));

    QVERIFY(snapshot.hasNet);
    QCOMPARE(snapshot.netName, QStringLiteral("Ethernet"));
    QVERIFY(near(snapshot.netDownMBps, 2.5));
    QVERIFY(near(snapshot.netUpMBps, 120.0 / 1024.0, 0.01));

    const QVariantMap view = hardwareToVariant(snapshot);
    QCOMPARE(view.value(QStringLiteral("cpuLine")).toString(),
             QStringLiteral("9800X3D 62° 48% 5.0G 142W"));
    QCOMPARE(view.value(QStringLiteral("gpuShort")).toString(),
             QStringLiteral("RTX 5080"));
    QCOMPARE(view.value(QStringLiteral("gpuTempLine")).toString(),
             QStringLiteral("TEMP 67°"));
    QCOMPARE(view.value(QStringLiteral("gpuHotLine")).toString(),
             QStringLiteral("HOTSPOT 81°"));
    QCOMPARE(view.value(QStringLiteral("gpuLoadLine")).toString(),
             QStringLiteral("LOAD 97%"));
    QCOMPARE(view.value(QStringLiteral("gpuClockLine")).toString(),
             QStringLiteral("CLK 2.8G"));
    QCOMPARE(view.value(QStringLiteral("gpuPowerLine")).toString(),
             QStringLiteral("PWR 320W"));
    QVERIFY(view.value(QStringLiteral("gpuMemLine")).toString().isEmpty());
    QCOMPARE(view.value(QStringLiteral("gpuVramLine")).toString(),
             QStringLiteral("VRAM 11/16G"));
    QCOMPARE(view.value(QStringLiteral("ramLine")).toString(),
             QStringLiteral("15/32G 47%"));
    QCOMPARE(view.value(QStringLiteral("fanLine")).toString(),
             QStringLiteral("GPU1 1600  SYS1 900"));
    QCOMPARE(view.value(QStringLiteral("driveLines")).toStringList(),
             QStringList({QStringLiteral("SAMSUNG 42°  R 12.5 MB/s  W 400 KB/s"),
                          QStringLiteral("WD 36°")}));
    QCOMPARE(view.value(QStringLiteral("netLine")).toString(),
             QStringLiteral("DOWN 2.5M UP 120K"));
}

void PcStatsTests::lhmIgnoresChildOrder()
{
    const QByteArray json = readFixture("lhm_data.json");
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    const QByteArray reversed = QJsonDocument(reverseChildren(doc.object()).toObject()).toJson();

    const HardwareSnapshot original = parseLibreHardwareMonitor(json);
    const HardwareSnapshot shuffled = parseLibreHardwareMonitor(reversed);
    QVERIFY(original.parsed);
    QVERIFY(shuffled.parsed);
    QCOMPARE(shuffled.machine, original.machine);
    QVERIFY(near(shuffled.cpuTempC, original.cpuTempC));
    QVERIFY(near(shuffled.cpuLoadPct, original.cpuLoadPct));
    QVERIFY(near(shuffled.cpuClockMHz, original.cpuClockMHz));
    QVERIFY(near(shuffled.cpuPowerW, original.cpuPowerW));
    QVERIFY(near(shuffled.gpuTempC, original.gpuTempC));
    QVERIFY(near(shuffled.gpuHotspotC, original.gpuHotspotC));
    QVERIFY(near(shuffled.gpuLoadPct, original.gpuLoadPct));
    QVERIFY(near(shuffled.gpuClockMHz, original.gpuClockMHz));
    QVERIFY(near(shuffled.vramUsedMb, original.vramUsedMb));
    QVERIFY(near(shuffled.ramUsedMb, original.ramUsedMb));
    QVERIFY(near(shuffled.netDownMBps, original.netDownMBps));
    QCOMPARE(shuffled.netName, original.netName);
    QCOMPARE(fanRpms(shuffled), fanRpms(original));
    QCOMPARE(driveTemps(shuffled), driveTemps(original));
}

void PcStatsTests::lhmCommaDecimalAndNumericValue()
{
    const char *comma =
        R"({"Text":"PC","Children":[{"Text":"AMD Ryzen","Children":[{"Text":"Temperatures","Children":[
            {"Text":"CPU Package","Value":"62,5 °C","Type":"Temperature","SensorId":"/amdcpu/0/temperature/0"}
        ]}]}]})";
    const HardwareSnapshot commaSnapshot = parseLibreHardwareMonitor(QByteArray(comma));
    QVERIFY(commaSnapshot.parsed);
    QVERIFY(commaSnapshot.hasCpuTemp);
    QVERIFY(near(commaSnapshot.cpuTempC, 62.5));

    const char *thousands =
        R"({"Text":"PC","Children":[{"Text":"Board","Children":[{"Text":"Fans","Children":[
            {"Text":"Fan #1","Value":"1,234 RPM","Type":"Fan","SensorId":"/lpc/0/fan/0"}
        ]}]}]})";
    const HardwareSnapshot rpm = parseLibreHardwareMonitor(QByteArray(thousands));
    QCOMPARE(rpm.fans.size(), 1);
    QVERIFY(near(rpm.fans.at(0).rpm, 1234.0));

    const char *numeric =
        R"({"Text":"PC","Children":[{"Text":"NVIDIA GeForce RTX 5080","Children":[
            {"Text":"GPU Core","Value":71,"Type":"Temperature","SensorId":"/nvidiagpu/0/temperature/0"}
        ]}]})";
    const HardwareSnapshot number = parseLibreHardwareMonitor(QByteArray(numeric));
    QVERIFY(number.hasGpuTemp);
    QVERIFY(near(number.gpuTempC, 71.0));
}

void PcStatsTests::lhmMalformedIsRejected()
{
    QVERIFY(!parseLibreHardwareMonitor("{").parsed);
    QVERIFY(!parseLibreHardwareMonitor("").parsed);
    QVERIFY(!parseLibreHardwareMonitor("null").parsed);
    QVERIFY(!parseLibreHardwareMonitor("\"nope\"").parsed);

    const HardwareSnapshot empty = parseLibreHardwareMonitor("{}");
    QVERIFY(empty.parsed);
    QVERIFY(!empty.hasCpuTemp);
    QVERIFY(!empty.hasGpuTemp);
    QVERIFY(!empty.hasNet);
    QCOMPARE(hardwareToVariant(empty).value(QStringLiteral("cpuLine")).toString(),
             QStringLiteral("-- -- -- --"));
}

void PcStatsTests::frameGraphUsesFixedScale()
{
    // 0 at the bottom, 33.3 ms at the top. A hitch above that clips to the top.
    const FrameGraphScale tight = frameGraphScale({8.0, 8.1, 8.2});
    QVERIFY(near(tight.low, 0.0));
    QVERIFY(near(tight.high, 33.3));

    const FrameGraphScale spike = frameGraphScale({8.0, 8.2, 50.0});
    QVERIFY(near(spike.low, 0.0));
    QVERIFY(near(spike.high, 33.3));
    const double span = spike.high - spike.low;
    const double clipped = std::min(50.0, spike.high);
    QVERIFY(near((clipped - spike.low) / span, 1.0));
    QVERIFY((16.7 - spike.low) / span < 0.55);

    const FrameGraphScale empty = frameGraphScale({});
    QVERIFY(near(empty.low, 0.0));
    QVERIFY(near(empty.high, 33.3));

    const QVariantMap view = framesToVariant(parseFrameStats(
        R"({"process":"game.exe","frame_ms":[8.0,8.2,16.7]})"));
    QVERIFY(near(view.value(QStringLiteral("frameLo")).toDouble(), 0.0));
    QVERIFY(near(view.value(QStringLiteral("frameHi")).toDouble(), 33.3));
}

void PcStatsTests::driveRatesAndFanLabels()
{
    const char *json = R"({
      "Text": "PC",
      "Children": [
        {
          "Text": "Sabrent Rocket 4.0",
          "Children": [
            {"Text": "Temperature", "Value": "29 °C", "Type": "Temperature", "SensorId": "/nvme/1/temperature/0"},
            {"Text": "Read Rate", "Value": "1.5 GB/s", "Type": "Throughput", "SensorId": "/nvme/1/throughput/0"},
            {"Text": "Write Rate", "Value": "512 B/s", "Type": "Throughput", "SensorId": "/nvme/1/throughput/1"}
          ]
        },
        {
          "Text": "CT2000P3SSD8",
          "Children": [
            {"Text": "Temperature", "Value": "55 °C", "Type": "Temperature", "SensorId": "/nvme/2/temperature/0"},
            {"Text": "Read Rate", "Value": "12.3 MB/s", "Type": "Throughput", "SensorId": "/nvme/2/throughput/0"}
          ]
        },
        {
          "Text": "ITE IT8689E",
          "Children": [
            {"Text": "CPU Fan", "Value": "1100 RPM", "Type": "Fan", "SensorId": "/lpc/0/fan/0"},
            {"Text": "Fan #1", "Value": "800 RPM", "Type": "Fan", "SensorId": "/lpc/0/fan/1"},
            {"Text": "Fan #2", "Value": "0 RPM", "Type": "Fan", "SensorId": "/lpc/0/fan/2"},
            {"Text": "Pump", "Value": "2200 RPM", "Type": "Fan", "SensorId": "/lpc/0/fan/3"},
            {"Text": "GPU Fan 2", "Value": "0 RPM", "Type": "Fan", "SensorId": "/nvidiagpu/0/fan/1"}
          ]
        }
      ]
    })";
    const QVariantMap view = hardwareToVariant(parseLibreHardwareMonitor(QByteArray(json)));
    QCOMPARE(view.value(QStringLiteral("driveLines")).toStringList(),
             QStringList({QStringLiteral("SABRENT 29°  R 1.5 GB/s  W 512 B/s"),
                          QStringLiteral("CRUCIAL 55°  R 12.3 MB/s")}));
    QCOMPARE(view.value(QStringLiteral("fanLine")).toString(),
             QStringLiteral("CPU 1100  PUMP 2200  SYS1 800"));
}

void PcStatsTests::framesFixtureKeepsReportedFps()
{
    const QByteArray json = readFixture("frames.json");
    QVERIFY(!json.isEmpty());
    const FrameSnapshot snapshot = parseFrameStats(json);
    QVERIFY(snapshot.parsed);
    QCOMPARE(snapshot.process, QStringLiteral("eldenring.exe"));
    QCOMPARE(snapshot.frameMs.size(), 8);
    QVERIFY(near(snapshot.frameMs.first(), 8.1));
    QVERIFY(snapshot.hasFps);
    // The companion already averaged a longer window than the graph samples.
    QVERIFY(near(snapshot.fpsAvg, 116.4));
    QVERIFY(near(snapshot.fpsOnePctLow, 87.2));

    const QVariantMap view = framesToVariant(snapshot);
    QCOMPARE(view.value(QStringLiteral("state")).toString(), QStringLiteral("live"));
    QCOMPARE(view.value(QStringLiteral("fpsAvg")).toString(), QStringLiteral("116"));
    QCOMPARE(view.value(QStringLiteral("fpsLow")).toString(), QStringLiteral("87.2"));
}

void PcStatsTests::framesComputesAverageAndOnePercentLow()
{
    QStringList samples;
    for (int i = 0; i < 19; ++i)
        samples << QStringLiteral("10");
    samples << QStringLiteral("50");
    const QByteArray json = QStringLiteral(R"({"process":"game.exe","frame_ms":[%1]})")
                                .arg(samples.join(QLatin1Char(',')))
                                .toUtf8();

    const FrameSnapshot snapshot = parseFrameStats(json);
    QVERIFY(snapshot.parsed);
    QVERIFY(snapshot.hasFps);
    // Mean frame time is 12 ms. The slowest 1% is the single 50 ms frame.
    QVERIFY(near(snapshot.fpsAvg, 1000.0 / 12.0));
    QVERIFY(near(snapshot.fpsOnePctLow, 20.0));
    QCOMPARE(snapshot.frameMs.size(), 20);
}

void PcStatsTests::framesNoGameAndCompanionError()
{
    const FrameSnapshot idle = parseFrameStats(R"({"process":"","frame_ms":[]})");
    QVERIFY(idle.parsed);
    QVERIFY(!idle.hasFps);
    QVERIFY(idle.process.isEmpty());
    QCOMPARE(framesToVariant(idle).value(QStringLiteral("state")).toString(),
             QStringLiteral("nogame"));

    const FrameSnapshot waiting = parseFrameStats(R"({"process":"game.exe","frame_ms":[]})");
    QCOMPARE(framesToVariant(waiting).value(QStringLiteral("state")).toString(),
             QStringLiteral("waiting"));

    const FrameSnapshot broken = parseFrameStats(R"({"ok":false,"error":"presentmon not found"})");
    QVERIFY(broken.parsed);
    QVERIFY(!broken.hasFps);
    QCOMPARE(broken.error, QStringLiteral("presentmon not found"));
    QCOMPARE(framesToVariant(broken).value(QStringLiteral("state")).toString(),
             QStringLiteral("unavailable"));

    QVERIFY(!parseFrameStats("{").parsed);
    QVERIFY(!parseFrameStats("[]").parsed);
}

QStringList homeMenuNames(const QByteArray &config)
{
    QTemporaryDir dir;
    if (!dir.isValid())
        return {};
    QFile file(dir.path() + QStringLiteral("/config.json"));
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(config);
    file.close();

    AppCore core(QString::fromLatin1(PC_STATS_SOURCE_DIR), dir.path());
    QStringList names;
    QObject::connect(&core, &AppCore::modulesLoaded, &core, [&names](const QVariantList &rows) {
        for (const QVariant &row : rows)
            names << row.toMap().value(QStringLiteral("name")).toString();
    });
    core.scan_for_modules();
    return names;
}

void PcStatsTests::homeMenuPlacesPcStatsUnderPlex()
{
    // Plex and YouTube on, everything that sorts ahead of Plex left off.
    // That is the menu where Plex is the top row.
    const QStringList plexFirst = homeMenuNames(R"({
        "modules": {
            "com.240mp.local_files": {"enabled": false},
            "com.240mp.plex": {"enabled": true},
            "com.240mp.youtube": {"enabled": true}
        }
    })");
    QCOMPARE(plexFirst, QStringList({
        QStringLiteral("Plex"),
        QStringLiteral("PC Stats"),
        QStringLiteral("YouTube")
    }));

    // A row that already sits above Plex stays above it. PC Stats still
    // follows Plex, and YouTube stays last.
    const QStringList withLocal = homeMenuNames(R"({
        "modules": {
            "com.240mp.local_files": {"enabled": true},
            "com.240mp.plex": {"enabled": true},
            "com.240mp.youtube": {"enabled": true}
        }
    })");
    QCOMPARE(withLocal, QStringList({
        QStringLiteral("Local Files"),
        QStringLiteral("Plex"),
        QStringLiteral("PC Stats"),
        QStringLiteral("YouTube")
    }));
}

void PcStatsTests::sectionSwapPhaseFollowsElapsed()
{
    const int interval = 15 * 60 * 1000;
    QVERIFY(!pcstats::gpuOnTopForElapsed(0, interval, true));
    QVERIFY(!pcstats::gpuOnTopForElapsed(interval - 1, interval, true));
    QVERIFY(pcstats::gpuOnTopForElapsed(interval, interval, true));
    QVERIFY(pcstats::gpuOnTopForElapsed(qint64(interval) * 2 - 1, interval, true));
    QVERIFY(!pcstats::gpuOnTopForElapsed(qint64(interval) * 2, interval, true));
    QVERIFY(pcstats::gpuOnTopForElapsed(qint64(interval) * 3, interval, true));
    // Off is the CPU half, whatever the elapsed time says.
    QVERIFY(!pcstats::gpuOnTopForElapsed(qint64(interval) * 3, interval, false));
    QVERIFY(!pcstats::gpuOnTopForElapsed(interval, 0, true));
}

void PcStatsTests::sectionSwapClockSurvivesPause()
{
    const int interval = 15 * 60 * 1000;
    pcstats::SectionSwapClock clock;
    QVERIFY(!clock.running());
    QVERIFY(!clock.gpuOnTop(0, interval, true));
    QCOMPARE(clock.msUntilNext(0, interval, true), interval);

    clock.resume();
    clock.resume();
    // Fourteen minutes on screen, then leave. The tick is not stored until pause.
    QVERIFY(!clock.gpuOnTop(14 * 60 * 1000, interval, true));
    QCOMPARE(clock.msUntilNext(14 * 60 * 1000, interval, true), 60 * 1000);
    clock.pause(14 * 60 * 1000);
    QVERIFY(!clock.running());
    // Time away is not applied: a later read with no new tick stays at 14 minutes.
    QCOMPARE(clock.elapsed(0), qint64(14 * 60 * 1000));
    QVERIFY(!clock.gpuOnTop(0, interval, true));
    // A second pause must not add those 14 minutes again.
    clock.pause(14 * 60 * 1000);
    QCOMPARE(clock.elapsed(0), qint64(14 * 60 * 1000));

    // Back on the screen. One more minute finishes the first half.
    clock.resume();
    QVERIFY(clock.gpuOnTop(60 * 1000, interval, true));
    QVERIFY(!clock.gpuOnTop(60 * 1000, interval, false));
    clock.pause(60 * 1000);
    QVERIFY(clock.gpuOnTop(0, interval, true));
    QCOMPARE(clock.elapsed(0), qint64(15 * 60 * 1000));
    QCOMPARE(clock.msUntilNext(0, interval, true), interval);

    // Another full interval puts the CPU half back on top. Pause keeps that.
    clock.resume();
    clock.pause(interval);
    QVERIFY(!clock.gpuOnTop(0, interval, true));
    QCOMPARE(clock.elapsed(0), qint64(30 * 60 * 1000));
    // Disabled reports the CPU half and does not throw the elapsed time away.
    QVERIFY(!clock.gpuOnTop(0, interval, false));
    QCOMPARE(clock.elapsed(0), qint64(30 * 60 * 1000));
    QCOMPARE(clock.msUntilNext(0, interval, false), 0);
}

void PcStatsTests::sectionSwapConfigDefaults()
{
    QCOMPARE(pcstats::leadingPositiveInt(QStringLiteral("2 SEC"), 9), 2);
    QCOMPARE(pcstats::leadingPositiveInt(QStringLiteral("15 MIN"), 9), 15);
    QCOMPARE(pcstats::leadingPositiveInt(QString(), 9), 9);
    QCOMPARE(pcstats::leadingPositiveInt(QStringLiteral("0 MIN"), 9), 9);
    QCOMPARE(pcstats::leadingPositiveInt(QStringLiteral("nope"), 9), 9);

    const QJsonObject empty;
    QVERIFY(pcstats::sectionSwapEnabledFromConfig(empty));
    QCOMPARE(pcstats::swapIntervalMsFromConfig(empty), 15 * 60 * 1000);

    // A config written before these keys existed.
    QJsonObject legacy;
    legacy.insert(QStringLiteral("poll_interval"), QStringLiteral("2 SEC"));
    legacy.insert(QStringLiteral("drift_interval"), QStringLiteral("3 MIN"));
    QVERIFY(pcstats::sectionSwapEnabledFromConfig(legacy));
    QCOMPARE(pcstats::swapIntervalMsFromConfig(legacy), 15 * 60 * 1000);

    QJsonObject off;
    off.insert(QStringLiteral("section_swap"), false);
    off.insert(QStringLiteral("swap_interval"), QStringLiteral("30 MIN"));
    QVERIFY(!pcstats::sectionSwapEnabledFromConfig(off));
    QCOMPARE(pcstats::swapIntervalMsFromConfig(off), 30 * 60 * 1000);

    QJsonObject onString;
    onString.insert(QStringLiteral("section_swap"), QStringLiteral("ON"));
    onString.insert(QStringLiteral("swap_interval"), QStringLiteral("5 MIN"));
    QVERIFY(pcstats::sectionSwapEnabledFromConfig(onString));
    QCOMPARE(pcstats::swapIntervalMsFromConfig(onString), 5 * 60 * 1000);

    QJsonObject offString;
    offString.insert(QStringLiteral("section_swap"), QStringLiteral("off"));
    QVERIFY(!pcstats::sectionSwapEnabledFromConfig(offString));

    QJsonObject blank;
    blank.insert(QStringLiteral("section_swap"), QString());
    QVERIFY(pcstats::sectionSwapEnabledFromConfig(blank));

    QJsonObject garbage;
    garbage.insert(QStringLiteral("swap_interval"), QStringLiteral("nope"));
    QCOMPARE(pcstats::swapIntervalMsFromConfig(garbage), 15 * 60 * 1000);
    garbage.insert(QStringLiteral("swap_interval"), QStringLiteral("999 MIN"));
    QCOMPARE(pcstats::swapIntervalMsFromConfig(garbage), 120 * 60 * 1000);
    garbage.insert(QStringLiteral("swap_interval"), QStringLiteral("10 MIN"));
    QCOMPARE(pcstats::swapIntervalMsFromConfig(garbage), 10 * 60 * 1000);
}

void PcStatsTests::backendSwapKeysDefaultWhenAbsent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/config.json");

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"modules":{"com.240mp.pc_stats":{"host":"10.0.0.44","drift_interval":"3 MIN","poll_interval":"1 SEC"}}})");
    file.close();

    PcStatsBackend backend(dir.path());
    QVERIFY(backend.sectionSwapEnabled());
    QCOMPARE(backend.swapIntervalMs(), 15 * 60 * 1000);
    QCOMPARE(backend.driftIntervalMs(), 3 * 60 * 1000);
    QVERIFY(!backend.gpuOnTop());
    QVERIFY(backend.hardware().isEmpty());
    QVERIFY(!backend.connecting());

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(R"({"modules":{"com.240mp.pc_stats":{"section_swap":false,"swap_interval":"10 MIN","drift_interval":"5 MIN"}}})");
    file.close();
    backend.onSettingChanged(QStringLiteral("com.240mp.pc_stats"),
                             QStringLiteral("section_swap"), false);
    QVERIFY(!backend.sectionSwapEnabled());
    QCOMPARE(backend.swapIntervalMs(), 10 * 60 * 1000);
    QCOMPARE(backend.driftIntervalMs(), 5 * 60 * 1000);
    QVERIFY(!backend.gpuOnTop());
    QVERIFY(backend.hardware().isEmpty());
    QVERIFY(!backend.connecting());
    QVERIFY(!backend.offline());
}

QTEST_GUILESS_MAIN(PcStatsTests)
#include "tst_pc_stats.moc"
