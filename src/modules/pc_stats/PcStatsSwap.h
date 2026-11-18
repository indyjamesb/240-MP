#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QtGlobal>

namespace pcstats {

// "15 MIN" / "2 SEC" -> the leading number. Non-positive or unparsable text
// keeps the fallback, matching Panel Drift and Poll Interval.
inline int leadingPositiveInt(const QString &text, int fallback)
{
    bool ok = false;
    const int n = text.trimmed().section(QLatin1Char(' '), 0, 0).toInt(&ok);
    return ok && n > 0 ? n : fallback;
}

// Missing, null, or blank stays on, so a config written before this key
// exists keeps swapping. Settings stores a bool; a hand-edited file may
// say ON/OFF.
inline bool sectionSwapEnabledFromConfig(const QJsonObject &config)
{
    const QString key = QStringLiteral("section_swap");
    if (!config.contains(key))
        return true;
    const QJsonValue value = config.value(key);
    if (value.isNull() || value.isUndefined())
        return true;
    if (value.isBool())
        return value.toBool();
    if (value.isDouble())
        return value.toInt() != 0;
    const QString text = value.toString().trimmed();
    if (text.isEmpty())
        return true;
    return text.compare(QLatin1String("on"), Qt::CaseInsensitive) == 0
        || text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
        || text == QLatin1String("1");
}

// Minutes, clamped. The Settings row offers 5/10/15/30; a hand-edited value
// outside 1..120 still resolves instead of being ignored or overflowing a timer.
inline int swapIntervalMsFromConfig(const QJsonObject &config)
{
    const int minutes = qBound(1, leadingPositiveInt(
        config.value(QStringLiteral("swap_interval")).toString(), 15), 120);
    return minutes * 60 * 1000;
}

// CPU half on top during [0, interval), GPU half on top during
// [interval, 2*interval), then it repeats. Disabled is always CPU on top.
inline bool gpuOnTopForElapsed(qint64 elapsedMs, int intervalMs, bool enabled)
{
    if (!enabled || intervalMs <= 0 || elapsedMs < intervalMs)
        return false;
    return (elapsedMs / qint64(intervalMs)) % 2 == 1;
}

// Elapsed time the PC Stats screen has been open with swap enabled.
// pause() stores the current tick; a later resume() continues from there,
// and a second pause() does not add the same tick again. Time spent away
// from the screen is not passed in, so it does not move the phase.
class SectionSwapClock {
public:
    void resume()
    {
        m_running = true;
    }

    void pause(qint64 tickElapsedMs)
    {
        if (!m_running)
            return;
        if (tickElapsedMs > 0)
            m_accumulatedMs += tickElapsedMs;
        m_running = false;
    }

    qint64 elapsed(qint64 tickElapsedMs) const
    {
        qint64 ms = m_accumulatedMs;
        if (m_running && tickElapsedMs > 0)
            ms += tickElapsedMs;
        return ms;
    }

    bool running() const { return m_running; }

    bool gpuOnTop(qint64 tickElapsedMs, int intervalMs, bool enabled) const
    {
        return gpuOnTopForElapsed(elapsed(tickElapsedMs), intervalMs, enabled);
    }

    // Milliseconds until the next half trades places. 0 when swap is off.
    int msUntilNext(qint64 tickElapsedMs, int intervalMs, bool enabled) const
    {
        if (!enabled || intervalMs <= 0)
            return 0;
        const qint64 into = elapsed(tickElapsedMs) % qint64(intervalMs);
        const qint64 remaining = qMax(qint64(1), qint64(intervalMs) - into);
        return int(remaining);
    }

private:
    qint64 m_accumulatedMs = 0;
    bool m_running = false;
};

} // namespace pcstats
