#include "MalScrobble.h"

namespace mal {

bool crossedWatchedPoint(qint64 positionMs, qint64 durationMs)
{
    if (positionMs <= 0 || durationMs <= 0)
        return false;
    // Integer form of position/duration >= 0.90, the point at which the
    // timeline 240-MP already sends makes Plex (and Emby/Jellyfin at their
    // default) record the item as watched.
    return positionMs * 10 >= durationMs * 9;
}

ScrobbleWrite decideScrobble(const MalTarget &target, const ListSnapshot &list, bool crossed)
{
    ScrobbleWrite out;
    if (!crossed || !target.valid())
        return out;
    // A later rewatch of an earlier episode must not walk the counter back.
    if (target.episode < list.progress)
        return out;

    const bool completes = target.movie
        || (list.episodeCount > 0 && target.episode >= list.episodeCount);

    // Leaving a finished entry on "watching" because this play is not the
    // finale would pull a completed show off the completed list. Skip it.
    if (list.completed && target.episode <= list.progress && !completes)
        return out;

    if (target.episode == list.progress && list.onList) {
        if (completes && list.completed)
            return out;
        if (!completes && !list.completed)
            return out;
    }

    out.write = true;
    out.progress = target.episode;
    out.status = completes ? QStringLiteral("completed") : QStringLiteral("watching");
    return out;
}

} // namespace mal
