#pragma once

// Anime id matching for MyAnimeList scrobbling.
//
// The lookup rules follow Plezy (https://github.com/edde746/plezy, GPLv3, the
// same licence as 240-MP): Fribb's anime-lists for show/movie ids, and
// Anime-Lists' episode map for split-cour and season offsets. This is a
// reimplementation of that behaviour, not a line-by-line translation.
// Mapping data itself comes from Fribb/anime-lists and Anime-Lists/anime-lists
// via the edde746/anime-mappings release asset. See docs/MAL.md.

#include "MalScrobble.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <optional>

namespace mal {

struct ExternalIds {
    QString imdb;
    int tmdb = 0;
    int tvdb = 0;
    int anidb = 0;

    bool hasAny() const { return !imdb.isEmpty() || tmdb > 0 || tvdb > 0 || anidb > 0; }
    bool hasCatalog() const { return !imdb.isEmpty() || tmdb > 0 || tvdb > 0; }
    ExternalIds filledFrom(const ExternalIds &fallback) const;
};

// Modern Plex Guid[] values plus a legacy agent guid. AniDB is read from
// `anidb://` and from HAMA's plain `anidb-<id>` guid. HAMA's anidb2..anidb9
// grouping modes are ignored: they name only the root entry.
ExternalIds externalIdsFromPlex(const QStringList &guids, const QString &legacyGuid);

// Jellyfin / Emby ProviderIds. Keys are matched case-insensitively.
ExternalIds externalIdsFromProviders(const QVariantMap &providerIds);

struct FribbRow {
    int anidbId = 0;
    int malId = 0;
    int tvdbId = 0;
    int tmdbTvId = 0;
    QVector<int> tmdbMovieIds;
    QStringList imdbIds;
    std::optional<int> tvdbSeason;
    std::optional<int> tmdbSeason;
    QString type;

    bool isMovie() const { return type.compare(QLatin1String("MOVIE"), Qt::CaseInsensitive) == 0; }
    bool hasSeasonMapping() const { return tvdbSeason.has_value() || tmdbSeason.has_value(); }
};

enum class ListProvider { Tvdb, Tmdb };
enum class MatchKind { Explicit, Range, DefaultMapping };

struct ExplicitEpisode {
    int anidbEpisode = 0;
    QVector<int> externalEpisodes;
};

struct EpisodeMapping {
    int anidbSeason = 0;
    ListProvider provider = ListProvider::Tvdb;
    int externalSeason = 0;
    std::optional<int> start;
    std::optional<int> end;
    int offset = 0;
    QVector<ExplicitEpisode> explicitMaps;
};

struct SeasonRef {
    bool absolute = false;
    std::optional<int> number;
    bool valid() const { return absolute || number.has_value(); }
};

struct AnimeListEntry {
    int anidbId = 0;
    int tvdbId = 0;
    SeasonRef defaultTvdbSeason;
    int episodeOffset = 0;
    int tmdbTvId = 0;
    SeasonRef tmdbSeason;
    int tmdbOffset = 0;
    QVector<EpisodeMapping> mappings;

    QVector<struct AnimeEpisodeMatch> resolveEpisode(ListProvider provider, int externalSeason,
                                                     int externalEpisode) const;
};

struct AnimeEpisodeMatch {
    int anidbId = 0;
    int anidbSeason = 0;
    int anidbEpisode = 0;
    MatchKind kind = MatchKind::DefaultMapping;

    bool sameEpisode(const AnimeEpisodeMatch &other) const
    {
        return anidbId == other.anidbId && anidbSeason == other.anidbSeason
            && anidbEpisode == other.anidbEpisode && kind == other.kind;
    }
};

class FribbIndex {
public:
    static FribbIndex fromRows(QVector<FribbRow> rows);

    bool isEmpty() const { return m_rows.isEmpty(); }
    int tvdbSeriesCount() const { return m_byTvdb.size(); }
    int rowCount() const { return m_rows.size(); }

    // First non-empty candidate list, in order anidb → tvdb → tmdb → imdb.
    // A movie skips TVDB (Fribb's tvdb_id is a series id) and looks TMDB up
    // in the movie namespace only.
    QVector<FribbRow> lookup(bool movie, int anidbId, int tvdbId, int tmdbId,
                             const QString &imdbId) const;

private:
    QVector<FribbRow> gather(const QVector<int> &indexes) const;

    QVector<FribbRow> m_rows;
    QHash<int, QVector<int>> m_byTvdb;
    QHash<int, QVector<int>> m_byTmdbTv;
    QHash<int, QVector<int>> m_byTmdbMovie;
    QHash<QString, QVector<int>> m_byImdb;
    QHash<int, int> m_byAnidb;
};

class AnimeListsIndex {
public:
    static AnimeListsIndex fromEntries(QVector<AnimeListEntry> entries);

    bool isEmpty() const { return m_entries.isEmpty(); }
    int tvdbSeriesCount() const { return m_byTvdb.size(); }

    // The AniDB episode an aired episode corresponds to, when exactly one
    // best match wins. Explicit beats range beats the entry's default season.
    // Same-priority matches that disagree resolve to null.
    std::optional<AnimeEpisodeMatch> lookupEpisode(int tvdbId, int tmdbId, int season,
                                                   int episodeNumber) const;

private:
    QVector<AnimeEpisodeMatch> matches(const QHash<int, QVector<int>> &table, int seriesId,
                                       ListProvider provider, int season, int episode) const;

    QVector<AnimeListEntry> m_entries;
    QHash<int, QVector<int>> m_byTvdb;
    QHash<int, QVector<int>> m_byTmdb;
};

struct AnimeMappings {
    FribbIndex fribb;
    AnimeListsIndex lists;
    bool isEmpty() const { return fribb.isEmpty() && lists.isEmpty(); }
};

struct PlaybackRef {
    bool movie = false;
    std::optional<int> season;
    std::optional<int> episode;
    ExternalIds ids;
};

// MAL id and the episode number to claim on that entry. Empty when the item
// does not map, including AniDB specials (anidb season 0), which must not be
// written as progress on the main series.
MalTarget resolveMalTarget(const AnimeMappings &mappings, const PlaybackRef &item);

QVector<ExplicitEpisode> parseExplicitMappings(const QString &raw);
SeasonRef seasonRefFrom(std::optional<qint64> number, const QString &word);

} // namespace mal
