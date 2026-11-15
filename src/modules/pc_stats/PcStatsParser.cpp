#include "PcStatsParser.h"

#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace pcstats {
namespace {

enum class Kind { Unknown, Cpu, Gpu, Ram, Drive, Net };

struct Measurement {
    bool ok = false;
    double value = 0;
    QString unit;
};

QString textOf(const QJsonObject &node)
{
    QString text = node.value(QStringLiteral("Text")).toString();
    if (text.isEmpty())
        text = node.value(QStringLiteral("text")).toString();
    if (text.isEmpty())
        text = node.value(QStringLiteral("Name")).toString();
    return text.trimmed();
}

QString typeOf(const QJsonObject &node)
{
    QString type = node.value(QStringLiteral("Type")).toString();
    if (type.isEmpty())
        type = node.value(QStringLiteral("type")).toString();
    return type.trimmed();
}

QString sensorIdOf(const QJsonObject &node)
{
    QString id = node.value(QStringLiteral("SensorId")).toString();
    if (id.isEmpty())
        id = node.value(QStringLiteral("sensorId")).toString();
    return id;
}

QJsonArray childrenOf(const QJsonObject &node)
{
    const QJsonValue children = node.value(QStringLiteral("Children"));
    if (children.isArray())
        return children.toArray();
    const QJsonValue lower = node.value(QStringLiteral("children"));
    if (lower.isArray())
        return lower.toArray();
    return {};
}

bool isCategoryName(const QString &text)
{
    const QString n = text.trimmed().toLower();
    return n == QLatin1String("temperature") || n == QLatin1String("temperatures")
        || n == QLatin1String("load") || n == QLatin1String("loads")
        || n == QLatin1String("clock") || n == QLatin1String("clocks")
        || n == QLatin1String("power") || n == QLatin1String("powers")
        || n == QLatin1String("fan") || n == QLatin1String("fans")
        || n == QLatin1String("data") || n == QLatin1String("small data")
        || n == QLatin1String("voltage") || n == QLatin1String("voltages")
        || n == QLatin1String("current") || n == QLatin1String("currents")
        || n == QLatin1String("factor") || n == QLatin1String("factors")
        || n == QLatin1String("control") || n == QLatin1String("controls")
        || n == QLatin1String("level") || n == QLatin1String("levels")
        || n == QLatin1String("throughput") || n == QLatin1String("throughputs")
        || n == QLatin1String("frequency") || n == QLatin1String("frequencies")
        || n == QLatin1String("flow") || n == QLatin1String("flows")
        || n == QLatin1String("noise") || n == QLatin1String("energy");
}

bool isRootLabel(const QString &text)
{
    const QString n = text.trimmed().toLower();
    return n.isEmpty() || n == QLatin1String("sensor") || n == QLatin1String("sensors")
        || n == QLatin1String("computer");
}

// LHM's web server formats with the invariant culture ("62.5 °C"), but a
// locale-formatted tree ("62,5 °C", "1,234 RPM") should still read.
Measurement parseMeasurement(const QString &raw)
{
    Measurement out;
    QString s = raw.trimmed();
    s.replace(QChar(0x00A0), QLatin1Char(' '));
    s.replace(QChar(0x202F), QLatin1Char(' '));
    if (s.isEmpty() || s == QLatin1String("-") || s.compare(QLatin1String("n/a"), Qt::CaseInsensitive) == 0)
        return out;

    static const QRegularExpression re(QStringLiteral(
        "^\\s*([+-]?(?:\\d[\\d\\s.,]*))\\s*(.*)$"));
    const QRegularExpressionMatch match = re.match(s);
    if (!match.hasMatch())
        return out;

    QString number = match.captured(1);
    number.remove(QLatin1Char(' '));
    const int lastComma = number.lastIndexOf(QLatin1Char(','));
    const int lastDot = number.lastIndexOf(QLatin1Char('.'));
    if (lastComma >= 0 && lastDot >= 0) {
        if (lastComma > lastDot) {
            number.remove(QLatin1Char('.'));
            number.replace(QLatin1Char(','), QLatin1Char('.'));
        } else {
            number.remove(QLatin1Char(','));
        }
    } else if (lastComma >= 0) {
        static const QRegularExpression thousands(QStringLiteral("^\\d{1,3}(?:,\\d{3})+$"));
        if (thousands.match(number).hasMatch())
            number.remove(QLatin1Char(','));
        else
            number.replace(QLatin1Char(','), QLatin1Char('.'));
    }

    bool ok = false;
    const double value = number.toDouble(&ok);
    if (!ok || !std::isfinite(value))
        return out;

    QString unit = match.captured(2).trimmed().toLower();
    unit.remove(QChar(0x00B0));
    unit.remove(QLatin1Char(' '));

    out.ok = true;
    out.value = value;
    out.unit = unit;
    return out;
}

QString defaultUnitForType(const QString &type)
{
    const QString t = type.toLower();
    if (t == QLatin1String("temperature")) return QStringLiteral("c");
    if (t == QLatin1String("load") || t == QLatin1String("level") || t == QLatin1String("control"))
        return QStringLiteral("%");
    if (t == QLatin1String("clock") || t == QLatin1String("frequency")) return QStringLiteral("mhz");
    if (t == QLatin1String("power")) return QStringLiteral("w");
    if (t == QLatin1String("fan")) return QStringLiteral("rpm");
    if (t == QLatin1String("smalldata")) return QStringLiteral("mb");
    if (t == QLatin1String("data")) return QStringLiteral("gb");
    if (t == QLatin1String("throughput")) return QStringLiteral("b/s");
    return {};
}

enum class UnitKind { Other, TempC, Percent, MHz, Watts, Rpm, Megabytes, MegabytesPerSec };

struct Normalized {
    bool ok = false;
    UnitKind kind = UnitKind::Other;
    double value = 0;
};

Normalized normalize(const Measurement &raw, const QString &type)
{
    Normalized out;
    if (!raw.ok)
        return out;
    QString unit = raw.unit;
    if (unit.isEmpty())
        unit = defaultUnitForType(type);
    if (unit.isEmpty())
        return out;

    out.ok = true;
    out.value = raw.value;

    if (unit == QLatin1String("c") || unit == QLatin1String("celsius")) {
        out.kind = UnitKind::TempC;
    } else if (unit == QLatin1String("f") || unit == QLatin1String("fahrenheit")) {
        out.kind = UnitKind::TempC;
        out.value = (raw.value - 32.0) * 5.0 / 9.0;
    } else if (unit == QLatin1String("%") || unit == QLatin1String("percent")) {
        out.kind = UnitKind::Percent;
    } else if (unit == QLatin1String("mhz")) {
        out.kind = UnitKind::MHz;
    } else if (unit == QLatin1String("ghz")) {
        out.kind = UnitKind::MHz;
        out.value = raw.value * 1000.0;
    } else if (unit == QLatin1String("khz")) {
        out.kind = UnitKind::MHz;
        out.value = raw.value / 1000.0;
    } else if (unit == QLatin1String("hz")) {
        out.kind = UnitKind::MHz;
        out.value = raw.value / 1.0e6;
    } else if (unit == QLatin1String("w")) {
        out.kind = UnitKind::Watts;
    } else if (unit == QLatin1String("mw")) {
        out.kind = UnitKind::Watts;
        out.value = raw.value / 1000.0;
    } else if (unit == QLatin1String("rpm")) {
        out.kind = UnitKind::Rpm;
    } else if (unit == QLatin1String("mb") || unit == QLatin1String("mib")) {
        out.kind = UnitKind::Megabytes;
    } else if (unit == QLatin1String("gb") || unit == QLatin1String("gib")) {
        out.kind = UnitKind::Megabytes;
        out.value = raw.value * 1024.0;
    } else if (unit == QLatin1String("kb") || unit == QLatin1String("kib")) {
        out.kind = UnitKind::Megabytes;
        out.value = raw.value / 1024.0;
    } else if (unit == QLatin1String("b") || unit == QLatin1String("byte") || unit == QLatin1String("bytes")) {
        out.kind = UnitKind::Megabytes;
        out.value = raw.value / (1024.0 * 1024.0);
    } else if (unit.endsWith(QLatin1String("/s"))) {
        out.kind = UnitKind::MegabytesPerSec;
        const QString rate = unit.left(unit.size() - 2);
        if (rate == QLatin1String("gb") || rate == QLatin1String("gib"))
            out.value = raw.value * 1024.0;
        else if (rate == QLatin1String("mb") || rate == QLatin1String("mib"))
            out.value = raw.value;
        else if (rate == QLatin1String("kb") || rate == QLatin1String("kib"))
            out.value = raw.value / 1024.0;
        else if (rate == QLatin1String("b") || rate.isEmpty())
            out.value = raw.value / (1024.0 * 1024.0);
        else
            out.ok = false;
    } else {
        out.ok = false;
    }
    return out;
}

Kind kindFromSensorId(const QString &sensorId)
{
    const QString id = sensorId.toLower();
    if (id.contains(QLatin1String("/intelcpu/")) || id.contains(QLatin1String("/amdcpu/")))
        return Kind::Cpu;
    if (id.contains(QLatin1String("/nvidiagpu/")) || id.contains(QLatin1String("/amdgpus/"))
            || id.contains(QLatin1String("/atigpu/")) || id.contains(QLatin1String("/intelgpu/")))
        return Kind::Gpu;
    if (id.contains(QLatin1String("/ram/")))
        return Kind::Ram;
    if (id.contains(QLatin1String("/nvme/")) || id.contains(QLatin1String("/hdd/"))
            || id.contains(QLatin1String("/ssd/")) || id.contains(QLatin1String("/storage/")))
        return Kind::Drive;
    if (id.contains(QLatin1String("/nic/")))
        return Kind::Net;
    return Kind::Unknown;
}

Kind kindFromHardwareName(const QString &hardware)
{
    const QString n = hardware.toLower();
    if (n.contains(QLatin1String("geforce")) || n.contains(QLatin1String("nvidia"))
            || n.contains(QLatin1String("radeon")) || n.contains(QLatin1String("rtx"))
            || n.contains(QLatin1String("gtx")) || n.contains(QLatin1String("arc graphics"))
            || n.contains(QLatin1String("gpu")))
        return Kind::Gpu;
    if (n.contains(QLatin1String("ryzen")) || n.contains(QLatin1String("core i"))
            || n.contains(QLatin1String("cpu")) || n.contains(QLatin1String("intel"))
            || n.contains(QLatin1String("amd")))
        return Kind::Cpu;
    if (n.contains(QLatin1String("memory")) || n == QLatin1String("ram"))
        return Kind::Ram;
    if (n.contains(QLatin1String("nvme")) || n.contains(QLatin1String("ssd"))
            || n.contains(QLatin1String("hdd")) || n.contains(QLatin1String("samsung"))
            || n.contains(QLatin1String("seagate")) || n.contains(QLatin1String("crucial")))
        return Kind::Drive;
    if (n.contains(QLatin1String("ethernet")) || n.contains(QLatin1String("wi-fi"))
            || n.contains(QLatin1String("wifi")) || n.contains(QLatin1String("tailscale"))
            || n.contains(QLatin1String("network")))
        return Kind::Net;
    return Kind::Unknown;
}

bool nameHas(const QString &name, const char *needle)
{
    return name.contains(QLatin1String(needle), Qt::CaseInsensitive);
}

void takeBetter(int score, double value, const QString &hardware,
                int *bestScore, double *bestValue, bool *has, QString *nameKeep, bool keepName)
{
    if (score < *bestScore)
        return;
    *bestScore = score;
    *bestValue = value;
    *has = true;
    if (keepName && nameKeep)
        *nameKeep = hardware;
}

bool isCoreClockName(const QString &name)
{
    if (nameHas(name, "bus"))
        return false;
    if (nameHas(name, "effective"))
        return false;
    static const QRegularExpression core(QStringLiteral("core\\s*#?\\s*\\d"),
                                         QRegularExpression::CaseInsensitiveOption);
    if (core.match(name).hasMatch())
        return true;
    return nameHas(name, "p-core") || nameHas(name, "e-core");
}

struct NetAdapter {
    QString name;
    bool hasDown = false;
    bool hasUp = false;
    double down = 0;
    double up = 0;
};

QString gbText(double megabytes)
{
    const double g = megabytes / 1024.0;
    if (std::abs(g - std::round(g)) < 0.05)
        return QString::number(qRound(g));
    return QString::number(g, 'f', 1);
}

QString clockText(double mhz)
{
    if (mhz >= 1000.0)
        return QString::number(mhz / 1000.0, 'f', 1) + QStringLiteral("G");
    return QString::number(qRound(mhz)) + QStringLiteral("M");
}

QString rateText(double megabytesPerSec)
{
    if (megabytesPerSec >= 10.0)
        return QString::number(qRound(megabytesPerSec)) + QStringLiteral("M");
    if (megabytesPerSec >= 1.0)
        return QString::number(megabytesPerSec, 'f', 1) + QStringLiteral("M");
    const double kb = megabytesPerSec * 1024.0;
    if (kb >= 10.0)
        return QString::number(qRound(kb)) + QStringLiteral("K");
    return QString::number(kb, 'f', 1) + QStringLiteral("K");
}

QString tempText(bool has, double c)
{
    if (!has)
        return QStringLiteral("--");
    return QString::number(qRound(c)) + QString(QChar(0x00B0));
}

QString pctText(bool has, double pct)
{
    if (!has)
        return QStringLiteral("--");
    return QString::number(qRound(pct)) + QStringLiteral("%");
}

QString wattText(bool has, double watts)
{
    if (!has)
        return QStringLiteral("--");
    return QString::number(qRound(watts)) + QStringLiteral("W");
}

QString joinPresent(const QStringList &parts)
{
    QStringList kept;
    for (const QString &part : parts) {
        if (!part.isEmpty())
            kept.append(part);
    }
    if (kept.isEmpty())
        return QStringLiteral("--");
    return kept.join(QStringLiteral(" "));
}

QString shortToken(const QString &text, int maxChars = 4)
{
    QString token = text.section(QLatin1Char(' '), 0, 0);
    token.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9]")));
    if (token.size() > maxChars)
        token = token.left(maxChars);
    return token.toUpper();
}

// "AMD Ryzen 7 9800X3D" -> "9800X3D". The full marketing name does not fit
// a 4:3 CRT row next to temp, load, clock and power.
QString shortCpuName(const QString &name)
{
    static const QRegularExpression model(
        QStringLiteral("\\b(\\d{4}[A-Za-z0-9]*|i[3579]-\\d{4,5}[A-Za-z0-9]*)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    QString last;
    auto it = model.globalMatch(name);
    while (it.hasNext())
        last = it.next().captured(1);
    if (!last.isEmpty())
        return last.toUpper();
    return shortToken(name, 8);
}

// "NVIDIA GeForce RTX 5080" -> "RTX 5080".
QString shortGpuName(const QString &name)
{
    static const QRegularExpression model(
        QStringLiteral("((?:rtx|gtx|rx)\\s*\\d{3,4}(?:\\s*(?:ti|super|xtx|xt))?)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = model.match(name);
    if (match.hasMatch()) {
        QString text = match.captured(1);
        text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
        return text.trimmed().toUpper();
    }
    return shortToken(name, 8);
}

// "GPU Fan 1" -> GPU / 1, "CPU Fan" -> CPU, "Fan #2" -> SYS / 2.
// Numbered motherboard headers (Fan #1) are case fans, not the chip name.
QString fanFamily(const QString &sensor)
{
    if (nameHas(sensor, "pump") || nameHas(sensor, "aio"))
        return QStringLiteral("PUMP");
    if (nameHas(sensor, "gpu"))
        return QStringLiteral("GPU");
    if (nameHas(sensor, "cpu"))
        return QStringLiteral("CPU");
    return QStringLiteral("SYS");
}

int fanNumber(const QString &sensor)
{
    static const QRegularExpression hashNum(QStringLiteral("#\\s*(\\d+)"));
    const auto hashed = hashNum.match(sensor);
    if (hashed.hasMatch())
        return hashed.captured(1).toInt();
    static const QRegularExpression tail(QStringLiteral("(\\d+)\\s*$"));
    const auto numbered = tail.match(sensor.trimmed());
    if (numbered.hasMatch())
        return numbered.captured(1).toInt();
    return -1;
}

int fanRank(const QString &family)
{
    if (family == QLatin1String("CPU")) return 0;
    if (family == QLatin1String("PUMP")) return 1;
    if (family == QLatin1String("GPU")) return 2;
    return 3;
}

QString driveLabel(const QString &name)
{
    const QString lower = name.toLower();
    if (lower.contains(QLatin1String("sabrent")))
        return QStringLiteral("SABRENT");
    if (lower.contains(QLatin1String("crucial"))
            || QRegularExpression(QStringLiteral("\\bct\\d")).match(lower).hasMatch())
        return QStringLiteral("CRUCIAL");
    if (lower.contains(QLatin1String("samsung")))
        return QStringLiteral("SAMSUNG");
    if (lower.contains(QLatin1String("seagate")))
        return QStringLiteral("SEAGATE");
    if (lower.contains(QLatin1String("kingston")))
        return QStringLiteral("KINGSTON");
    if (lower.contains(QLatin1String("western digital")) || lower.contains(QLatin1String("wdc"))
            || lower.contains(QLatin1String("wd ")))
        return QStringLiteral("WD");
    return shortToken(name, 8);
}

// megabytes/s -> B/s, KB/s, MB/s, or GB/s. One decimal, trailing ".0" dropped.
QString scaledRate(double megabytesPerSec)
{
    auto trim = [](double value) {
        const int digits = std::abs(value) >= 10.0 ? 1 : 1;
        QString text = QString::number(value, 'f', digits);
        if (text.endsWith(QLatin1String(".0")))
            text.chop(2);
        return text;
    };
    const double absMb = std::abs(megabytesPerSec);
    if (absMb >= 1024.0)
        return trim(megabytesPerSec / 1024.0) + QStringLiteral(" GB/s");
    if (absMb >= 1.0)
        return trim(megabytesPerSec) + QStringLiteral(" MB/s");
    const double kb = megabytesPerSec * 1024.0;
    if (std::abs(kb) >= 1.0)
        return trim(kb) + QStringLiteral(" KB/s");
    const double bytes = megabytesPerSec * 1024.0 * 1024.0;
    return QString::number(qRound(bytes)) + QStringLiteral(" B/s");
}

double meanOf(const QList<double> &values)
{
    if (values.isEmpty())
        return 0;
    double sum = 0;
    for (double v : values)
        sum += v;
    return sum / double(values.size());
}

void considerFps(FrameSnapshot *snapshot)
{
    QList<double> samples;
    samples.reserve(snapshot->frameMs.size());
    for (double ms : snapshot->frameMs) {
        if (ms > 0.0 && ms < 5000.0)
            samples.append(ms);
    }
    snapshot->frameMs = samples;
    if (samples.isEmpty())
        return;

    const double avgMs = meanOf(samples);
    if (avgMs <= 0.0)
        return;

    QList<double> slowest = samples;
    std::sort(slowest.begin(), slowest.end(), std::greater<double>());
    const int worstCount = qMax(1, qRound(samples.size() * 0.01));
    double worstSum = 0;
    for (int i = 0; i < worstCount; ++i)
        worstSum += slowest.at(i);
    const double worstMs = worstSum / double(worstCount);
    if (worstMs <= 0.0)
        return;

    snapshot->hasFps = true;
    snapshot->fpsAvg = 1000.0 / avgMs;
    snapshot->fpsOnePctLow = 1000.0 / worstMs;
}

struct Walker {
    HardwareSnapshot *snap = nullptr;

    int cpuTempScore = -1;
    int cpuLoadScore = -1;
    int cpuPowerScore = -1;
    int gpuTempScore = -1;
    int gpuHotScore = -1;
    int gpuLoadScore = -1;
    int gpuClockScore = -1;
    int gpuMemClockScore = -1;
    int gpuPowerScore = -1;
    int vramUsedScore = -1;
    int vramTotalScore = -1;
    int ramUsedScore = -1;
    int ramAvailScore = -1;
    int ramTotalScore = -1;
    int ramLoadScore = -1;
    bool cpuNameSet = false;
    bool gpuNameSet = false;

    QList<double> cpuCoreClocks;
    double ramAvailableMb = 0;
    bool hasRamAvailable = false;
    QHash<QString, int> driveIndex;
    QHash<QString, NetAdapter> nets;

    void rememberDevice(Kind kind, const QString &hardware)
    {
        if (kind == Kind::Cpu && !cpuNameSet && !hardware.isEmpty()) {
            snap->cpuName = hardware;
            cpuNameSet = true;
        } else if (kind == Kind::Gpu && !gpuNameSet && !hardware.isEmpty()) {
            snap->gpuName = hardware;
            gpuNameSet = true;
        }
    }

    int ensureDrive(const QString &hardware)
    {
        const QString key = hardware.toLower();
        const auto it = driveIndex.constFind(key);
        if (it != driveIndex.constEnd())
            return it.value();
        DriveReading drive;
        drive.name = hardware;
        const int index = snap->drives.size();
        driveIndex.insert(key, index);
        snap->drives.append(drive);
        return index;
    }

    void addDriveTemp(const QString &hardware, double tempC)
    {
        DriveReading &drive = snap->drives[ensureDrive(hardware)];
        // The first temperature on the drive is the composite. A later
        // sensor (a single NAND die, for example) must not replace it.
        if (drive.hasTemp)
            return;
        drive.hasTemp = true;
        drive.tempC = tempC;
    }

    void addDriveRate(const QString &hardware, bool isRead, double megabytesPerSec)
    {
        DriveReading &drive = snap->drives[ensureDrive(hardware)];
        if (isRead) {
            if (!drive.hasRead || megabytesPerSec > drive.readMBps) {
                drive.hasRead = true;
                drive.readMBps = megabytesPerSec;
            }
        } else if (!drive.hasWrite || megabytesPerSec > drive.writeMBps) {
            drive.hasWrite = true;
            drive.writeMBps = megabytesPerSec;
        }
    }

    void addNet(const QString &hardware, const QString &sensor, double megabytesPerSec)
    {
        NetAdapter &adapter = nets[hardware];
        if (adapter.name.isEmpty())
            adapter.name = hardware;
        const bool upload = nameHas(sensor, "upload") || nameHas(sensor, "sent") || nameHas(sensor, "tx");
        const bool download = nameHas(sensor, "download") || nameHas(sensor, "received") || nameHas(sensor, "rx");
        if (upload && !download) {
            adapter.hasUp = true;
            adapter.up = std::max(adapter.up, megabytesPerSec);
        } else if (download) {
            adapter.hasDown = true;
            adapter.down = std::max(adapter.down, megabytesPerSec);
        }
    }

    void finish()
    {
        if (!cpuCoreClocks.isEmpty()) {
            snap->hasCpuClock = true;
            snap->cpuClockMHz = meanOf(cpuCoreClocks);
        }
        if (!snap->hasRamTotal && snap->hasRamUsed && hasRamAvailable) {
            snap->ramTotalMb = snap->ramUsedMb + ramAvailableMb;
            snap->hasRamTotal = true;
        }

        const NetAdapter *busiest = nullptr;
        double busiestRate = -1;
        for (auto it = nets.constBegin(); it != nets.constEnd(); ++it) {
            if (!it->hasDown && !it->hasUp)
                continue;
            const double rate = it->down + it->up;
            if (!busiest || rate > busiestRate) {
                busiest = &(*it);
                busiestRate = rate;
            }
        }
        if (busiest) {
            snap->hasNet = true;
            snap->netName = busiest->name;
            snap->netDownMBps = busiest->down;
            snap->netUpMBps = busiest->up;
        }
    }

    void onSensor(Kind kind, const QString &hardware, const QString &name,
                  const QString &type, const Normalized &reading)
    {
        if (!reading.ok)
            return;
        const QString typeKey = type.toLower();
        rememberDevice(kind, hardware);

        if (typeKey == QLatin1String("fan") && reading.kind == UnitKind::Rpm) {
            FanReading fan;
            fan.hardware = hardware;
            fan.name = name;
            fan.rpm = reading.value;
            snap->fans.append(fan);
        }

        if (kind == Kind::Cpu) {
            if (typeKey == QLatin1String("temperature") && reading.kind == UnitKind::TempC) {
                int score = 10;
                if (nameHas(name, "package")) score = 100;
                else if (nameHas(name, "tctl") || nameHas(name, "tdie")) score = 90;
                else if (name.compare(QLatin1String("cpu"), Qt::CaseInsensitive) == 0) score = 80;
                else if (nameHas(name, "ccd")) score = 70;
                takeBetter(score, reading.value, hardware, &cpuTempScore, &snap->cpuTempC,
                           &snap->hasCpuTemp, &snap->cpuName, true);
                cpuNameSet = true;
            } else if (typeKey == QLatin1String("load") && reading.kind == UnitKind::Percent) {
                int score = 10;
                if (nameHas(name, "total")) score = 100;
                else if (name.compare(QLatin1String("cpu"), Qt::CaseInsensitive) == 0) score = 80;
                takeBetter(score, reading.value, hardware, &cpuLoadScore, &snap->cpuLoadPct,
                           &snap->hasCpuLoad, nullptr, false);
            } else if ((typeKey == QLatin1String("clock") || typeKey == QLatin1String("frequency"))
                       && reading.kind == UnitKind::MHz && reading.value > 0.0 && isCoreClockName(name)) {
                cpuCoreClocks.append(reading.value);
            } else if (typeKey == QLatin1String("power") && reading.kind == UnitKind::Watts) {
                int score = 40;
                if (nameHas(name, "package")) score = 100;
                else if (nameHas(name, "cores")) score = 60;
                takeBetter(score, reading.value, hardware, &cpuPowerScore, &snap->cpuPowerW,
                           &snap->hasCpuPower, nullptr, false);
            }
        } else if (kind == Kind::Gpu) {
            if (typeKey == QLatin1String("temperature") && reading.kind == UnitKind::TempC) {
                if (nameHas(name, "hot spot") || nameHas(name, "hotspot")) {
                    takeBetter(100, reading.value, hardware, &gpuHotScore, &snap->gpuHotspotC,
                               &snap->hasGpuHotspot, &snap->gpuName, true);
                    gpuNameSet = true;
                } else if (!nameHas(name, "memory") && !nameHas(name, "junction")) {
                    int score = 50;
                    if (nameHas(name, "core")) score = 100;
                    else if (name.compare(QLatin1String("gpu"), Qt::CaseInsensitive) == 0) score = 80;
                    takeBetter(score, reading.value, hardware, &gpuTempScore, &snap->gpuTempC,
                               &snap->hasGpuTemp, &snap->gpuName, true);
                    gpuNameSet = true;
                }
            } else if (typeKey == QLatin1String("load") && reading.kind == UnitKind::Percent
                       && !nameHas(name, "memory") && !nameHas(name, "controller")
                       && !nameHas(name, "video") && !nameHas(name, "bus")) {
                int score = 40;
                if (nameHas(name, "core")) score = 100;
                else if (name.compare(QLatin1String("gpu"), Qt::CaseInsensitive) == 0) score = 80;
                takeBetter(score, reading.value, hardware, &gpuLoadScore, &snap->gpuLoadPct,
                           &snap->hasGpuLoad, nullptr, false);
            } else if ((typeKey == QLatin1String("clock") || typeKey == QLatin1String("frequency"))
                       && reading.kind == UnitKind::MHz && reading.value > 0.0) {
                if (nameHas(name, "memory")) {
                    int score = nameHas(name, "core") ? 40 : 80;
                    takeBetter(score, reading.value, hardware, &gpuMemClockScore, &snap->gpuMemClockMHz,
                               &snap->hasGpuMemClock, nullptr, false);
                } else {
                    int score = 40;
                    if (nameHas(name, "core")) score = 100;
                    else if (nameHas(name, "shader")) score = 60;
                    takeBetter(score, reading.value, hardware, &gpuClockScore, &snap->gpuClockMHz,
                               &snap->hasGpuClock, nullptr, false);
                }
            } else if (typeKey == QLatin1String("power") && reading.kind == UnitKind::Watts) {
                int score = 40;
                if (nameHas(name, "package")) score = 100;
                else if (nameHas(name, "board")) score = 90;
                else if (nameHas(name, "gpu power") || name.compare(QLatin1String("gpu"), Qt::CaseInsensitive) == 0)
                    score = 80;
                takeBetter(score, reading.value, hardware, &gpuPowerScore, &snap->gpuPowerW,
                           &snap->hasGpuPower, nullptr, false);
            } else if ((typeKey == QLatin1String("data") || typeKey == QLatin1String("smalldata"))
                       && reading.kind == UnitKind::Megabytes) {
                if (nameHas(name, "used")) {
                    int score = 40;
                    if (nameHas(name, "gpu memory used")) score = 100;
                    else if (nameHas(name, "memory used")) score = 80;
                    else if (nameHas(name, "dedicated")) score = 60;
                    takeBetter(score, reading.value, hardware, &vramUsedScore, &snap->vramUsedMb,
                               &snap->hasVramUsed, nullptr, false);
                } else if (nameHas(name, "total")) {
                    int score = 40;
                    if (nameHas(name, "gpu memory total")) score = 100;
                    else if (nameHas(name, "memory total")) score = 80;
                    else if (nameHas(name, "dedicated")) score = 60;
                    takeBetter(score, reading.value, hardware, &vramTotalScore, &snap->vramTotalMb,
                               &snap->hasVramTotal, nullptr, false);
                }
            }
        } else if (kind == Kind::Ram) {
            if (typeKey == QLatin1String("load") && reading.kind == UnitKind::Percent) {
                int score = nameHas(name, "memory") ? 100 : 50;
                takeBetter(score, reading.value, hardware, &ramLoadScore, &snap->ramLoadPct,
                           &snap->hasRamLoad, nullptr, false);
            } else if ((typeKey == QLatin1String("data") || typeKey == QLatin1String("smalldata"))
                       && reading.kind == UnitKind::Megabytes) {
                if (nameHas(name, "used")) {
                    takeBetter(100, reading.value, hardware, &ramUsedScore, &snap->ramUsedMb,
                               &snap->hasRamUsed, nullptr, false);
                } else if (nameHas(name, "available") || nameHas(name, "free")) {
                    takeBetter(100, reading.value, hardware, &ramAvailScore, &ramAvailableMb,
                               &hasRamAvailable, nullptr, false);
                } else if (nameHas(name, "total")) {
                    takeBetter(100, reading.value, hardware, &ramTotalScore, &snap->ramTotalMb,
                               &snap->hasRamTotal, nullptr, false);
                }
            }
        } else if (kind == Kind::Drive) {
            if (typeKey == QLatin1String("temperature") && reading.kind == UnitKind::TempC)
                addDriveTemp(hardware, reading.value);
            else if (typeKey == QLatin1String("throughput") && reading.kind == UnitKind::MegabytesPerSec) {
                if (nameHas(name, "read"))
                    addDriveRate(hardware, true, reading.value);
                else if (nameHas(name, "write"))
                    addDriveRate(hardware, false, reading.value);
            }
        } else if (kind == Kind::Net) {
            if (typeKey == QLatin1String("throughput") && reading.kind == UnitKind::MegabytesPerSec)
                addNet(hardware, name, reading.value);
        }
    }
};

bool looksLikeHardwareContainer(const QJsonArray &children)
{
    bool hardwareChild = false;
    bool categoryChild = false;
    for (const QJsonValue &value : children) {
        if (!value.isObject())
            continue;
        const QJsonObject child = value.toObject();
        if (!typeOf(child).isEmpty())
            continue;
        const QString name = textOf(child);
        if (isCategoryName(name))
            categoryChild = true;
        else if (!isRootLabel(name))
            hardwareChild = true;
    }
    return hardwareChild && !categoryChild;
}

void walk(const QJsonValue &value, const QString &hardware, const QString &machine, Walker *walker)
{
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &child : array)
            walk(child, hardware, machine, walker);
        return;
    }
    if (!value.isObject())
        return;

    const QJsonObject node = value.toObject();
    const QString type = typeOf(node);
    const QJsonArray children = childrenOf(node);
    const QString sensorId = sensorIdOf(node);
    const bool sensor = !type.isEmpty() && (node.contains(QStringLiteral("Value"))
                                            || node.contains(QStringLiteral("value"))
                                            || !sensorId.isEmpty());
    if (sensor) {
        QString raw;
        const QJsonValue reading = node.contains(QStringLiteral("Value"))
            ? node.value(QStringLiteral("Value"))
            : node.value(QStringLiteral("value"));
        if (reading.isDouble())
            raw = QString::number(reading.toDouble(), 'f', 3);
        else
            raw = reading.toString();
        Measurement measured = parseMeasurement(raw);
        if (!measured.ok && reading.isDouble()) {
            measured.ok = true;
            measured.value = reading.toDouble();
        }
        const Normalized normal = normalize(measured, type);
        Kind kind = kindFromSensorId(sensorId);
        if (kind == Kind::Unknown)
            kind = kindFromHardwareName(hardware);
        walker->onSensor(kind, hardware, textOf(node), type, normal);
        return;
    }

    if (children.isEmpty())
        return;

    QString nextHardware = hardware;
    QString nextMachine = machine;
    const QString name = textOf(node);
    if (!isCategoryName(name) && !isRootLabel(name)) {
        if (looksLikeHardwareContainer(children))
            nextMachine = name;
        else
            nextHardware = name;
    }
    if (!nextMachine.isEmpty())
        walker->snap->machine = nextMachine;

    for (const QJsonValue &child : children)
        walk(child, nextHardware, nextMachine, walker);
}

QString fpsText(double fps)
{
    if (fps >= 100.0)
        return QString::number(qRound(fps));
    return QString::number(fps, 'f', 1);
}

} // namespace

FrameGraphScale frameGraphScale(const QList<double> &samples)
{
    FrameGraphScale scale;
    if (samples.isEmpty())
        return scale;
    double minV = samples.first();
    double maxV = samples.first();
    for (double sample : samples) {
        minV = std::min(minV, sample);
        maxV = std::max(maxV, sample);
    }
    const double pad = std::max((maxV - minV) * 0.20, std::max(maxV * 0.05, 0.4));
    scale.low = std::max(0.0, minV - pad);
    scale.high = maxV + pad;
    if (scale.high - scale.low < 1.0) {
        const double mid = (minV + maxV) * 0.5;
        scale.low = std::max(0.0, mid - 0.5);
        scale.high = mid + 0.5;
    }
    return scale;
}

HardwareSnapshot parseLibreHardwareMonitor(const QByteArray &json)
{
    HardwareSnapshot snapshot;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || doc.isNull())
        return snapshot;
    if (!doc.isObject() && !doc.isArray())
        return snapshot;

    snapshot.parsed = true;
    Walker walker;
    walker.snap = &snapshot;
    if (doc.isArray())
        walk(doc.array(), QString(), QString(), &walker);
    else
        walk(doc.object(), QString(), QString(), &walker);
    walker.finish();
    return snapshot;
}

FrameSnapshot parseFrameStats(const QByteArray &json)
{
    FrameSnapshot snapshot;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return snapshot;

    snapshot.parsed = true;
    const QJsonObject obj = doc.object();
    if (obj.contains(QStringLiteral("ok")) && !obj.value(QStringLiteral("ok")).toBool(true))
        snapshot.error = obj.value(QStringLiteral("error")).toString();

    snapshot.process = obj.value(QStringLiteral("process")).toString();
    if (snapshot.process.isEmpty())
        snapshot.process = obj.value(QStringLiteral("process_name")).toString();

    const QJsonValue frames = obj.contains(QStringLiteral("frame_ms"))
        ? obj.value(QStringLiteral("frame_ms"))
        : obj.value(QStringLiteral("frames"));
    if (frames.isArray()) {
        const QJsonArray array = frames.toArray();
        snapshot.frameMs.reserve(array.size());
        for (const QJsonValue &value : array) {
            if (value.isDouble())
                snapshot.frameMs.append(value.toDouble());
            else if (value.isString()) {
                bool ok = false;
                const double n = value.toString().toDouble(&ok);
                if (ok)
                    snapshot.frameMs.append(n);
            }
        }
    }

    const bool reported = obj.contains(QStringLiteral("fps_avg"))
        || obj.contains(QStringLiteral("fpsAvg"));
    considerFps(&snapshot);
    if (reported) {
        const double avg = obj.contains(QStringLiteral("fps_avg"))
            ? obj.value(QStringLiteral("fps_avg")).toDouble()
            : obj.value(QStringLiteral("fpsAvg")).toDouble();
        double low = 0;
        if (obj.contains(QStringLiteral("fps_1pct_low")))
            low = obj.value(QStringLiteral("fps_1pct_low")).toDouble();
        else if (obj.contains(QStringLiteral("fps_low")))
            low = obj.value(QStringLiteral("fps_low")).toDouble();
        else if (obj.contains(QStringLiteral("one_percent_low")))
            low = obj.value(QStringLiteral("one_percent_low")).toDouble();
        if (avg > 0.0) {
            snapshot.hasFps = true;
            snapshot.fpsAvg = avg;
            snapshot.fpsOnePctLow = low > 0.0 ? low : snapshot.fpsOnePctLow;
        }
    }

    constexpr int kGraphPoints = 180;
    if (snapshot.frameMs.size() > kGraphPoints)
        snapshot.frameMs = snapshot.frameMs.mid(snapshot.frameMs.size() - kGraphPoints);
    return snapshot;
}

QVariantMap hardwareToVariant(const HardwareSnapshot &snapshot)
{
    QVariantMap map;
    map.insert(QStringLiteral("machine"), snapshot.machine);
    map.insert(QStringLiteral("cpuName"), snapshot.cpuName);
    QStringList cpuParts;
    const QString cpuShort = shortCpuName(snapshot.cpuName);
    if (!cpuShort.isEmpty())
        cpuParts << cpuShort;
    cpuParts << tempText(snapshot.hasCpuTemp, snapshot.cpuTempC)
             << pctText(snapshot.hasCpuLoad, snapshot.cpuLoadPct)
             << (snapshot.hasCpuClock ? clockText(snapshot.cpuClockMHz) : QStringLiteral("--"))
             << wattText(snapshot.hasCpuPower, snapshot.cpuPowerW);
    map.insert(QStringLiteral("cpuLine"), joinPresent(cpuParts));

    map.insert(QStringLiteral("gpuName"), snapshot.gpuName);
    map.insert(QStringLiteral("gpuShort"), shortGpuName(snapshot.gpuName));
    map.insert(QStringLiteral("gpuTempLine"),
               snapshot.hasGpuTemp ? QStringLiteral("TEMP ") + tempText(true, snapshot.gpuTempC) : QString());
    map.insert(QStringLiteral("gpuHotLine"),
               snapshot.hasGpuHotspot ? QStringLiteral("HOTSPOT ") + tempText(true, snapshot.gpuHotspotC) : QString());
    map.insert(QStringLiteral("gpuLoadLine"),
               snapshot.hasGpuLoad ? QStringLiteral("LOAD ") + pctText(true, snapshot.gpuLoadPct) : QString());
    map.insert(QStringLiteral("gpuClockLine"),
               snapshot.hasGpuClock ? QStringLiteral("CLK ") + clockText(snapshot.gpuClockMHz) : QString());
    map.insert(QStringLiteral("gpuPowerLine"),
               snapshot.hasGpuPower ? QStringLiteral("PWR ") + wattText(true, snapshot.gpuPowerW) : QString());
    // Memory clock is not shown. VRAM used/total is the line that used to sit under it.
    if (snapshot.hasVramUsed || snapshot.hasVramTotal) {
        const QString used = snapshot.hasVramUsed ? gbText(snapshot.vramUsedMb) : QStringLiteral("--");
        const QString total = snapshot.hasVramTotal ? gbText(snapshot.vramTotalMb) : QStringLiteral("--");
        map.insert(QStringLiteral("gpuVramLine"),
                   QStringLiteral("VRAM ") + used + QLatin1Char('/') + total + QStringLiteral("G"));
    } else {
        map.insert(QStringLiteral("gpuVramLine"), QString());
    }

    QStringList ram;
    if (snapshot.hasRamUsed || snapshot.hasRamTotal) {
        const QString used = snapshot.hasRamUsed ? gbText(snapshot.ramUsedMb) : QStringLiteral("--");
        const QString total = snapshot.hasRamTotal ? gbText(snapshot.ramTotalMb) : QStringLiteral("--");
        ram << used + QLatin1Char('/') + total + QStringLiteral("G");
    }
    if (snapshot.hasRamLoad)
        ram << pctText(true, snapshot.ramLoadPct);
    map.insert(QStringLiteral("ramLine"), joinPresent(ram));

    struct ShownFan {
        int rank = 0;
        int number = 0;
        int rpm = 0;
        QString family;
    };
    QList<ShownFan> shown;
    for (const FanReading &fan : snapshot.fans) {
        const int rpm = qRound(fan.rpm);
        if (rpm <= 0)
            continue;
        ShownFan item;
        item.family = fanFamily(fan.name);
        item.number = fanNumber(fan.name);
        item.rank = fanRank(item.family);
        item.rpm = rpm;
        shown.append(item);
    }
    std::sort(shown.begin(), shown.end(), [](const ShownFan &a, const ShownFan &b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        if (a.number != b.number) return a.number < b.number;
        return a.rpm > b.rpm;
    });
    constexpr int kMaxFans = 6;
    if (shown.size() > kMaxFans)
        shown = shown.mid(0, kMaxFans);
    QStringList fans;
    QSet<QString> usedLabels;
    for (const ShownFan &fan : shown) {
        QString label = fan.family;
        if (fan.number > 0)
            label += QString::number(fan.number);
        if (usedLabels.contains(label)) {
            int n = fan.number > 0 ? fan.number + 1 : 2;
            while (usedLabels.contains(fan.family + QString::number(n)))
                ++n;
            label = fan.family + QString::number(n);
        }
        usedLabels.insert(label);
        fans << label + QLatin1Char(' ') + QString::number(fan.rpm);
    }
    map.insert(QStringLiteral("fanLine"), fans.isEmpty() ? QStringLiteral("--")
                                                         : fans.join(QStringLiteral("  ")));

    QStringList drives;
    for (const DriveReading &drive : snapshot.drives) {
        if (!drive.hasTemp && !drive.hasRead && !drive.hasWrite)
            continue;
        QString piece = driveLabel(drive.name);
        if (drive.hasTemp)
            piece += QLatin1Char(' ') + tempText(true, drive.tempC);
        if (drive.hasRead)
            piece += QStringLiteral("  R ") + scaledRate(drive.readMBps);
        if (drive.hasWrite)
            piece += QStringLiteral("  W ") + scaledRate(drive.writeMBps);
        drives << piece;
    }
    map.insert(QStringLiteral("driveLines"), drives);

    if (snapshot.hasNet) {
        map.insert(QStringLiteral("netLine"),
                   QStringLiteral("DOWN ") + rateText(snapshot.netDownMBps)
                   + QStringLiteral(" UP ") + rateText(snapshot.netUpMBps));
    } else {
        map.insert(QStringLiteral("netLine"), QStringLiteral("--"));
    }
    map.insert(QStringLiteral("netName"), snapshot.netName);
    return map;
}

QVariantMap framesToVariant(const FrameSnapshot &snapshot)
{
    QVariantMap map;
    map.insert(QStringLiteral("process"), snapshot.process);
    map.insert(QStringLiteral("error"), snapshot.error);
    QVariantList series;
    for (double ms : snapshot.frameMs)
        series.append(ms);
    map.insert(QStringLiteral("frameMs"), series);
    const FrameGraphScale scale = frameGraphScale(snapshot.frameMs);
    map.insert(QStringLiteral("frameLo"), scale.low);
    map.insert(QStringLiteral("frameHi"), scale.high);

    QString state = QStringLiteral("nogame");
    if (!snapshot.error.isEmpty() && !snapshot.hasFps)
        state = QStringLiteral("unavailable");
    else if (snapshot.hasFps)
        state = QStringLiteral("live");
    else if (!snapshot.process.isEmpty())
        state = QStringLiteral("waiting");
    map.insert(QStringLiteral("state"), state);
    map.insert(QStringLiteral("fpsAvg"), snapshot.hasFps ? fpsText(snapshot.fpsAvg) : QStringLiteral("--"));
    map.insert(QStringLiteral("fpsLow"),
               snapshot.hasFps && snapshot.fpsOnePctLow > 0.0 ? fpsText(snapshot.fpsOnePctLow)
                                                              : QStringLiteral("--"));
    return map;
}

} // namespace pcstats
