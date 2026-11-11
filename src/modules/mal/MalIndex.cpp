#include "MalIndex.h"

#include <QRegularExpression>

namespace mal {

namespace {

int parsePositiveInt(const QString &text)
{
    if (text.isEmpty())
        return 0;
    for (const QChar c : text) {
        if (c < QLatin1Char('0') || c > QLatin1Char('9'))
            return 0;
    }
    bool ok = false;
    const int n = text.toInt(&ok);
    return ok && n > 0 ? n : 0;
}

QString normalizeImdb(const QString &value, bool allowBareDigits)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized.startsWith(QLatin1String("tt"))) {
        const QString digits = normalized.mid(2);
        return parsePositiveInt(digits) > 0 ? QStringLiteral("tt") + digits : QString();
    }
    if (allowBareDigits && parsePositiveInt(normalized) > 0)
        return QStringLiteral("tt") + normalized;
    return {};
}

// Take the leading decimal run so "76079?lang=en" and "76079" both work.
int leadingInt(QString value)
{
    const int q = value.indexOf(QLatin1Char('?'));
    if (q >= 0)
        value.truncate(q);
    return parsePositiveInt(value);
}

ExternalIds fromLegacyPlexGuid(const QString &guid)
{
    ExternalIds ids;
    const int schemeEnd = guid.indexOf(QLatin1String("://"));
    if (schemeEnd <= 0)
        return ids;
    const QString scheme = guid.left(schemeEnd).toLower();
    QString value = guid.mid(schemeEnd + 3);
    const int query = value.indexOf(QLatin1Char('?'));
    if (query >= 0)
        value.truncate(query);
    // A path would mean this is not the agent form Plezy treats as a host.
    if (value.isEmpty() || value.contains(QLatin1Char('/')))
        return ids;

    if (scheme == QLatin1String("com.plexapp.agents.imdb")) {
        ids.imdb = normalizeImdb(value, false);
        return ids;
    }
    if (scheme == QLatin1String("com.plexapp.agents.themoviedb")) {
        ids.tmdb = leadingInt(value);
        return ids;
    }
    if (scheme == QLatin1String("com.plexapp.agents.thetvdb")) {
        ids.tvdb = leadingInt(value);
        return ids;
    }
    if (scheme != QLatin1String("com.plexapp.agents.hama"))
        return ids;

    const int sep = value.indexOf(QLatin1Char('-'));
    if (sep <= 0 || sep == value.size() - 1)
        return ids;
    const QString source = value.left(sep).toLower();
    const QString id = value.mid(sep + 1);
    if (source == QLatin1String("imdb"))
        ids.imdb = normalizeImdb(id, true);
    else if (source == QLatin1String("anidb"))
        ids.anidb = leadingInt(id);
    else if (source == QLatin1String("tmdb") || source == QLatin1String("tsdb"))
        ids.tmdb = leadingInt(id);
    else if (QRegularExpression(QStringLiteral("^tvdb(?:[2-9])?$")).match(source).hasMatch())
        ids.tvdb = leadingInt(id);
    return ids;
}

bool isRegularSeries(const FribbRow &row)
{
    if (row.isMovie())
        return false;
    if (row.tvdbSeason && *row.tvdbSeason == 0)
        return false;
    if (row.tmdbSeason && *row.tmdbSeason == 0)
        return false;
    const QString type = row.type.toUpper();
    return type.isEmpty() || type == QLatin1String("TV") || type == QLatin1String("ONA")
        || type == QLatin1String("UNKNOWN");
}

std::optional<FribbRow> rowForAnidb(const QVector<FribbRow> &rows, int anidbId)
{
    for (const FribbRow &row : rows) {
        if (row.anidbId == anidbId)
            return row;
    }
    return std::nullopt;
}

std::optional<FribbRow> pickMovie(const QVector<FribbRow> &rows)
{
    if (rows.isEmpty())
        return std::nullopt;
    for (const FribbRow &row : rows) {
        if (row.isMovie())
            return row;
    }
    return rows.first();
}

std::optional<FribbRow> pickShow(const QVector<FribbRow> &rows, std::optional<int> season, int anidbId,
                                 const AnimeEpisodeMatch *match)
{
    if (rows.isEmpty())
        return std::nullopt;
    if (match)
        return rowForAnidb(rows, match->anidbId);
    if (anidbId > 0) {
        if (auto row = rowForAnidb(rows, anidbId))
            return row;
    }

    QVector<FribbRow> candidates = rows;
    if (season) {
        for (const FribbRow &row : rows) {
            if ((row.tvdbSeason && *row.tvdbSeason == *season)
                || (row.tmdbSeason && *row.tmdbSeason == *season))
                return row;
        }
        // A row mapped to a different season must not absorb this one. An
        // unmapped row still covers absolute numbering, but never specials.
        if (*season <= 0)
            return std::nullopt;
        candidates.clear();
        for (const FribbRow &row : rows) {
            if (!row.hasSeasonMapping())
                candidates.append(row);
        }
        if (candidates.isEmpty())
            return std::nullopt;
    }

    for (const FribbRow &row : candidates) {
        if (isRegularSeries(row))
            return row;
    }
    for (const FribbRow &row : candidates) {
        if (!row.isMovie())
            return row;
    }
    return candidates.first();
}

int usableAnidb(const ExternalIds &ids, bool movie, std::optional<int> season)
{
    if (ids.anidb <= 0)
        return 0;
    if (movie || !season.has_value() || *season == 1)
        return ids.anidb;
    return 0;
}

int matchPriority(MatchKind kind)
{
    switch (kind) {
    case MatchKind::Explicit: return 0;
    case MatchKind::Range: return 1;
    case MatchKind::DefaultMapping: return 2;
    }
    return 2;
}

std::optional<AnimeEpisodeMatch> selectMatch(const QVector<AnimeEpisodeMatch> &matches)
{
    if (matches.isEmpty())
        return std::nullopt;
    int best = matchPriority(matches.first().kind);
    for (const AnimeEpisodeMatch &match : matches)
        best = qMin(best, matchPriority(match.kind));
    const AnimeEpisodeMatch *first = nullptr;
    for (const AnimeEpisodeMatch &match : matches) {
        if (matchPriority(match.kind) != best)
            continue;
        if (!first)
            first = &match;
        else if (!first->sameEpisode(match))
            return std::nullopt;
    }
    return first ? std::optional<AnimeEpisodeMatch>(*first) : std::nullopt;
}

} // namespace

ExternalIds ExternalIds::filledFrom(const ExternalIds &fallback) const
{
    ExternalIds out = *this;
    if (out.imdb.isEmpty())
        out.imdb = fallback.imdb;
    if (out.tmdb <= 0)
        out.tmdb = fallback.tmdb;
    if (out.tvdb <= 0)
        out.tvdb = fallback.tvdb;
    if (out.anidb <= 0)
        out.anidb = fallback.anidb;
    return out;
}

ExternalIds externalIdsFromPlex(const QStringList &guids, const QString &legacyGuid)
{
    ExternalIds ids;
    for (const QString &raw : guids) {
        const QString id = raw.trimmed();
        const auto take = [&](const QLatin1String prefix, int &dest) {
            if (!id.startsWith(prefix) || dest > 0)
                return;
            dest = leadingInt(id.mid(prefix.size()));
        };
        if (id.startsWith(QLatin1String("imdb://")) && ids.imdb.isEmpty()) {
            QString imdb = id.mid(7);
            const int query = imdb.indexOf(QLatin1Char('?'));
            if (query >= 0)
                imdb.truncate(query);
            ids.imdb = normalizeImdb(imdb, false);
        }
        else
            take(QLatin1String("tmdb://"), ids.tmdb);
        take(QLatin1String("tvdb://"), ids.tvdb);
        take(QLatin1String("anidb://"), ids.anidb);
    }
    return ids.filledFrom(fromLegacyPlexGuid(legacyGuid));
}

ExternalIds externalIdsFromProviders(const QVariantMap &providerIds)
{
    ExternalIds ids;
    for (auto it = providerIds.cbegin(); it != providerIds.cend(); ++it) {
        const QString key = it.key().toLower();
        const QString value = it.value().toString().trimmed();
        if (value.isEmpty())
            continue;
        if (key == QLatin1String("imdb") && ids.imdb.isEmpty())
            ids.imdb = normalizeImdb(value, true);
        else if (key == QLatin1String("tmdb") && ids.tmdb <= 0)
            ids.tmdb = leadingInt(value);
        else if (key == QLatin1String("tvdb") && ids.tvdb <= 0)
            ids.tvdb = leadingInt(value);
        else if ((key == QLatin1String("anidb") || key == QLatin1String("anidbid")) && ids.anidb <= 0)
            ids.anidb = leadingInt(value);
    }
    return ids;
}

QVector<ExplicitEpisode> parseExplicitMappings(const QString &raw)
{
    QVector<ExplicitEpisode> mappings;
    for (const QString &segment : raw.split(QLatin1Char(';'))) {
        const QString item = segment.trimmed();
        const int separator = item.indexOf(QLatin1Char('-'));
        if (separator <= 0 || separator == item.size() - 1)
            continue;
        const int anidbEpisode = parsePositiveInt(item.left(separator));
        if (anidbEpisode <= 0)
            continue;
        ExplicitEpisode mapped;
        mapped.anidbEpisode = anidbEpisode;
        for (const QString &target : item.mid(separator + 1).split(QLatin1Char('+'))) {
            const int episode = parsePositiveInt(target.trimmed());
            if (episode > 0)
                mapped.externalEpisodes.append(episode);
        }
        if (!mapped.externalEpisodes.isEmpty())
            mappings.append(mapped);
    }
    return mappings;
}

SeasonRef seasonRefFrom(std::optional<qint64> number, const QString &word)
{
    SeasonRef ref;
    if (number.has_value()) {
        ref.number = int(*number);
        return ref;
    }
    if (word == QLatin1String("a"))
        ref.absolute = true;
    return ref;
}

QVector<AnimeEpisodeMatch> AnimeListEntry::resolveEpisode(ListProvider provider, int externalSeason,
                                                          int externalEpisode) const
{
    QVector<AnimeEpisodeMatch> explicitMatches;
    for (const EpisodeMapping &mapping : mappings) {
        if (mapping.provider != provider || mapping.externalSeason != externalSeason)
            continue;
        for (const ExplicitEpisode &item : mapping.explicitMaps) {
            if (!item.externalEpisodes.contains(externalEpisode))
                continue;
            AnimeEpisodeMatch match;
            match.anidbId = anidbId;
            match.anidbSeason = mapping.anidbSeason;
            match.anidbEpisode = item.anidbEpisode;
            match.kind = MatchKind::Explicit;
            explicitMatches.append(match);
        }
    }
    if (!explicitMatches.isEmpty())
        return explicitMatches;

    QVector<AnimeEpisodeMatch> ranges;
    for (const EpisodeMapping &mapping : mappings) {
        if (mapping.provider != provider || mapping.externalSeason != externalSeason)
            continue;
        if (!mapping.start.has_value())
            continue;
        const int anidbEpisode = externalEpisode - mapping.offset;
        if (anidbEpisode < *mapping.start)
            continue;
        if (mapping.end && anidbEpisode > *mapping.end)
            continue;
        if (anidbEpisode <= 0)
            continue;
        AnimeEpisodeMatch match;
        match.anidbId = anidbId;
        match.anidbSeason = mapping.anidbSeason;
        match.anidbEpisode = anidbEpisode;
        match.kind = MatchKind::Range;
        ranges.append(match);
    }
    if (!ranges.isEmpty())
        return ranges;

    const SeasonRef &def = provider == ListProvider::Tvdb ? defaultTvdbSeason : tmdbSeason;
    const int defOffset = provider == ListProvider::Tvdb ? episodeOffset : tmdbOffset;
    if (!def.valid() || def.absolute || !def.number || *def.number != externalSeason)
        return {};
    const int anidbEpisode = externalEpisode - defOffset;
    if (anidbEpisode <= 0)
        return {};
    AnimeEpisodeMatch match;
    match.anidbId = anidbId;
    match.anidbSeason = 1;
    match.anidbEpisode = anidbEpisode;
    match.kind = MatchKind::DefaultMapping;
    return {match};
}

FribbIndex FribbIndex::fromRows(QVector<FribbRow> rows)
{
    FribbIndex index;
    index.m_rows = std::move(rows);
    for (int i = 0; i < index.m_rows.size(); ++i) {
        const FribbRow &row = index.m_rows.at(i);
        if (row.tvdbId > 0)
            index.m_byTvdb[row.tvdbId].append(i);
        if (row.tmdbTvId > 0)
            index.m_byTmdbTv[row.tmdbTvId].append(i);
        for (int movieId : row.tmdbMovieIds) {
            if (movieId > 0)
                index.m_byTmdbMovie[movieId].append(i);
        }
        for (const QString &imdb : row.imdbIds) {
            if (!imdb.isEmpty())
                index.m_byImdb[imdb].append(i);
        }
        if (row.anidbId > 0 && !index.m_byAnidb.contains(row.anidbId))
            index.m_byAnidb.insert(row.anidbId, i);
    }
    return index;
}

QVector<FribbRow> FribbIndex::gather(const QVector<int> &indexes) const
{
    QVector<FribbRow> out;
    out.reserve(indexes.size());
    for (int index : indexes)
        out.append(m_rows.at(index));
    return out;
}

QVector<FribbRow> FribbIndex::lookup(bool movie, int anidbId, int tvdbId, int tmdbId,
                                     const QString &imdbId) const
{
    if (anidbId > 0) {
        const auto it = m_byAnidb.constFind(anidbId);
        if (it != m_byAnidb.cend())
            return {m_rows.at(it.value())};
    }
    if (tvdbId > 0 && !movie) {
        const auto it = m_byTvdb.constFind(tvdbId);
        if (it != m_byTvdb.cend() && !it.value().isEmpty())
            return gather(it.value());
    }
    if (tmdbId > 0) {
        const auto &table = movie ? m_byTmdbMovie : m_byTmdbTv;
        const auto it = table.constFind(tmdbId);
        if (it != table.cend() && !it.value().isEmpty())
            return gather(it.value());
    }
    if (!imdbId.isEmpty()) {
        const auto it = m_byImdb.constFind(imdbId);
        if (it != m_byImdb.cend() && !it.value().isEmpty())
            return gather(it.value());
    }
    return {};
}

AnimeListsIndex AnimeListsIndex::fromEntries(QVector<AnimeListEntry> entries)
{
    AnimeListsIndex index;
    index.m_entries = std::move(entries);
    for (int i = 0; i < index.m_entries.size(); ++i) {
        const AnimeListEntry &entry = index.m_entries.at(i);
        if (entry.tvdbId > 0)
            index.m_byTvdb[entry.tvdbId].append(i);
        if (entry.tmdbTvId > 0)
            index.m_byTmdb[entry.tmdbTvId].append(i);
    }
    return index;
}

QVector<AnimeEpisodeMatch> AnimeListsIndex::matches(const QHash<int, QVector<int>> &table, int seriesId,
                                                    ListProvider provider, int season, int episode) const
{
    QVector<AnimeEpisodeMatch> found;
    const auto it = table.constFind(seriesId);
    if (it == table.cend())
        return found;
    for (int index : it.value())
        found += m_entries.at(index).resolveEpisode(provider, season, episode);
    return found;
}

std::optional<AnimeEpisodeMatch> AnimeListsIndex::lookupEpisode(int tvdbId, int tmdbId, int season,
                                                                int episodeNumber) const
{
    if (episodeNumber <= 0)
        return std::nullopt;
    if (tvdbId > 0) {
        if (auto selected = selectMatch(matches(m_byTvdb, tvdbId, ListProvider::Tvdb, season, episodeNumber)))
            return selected;
    }
    if (tmdbId > 0)
        return selectMatch(matches(m_byTmdb, tmdbId, ListProvider::Tmdb, season, episodeNumber));
    return std::nullopt;
}

MalTarget resolveMalTarget(const AnimeMappings &mappings, const PlaybackRef &item)
{
    MalTarget target;
    if (!item.ids.hasAny())
        return target;

    const int anidb = usableAnidb(item.ids, item.movie, item.season);
    const QVector<FribbRow> rows = mappings.fribb.lookup(item.movie, anidb, item.ids.tvdb, item.ids.tmdb,
                                                         item.ids.imdb);
    const bool haveEpisode = !item.movie && item.season.has_value() && item.episode.has_value()
        && *item.episode > 0;
    std::optional<AnimeEpisodeMatch> match;
    if (haveEpisode) {
        match = mappings.lists.lookupEpisode(item.ids.tvdb, item.ids.tmdb, *item.season, *item.episode);
    }
    // An AniDB special is numbered beside the main episodes. Writing that
    // number as list progress would rewind or finish the series entry.
    if (match && match->anidbSeason == 0)
        return target;

    const std::optional<FribbRow> row = item.movie
        ? pickMovie(rows)
        : pickShow(rows, item.season, anidb, match ? &*match : nullptr);
    if (!row || row->malId <= 0)
        return target;

    target.malId = row->malId;
    target.movie = item.movie;
    if (item.movie) {
        target.episode = 1;
        return target;
    }
    if (match && match->anidbEpisode > 0)
        target.episode = match->anidbEpisode;
    else if (item.episode && *item.episode > 0)
        target.episode = *item.episode;
    return target;
}

} // namespace mal
