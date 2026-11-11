#include "MalIndex.h"
#include "MalPackedList.h"
#include "MalQr.h"
#include "MalScrobble.h"
#include "third_party/qrcodegen.hpp"

#include <QtTest>

#include <zlib.h>

#include <cstring>

using namespace mal;

namespace {

void writeVarint(QByteArray &out, quint64 n)
{
    while (n >= 0x80) {
        out.append(char((n & 0x7f) | 0x80));
        n >>= 7;
    }
    out.append(char(n));
}

quint64 zigzag(qint64 n)
{
    return (quint64(n) << 1) ^ quint64(n >> 63);
}

void writeString(QByteArray &out, const QByteArray &text)
{
    writeVarint(out, quint64(text.size()));
    out.append(text);
}

QByteArray intColumn(const QList<std::optional<qint64>> &values)
{
    QByteArray col;
    for (const auto &value : values) {
        if (!value) {
            writeVarint(col, 0);
            continue;
        }
        writeVarint(col, zigzag(*value) + 1);
    }
    return col;
}

QByteArray deltaColumn(const QList<std::optional<qint64>> &values)
{
    QByteArray col;
    qint64 previous = 0;
    for (const auto &value : values) {
        if (!value) {
            writeVarint(col, 0);
            continue;
        }
        writeVarint(col, zigzag(*value - previous) + 1);
        previous = *value;
    }
    return col;
}

QByteArray dictColumn(const QStringList &words, const QList<int> &indexes)
{
    QByteArray col;
    writeVarint(col, quint64(words.size()));
    for (const QString &word : words)
        writeString(col, word.toUtf8());
    for (int index : indexes)
        writeVarint(col, index < 0 ? 0 : quint64(index + 1));
    return col;
}

QByteArray intListColumn(const QList<QList<qint64>> &rows)
{
    QByteArray col;
    for (const QList<qint64> &row : rows) {
        if (row.isEmpty()) {
            writeVarint(col, 0);
            continue;
        }
        writeVarint(col, quint64(row.size() + 1));
        for (qint64 n : row)
            writeVarint(col, zigzag(n));
    }
    return col;
}

void addColumn(QByteArray &out, const char *name, int type, const QByteArray &data)
{
    writeString(out, name);
    out.append(char(type));
    writeVarint(out, quint64(data.size()));
    out.append(data);
}

QByteArray gzipWrap(const QByteArray &raw)
{
    z_stream strm;
    std::memset(&strm, 0, sizeof(strm));
    deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(raw.constData()));
    strm.avail_in = uInt(raw.size());
    QByteArray out;
    char buf[1024];
    int rc = Z_OK;
    while (rc != Z_STREAM_END) {
        strm.next_out = reinterpret_cast<Bytef *>(buf);
        strm.avail_out = sizeof(buf);
        rc = deflate(&strm, Z_FINISH);
        out.append(buf, int(sizeof(buf) - strm.avail_out));
    }
    deflateEnd(&strm);
    return out;
}

AnimeMappings sampleMappings()
{
    QByteArray file;
    file.append("AMAP");
    file.append(char(1));
    writeVarint(file, 3);

    writeString(file, "fribb");
    writeVarint(file, 3);
    writeVarint(file, 6);
    addColumn(file, "type", 4, dictColumn({QStringLiteral("TV"), QStringLiteral("MOVIE")}, {0, 0, 1}));
    addColumn(file, "anidb_id", 2, deltaColumn({10, 11, 40}));
    addColumn(file, "mal_id", 2, deltaColumn({100, 200, 9}));
    addColumn(file, "tvdb_id", 1, intColumn({50, 50, std::nullopt}));
    addColumn(file, "themoviedb_id.movie", 5, intListColumn({{}, {}, {5}}));
    addColumn(file, "season.tvdb", 1, intColumn({1, 2, std::nullopt}));

    writeString(file, "anime_lists");
    writeVarint(file, 1);
    writeVarint(file, 4);
    addColumn(file, "anidbid", 2, deltaColumn({11}));
    addColumn(file, "tvdbid", 1, intColumn({50}));
    addColumn(file, "defaulttvdbseason", 1, intColumn({2}));
    addColumn(file, "episodeoffset", 1, intColumn({1}));

    writeString(file, "anime_lists_mappings");
    writeVarint(file, 1);
    writeVarint(file, 6);
    addColumn(file, "anidbid", 2, deltaColumn({11}));
    addColumn(file, "anidbseason", 1, intColumn({1}));
    addColumn(file, "tvdbseason", 1, intColumn({2}));
    addColumn(file, "start", 1, intColumn({1}));
    addColumn(file, "end", 1, intColumn({12}));
    addColumn(file, "text", 3, [] {
        QByteArray col;
        const QByteArray text = ";3-4;";
        writeVarint(col, quint64(text.size() + 1));
        col.append(text);
        return col;
    }());

    return decodeAnimeMappings(gzipWrap(file));
}

FribbRow row(int anidb, int mal, int tvdb, std::optional<int> season, const QString &type)
{
    FribbRow out;
    out.anidbId = anidb;
    out.malId = mal;
    out.tvdbId = tvdb;
    out.tvdbSeason = season;
    out.type = type;
    return out;
}

} // namespace

class MalTests : public QObject {
    Q_OBJECT
private slots:
    void watchedPointMatchesPlexTimeline();
    void scrobbleNeverDecreasesAndCompletesTheFinale();
    void splitCourUsesTheSeasonEntry();
    void unmappedSeasonDoesNotStealAnotherCour();
    void movieTmdbDoesNotMatchASeries();
    void hamaAnidbOnlyCountsForSeasonOne();
    void specialsAreNotProgress();
    void explicitMappingBeatsTheDefaultOffset();
    void packedListRoundTrip();
    void realMappingsFileIfPresent();
    void buildQrImageSize();
};

void MalTests::watchedPointMatchesPlexTimeline()
{
    QCOMPARE(crossedWatchedPoint(89, 100), false);
    QCOMPARE(crossedWatchedPoint(90, 100), true);
    QCOMPARE(crossedWatchedPoint(0, 100), false);
    QCOMPARE(crossedWatchedPoint(1000, 0), false);
    QCOMPARE(crossedWatchedPoint(9 * 60 * 1000, 10 * 60 * 1000), true);
}

void MalTests::scrobbleNeverDecreasesAndCompletesTheFinale()
{
    MalTarget episode;
    episode.malId = 100;
    episode.episode = 4;
    ListSnapshot list;
    list.onList = true;
    list.progress = 6;
    list.episodeCount = 12;
    QVERIFY(!decideScrobble(episode, list, true).write);

    list.progress = 2;
    const ScrobbleWrite watching = decideScrobble(episode, list, true);
    QVERIFY(watching.write);
    QCOMPARE(watching.progress, 4);
    QCOMPARE(watching.status, QStringLiteral("watching"));

    episode.episode = 12;
    list.progress = 11;
    const ScrobbleWrite done = decideScrobble(episode, list, true);
    QVERIFY(done.write);
    QCOMPARE(done.progress, 12);
    QCOMPARE(done.status, QStringLiteral("completed"));

    list.progress = 12;
    list.completed = true;
    QVERIFY(!decideScrobble(episode, list, true).write);

    list.completed = true;
    list.progress = 12;
    episode.episode = 3;
    QVERIFY(!decideScrobble(episode, list, true).write);

    MalTarget movie;
    movie.malId = 9;
    movie.episode = 1;
    movie.movie = true;
    const ScrobbleWrite film = decideScrobble(movie, ListSnapshot{}, true);
    QVERIFY(film.write);
    QCOMPARE(film.progress, 1);
    QCOMPARE(film.status, QStringLiteral("completed"));

    QVERIFY(!decideScrobble(episode, list, false).write);
}

void MalTests::splitCourUsesTheSeasonEntry()
{
    AnimeMappings mappings;
    mappings.fribb = FribbIndex::fromRows({
        row(1, 100, 50, 1, QStringLiteral("TV")),
        row(2, 200, 50, 2, QStringLiteral("TV")),
    });
    PlaybackRef item;
    item.ids.tvdb = 50;
    item.season = 2;
    item.episode = 3;
    const MalTarget target = resolveMalTarget(mappings, item);
    QCOMPARE(target.malId, 200);
    QCOMPARE(target.episode, 3);
}

void MalTests::unmappedSeasonDoesNotStealAnotherCour()
{
    AnimeMappings mappings;
    mappings.fribb = FribbIndex::fromRows({
        row(1, 100, 50, 1, QStringLiteral("TV")),
        row(2, 200, 50, 2, QStringLiteral("TV")),
    });
    PlaybackRef item;
    item.ids.tvdb = 50;
    item.season = 3;
    item.episode = 1;
    QVERIFY(!resolveMalTarget(mappings, item).valid());

    item.season = 0;
    QVERIFY(!resolveMalTarget(mappings, item).valid());
}

void MalTests::movieTmdbDoesNotMatchASeries()
{
    FribbRow series;
    series.anidbId = 1;
    series.malId = 8;
    series.tmdbTvId = 5;
    series.type = QStringLiteral("TV");
    FribbRow film;
    film.anidbId = 2;
    film.malId = 9;
    film.tmdbMovieIds = {5};
    film.type = QStringLiteral("MOVIE");
    AnimeMappings mappings;
    mappings.fribb = FribbIndex::fromRows({series, film});
    PlaybackRef movie;
    movie.movie = true;
    movie.ids.tmdb = 5;
    const MalTarget target = resolveMalTarget(mappings, movie);
    QCOMPARE(target.malId, 9);
    QVERIFY(target.movie);
    QCOMPARE(target.episode, 1);

    PlaybackRef show;
    show.ids.tmdb = 5;
    show.season = 1;
    show.episode = 1;
    QCOMPARE(resolveMalTarget(mappings, show).malId, 8);
}

void MalTests::hamaAnidbOnlyCountsForSeasonOne()
{
    AnimeMappings mappings;
    mappings.fribb = FribbIndex::fromRows({
        row(7, 70, 50, 1, QStringLiteral("TV")),
        row(8, 80, 50, 2, QStringLiteral("TV")),
    });
    PlaybackRef item;
    item.ids.anidb = 7;
    item.ids.tvdb = 50;
    item.season = 2;
    item.episode = 1;
    QCOMPARE(resolveMalTarget(mappings, item).malId, 80);

    item.season = 1;
    QCOMPARE(resolveMalTarget(mappings, item).malId, 70);

    const ExternalIds hama = externalIdsFromPlex({}, QStringLiteral("com.plexapp.agents.hama://anidb-7?lang=en"));
    QCOMPARE(hama.anidb, 7);
    const ExternalIds grouped = externalIdsFromPlex({}, QStringLiteral("com.plexapp.agents.hama://anidb2-7?lang=en"));
    QCOMPARE(grouped.anidb, 0);
    const ExternalIds modern = externalIdsFromPlex(
        {QStringLiteral("tvdb://50"), QStringLiteral("imdb://tt123"), QStringLiteral("anidb://7")},
        QString());
    QCOMPARE(modern.tvdb, 50);
    QCOMPARE(modern.imdb, QStringLiteral("tt123"));
    QCOMPARE(modern.anidb, 7);
}

void MalTests::specialsAreNotProgress()
{
    AnimeListEntry entry;
    entry.anidbId = 2;
    entry.tvdbId = 50;
    EpisodeMapping mapping;
    mapping.anidbSeason = 0;
    mapping.provider = ListProvider::Tvdb;
    mapping.externalSeason = 0;
    mapping.start = 1;
    mapping.explicitMaps = {};
    entry.mappings = {mapping};
    AnimeMappings mappings;
    mappings.fribb = FribbIndex::fromRows({row(2, 200, 50, std::nullopt, QStringLiteral("TV"))});
    mappings.lists = AnimeListsIndex::fromEntries({entry});
    PlaybackRef item;
    item.ids.tvdb = 50;
    item.season = 0;
    item.episode = 1;
    QVERIFY(!resolveMalTarget(mappings, item).valid());
}

void MalTests::explicitMappingBeatsTheDefaultOffset()
{
    AnimeListEntry entry;
    entry.anidbId = 11;
    entry.tvdbId = 50;
    entry.defaultTvdbSeason = seasonRefFrom(2, QString());
    entry.episodeOffset = 1;
    EpisodeMapping mapping;
    mapping.anidbSeason = 1;
    mapping.provider = ListProvider::Tvdb;
    mapping.externalSeason = 2;
    mapping.start = 1;
    mapping.end = 12;
    mapping.explicitMaps = parseExplicitMappings(QStringLiteral(";3-4;"));
    entry.mappings = {mapping};
    AnimeMappings mappings;
    mappings.fribb = FribbIndex::fromRows({row(11, 200, 50, 2, QStringLiteral("TV"))});
    mappings.lists = AnimeListsIndex::fromEntries({entry});
    PlaybackRef item;
    item.ids.tvdb = 50;
    item.season = 2;
    item.episode = 4;
    const MalTarget explicitHit = resolveMalTarget(mappings, item);
    QCOMPARE(explicitHit.malId, 200);
    QCOMPARE(explicitHit.episode, 3);

    item.episode = 2;
    QCOMPARE(resolveMalTarget(mappings, item).episode, 2);
}

void MalTests::packedListRoundTrip()
{
    const AnimeMappings mappings = sampleMappings();
    QCOMPARE(mappings.fribb.rowCount(), 3);
    PlaybackRef item;
    item.ids.tvdb = 50;
    item.season = 2;
    item.episode = 4;
    const MalTarget target = resolveMalTarget(mappings, item);
    QCOMPARE(target.malId, 200);
    QCOMPARE(target.episode, 3);

    PlaybackRef film;
    film.movie = true;
    film.ids.tmdb = 5;
    QCOMPARE(resolveMalTarget(mappings, film).malId, 9);
}

void MalTests::realMappingsFileIfPresent()
{
    const QByteArray path = qgetenv("MAL_MAPPINGS_FILE");
    if (path.isEmpty())
        QSKIP("set MAL_MAPPINGS_FILE to check a real anime-mappings.bin.gz");
    QFile file(QString::fromUtf8(path));
    QVERIFY(file.open(QIODevice::ReadOnly));
    AnimeMappings mappings;
    try {
        mappings = decodeAnimeMappings(file.readAll());
    } catch (const std::exception &ex) {
        QFAIL(ex.what());
    }
    // A current anime-mappings asset is ~40k Fribb rows. Unique TVDB series are
    // fewer because movies have none and split cours share one series id.
    const QByteArray summary = QStringLiteral("fribb rows %1 tvdb %2 lists tvdb %3")
                                   .arg(mappings.fribb.rowCount())
                                   .arg(mappings.fribb.tvdbSeriesCount())
                                   .arg(mappings.lists.tvdbSeriesCount())
                                   .toUtf8();
    QVERIFY2(mappings.fribb.rowCount() > 20000, summary.constData());
    QVERIFY2(mappings.fribb.tvdbSeriesCount() > 1000, summary.constData());
    QVERIFY2(mappings.lists.tvdbSeriesCount() > 1000, summary.constData());

    // Cowboy Bebop: TheTVDB 76885, MAL 1. Guards a zigzag/delta mix-up that
    // would still produce a plausible row count.
    PlaybackRef bebop;
    bebop.ids.tvdb = 76885;
    bebop.season = 1;
    bebop.episode = 1;
    const MalTarget target = resolveMalTarget(mappings, bebop);
    QCOMPARE(target.malId, 1);
    QCOMPARE(target.episode, 1);
}

void MalTests::buildQrImageSize()
{
    const QString text = QStringLiteral("http://192.168.1.20:8740/");
    const QImage image = buildQr(text);
    QVERIFY(!image.isNull());

    const QByteArray utf8 = text.toUtf8();
    const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(
        utf8.constData(), qrcodegen::QrCode::Ecc::MEDIUM);
    const int side = (qr.getSize() + kQrQuietModules * 2) * kQrModulePixels;
    QCOMPARE(image.width(), side);
    QCOMPARE(image.height(), side);
    // Quiet zone stays white; the finder module at the origin of the symbol is black.
    QCOMPARE(image.pixel(0, 0), qRgb(255, 255, 255));
    const int origin = kQrQuietModules * kQrModulePixels;
    QCOMPARE(image.pixel(origin, origin), qRgb(0, 0, 0));
    QCOMPARE(image.pixel(origin + kQrModulePixels - 1, origin + kQrModulePixels - 1), qRgb(0, 0, 0));
}

QTEST_GUILESS_MAIN(MalTests)
#include "tst_mal.moc"
