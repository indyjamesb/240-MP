#pragma once

// Watched-point decision for a MyAnimeList list update.
//
// 240-MP does not compute a second threshold of its own for Plex: it posts
// time and duration on /:/timeline, and Plex marks the item watched at 90%
// of that duration. Emby and Jellyfin do the same with their default
// MaxResumePct of 90. Scrobbling uses that same point. The "never decrease
// progress" rule and the watching/completed status split are the part Plezy's
// MAL tracker is responsible for; rewatch bookkeeping is intentionally not
// ported — a write that would lower num_watched_episodes is skipped entirely.

#include <QString>
#include <QtGlobal>

namespace mal {

constexpr int kWatchedPercent = 90;

bool crossedWatchedPoint(qint64 positionMs, qint64 durationMs);

struct ListSnapshot {
    bool onList = false;
    int progress = 0;
    int episodeCount = 0; // 0 while MAL does not know a total (airing, or omitted)
    bool completed = false;
};

struct MalTarget {
    int malId = 0;
    int episode = 0;
    bool movie = false;
    bool valid() const { return malId > 0 && episode > 0; }
};

struct ScrobbleWrite {
    bool write = false;
    int progress = 0;
    QString status; // "watching" or "completed"
};

ScrobbleWrite decideScrobble(const MalTarget &target, const ListSnapshot &list, bool crossed);

} // namespace mal
