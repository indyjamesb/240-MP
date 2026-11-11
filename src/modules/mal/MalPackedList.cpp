#include "MalPackedList.h"

#include <QHash>

#include <cstring>
#include <limits>
#include <stdexcept>
#include <zlib.h>

namespace mal {

namespace {

constexpr int kTypeInt = 1;
constexpr int kTypeIntDelta = 2;
constexpr int kTypeStr = 3;
constexpr int kTypeDict = 4;
constexpr int kTypeIntList = 5;
constexpr int kTypeStrList = 6;

[[noreturn]] void bad(const char *message)
{
    throw std::runtime_error(message);
}

qint64 unzigzag(quint64 n)
{
    if ((n & 1ull) == 0)
        return qint64(n >> 1);
    return -qint64(n >> 1) - 1;
}

class Reader {
public:
    Reader(const QByteArray &data, int start, int end)
        : m_data(data), m_pos(start), m_end(end)
    {
        if (m_pos < 0 || m_end > m_data.size() || m_pos > m_end)
            bad("anime mappings packed list is cut short");
    }

    int pos() const { return m_pos; }
    int end() const { return m_end; }

    quint8 byte()
    {
        if (m_pos >= m_end)
            bad("anime mappings packed list is cut short");
        return quint8(m_data.at(m_pos++));
    }

    quint64 varint()
    {
        quint64 n = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            const quint64 b = byte();
            if (shift == 63 && (b & 0x7e))
                bad("anime mappings varint is longer than 64 bits");
            n |= (b & 0x7f) << shift;
            if (b < 0x80)
                return n;
        }
        bad("anime mappings varint is longer than 64 bits");
    }

    qint64 signedInt() { return unzigzag(varint()); }

    QString text() { return string(int(varint())); }

    QString string(int length)
    {
        if (length < 0)
            bad("anime mappings packed list is cut short");
        const int start = skip(length);
        return QString::fromUtf8(m_data.constData() + start, length);
    }

    int skip(int length)
    {
        if (length < 0 || length > m_end - m_pos)
            bad("anime mappings packed list is cut short");
        const int start = m_pos;
        m_pos += length;
        return start;
    }

private:
    const QByteArray &m_data;
    int m_pos;
    int m_end;
};

struct Cell {
    enum Kind { Null, Int, Str, Ints, Strs } kind = Null;
    qint64 n = 0;
    QString s;
    QVector<qint64> ints;
    QStringList strs;
};

struct ColumnRef {
    int type = 0;
    int start = 0;
    int end = 0;
};

class Table {
public:
    Table() = default;
    Table(const QByteArray *data, int rows)
        : m_data(data), m_rows(rows) {}

    int rows() const { return m_rows; }
    void addColumn(const QString &name, ColumnRef ref) { m_columns.insert(name, ref); }

    QVector<Cell> column(const QString &name) const
    {
        QVector<Cell> values(m_rows);
        const auto it = m_columns.constFind(name);
        if (it == m_columns.cend())
            return values;
        const ColumnRef col = it.value();
        if (col.type < kTypeInt || col.type > kTypeStrList)
            return values;

        Reader r(*m_data, col.start, col.end);
        QStringList words;
        if (col.type == kTypeDict) {
            const quint64 count = r.varint();
            for (quint64 i = 0; i < count; ++i)
                words.append(r.text());
        }
        qint64 previous = 0;
        for (int i = 0; i < m_rows; ++i) {
            const quint64 tag = r.varint();
            if (tag == 0)
                continue;
            Cell cell;
            switch (col.type) {
            case kTypeInt:
                cell.kind = Cell::Int;
                cell.n = unzigzag(tag - 1);
                break;
            case kTypeIntDelta:
                previous += unzigzag(tag - 1);
                cell.kind = Cell::Int;
                cell.n = previous;
                break;
            case kTypeStr:
                cell.kind = Cell::Str;
                cell.s = r.string(int(tag - 1));
                break;
            case kTypeDict:
                if (tag > quint64(words.size()))
                    bad("anime mappings dictionary word is out of range");
                cell.kind = Cell::Str;
                cell.s = words.at(int(tag - 1));
                break;
            case kTypeIntList:
                cell.kind = Cell::Ints;
                cell.ints.reserve(int(tag - 1));
                for (quint64 k = tag - 1; k > 0; --k)
                    cell.ints.append(r.signedInt());
                break;
            case kTypeStrList:
                cell.kind = Cell::Strs;
                for (quint64 k = tag - 1; k > 0; --k)
                    cell.strs.append(r.text());
                break;
            default:
                break;
            }
            values[i] = cell;
        }
        if (r.pos() != r.end())
            bad("anime mappings column has unread bytes");
        return values;
    }

private:
    const QByteArray *m_data = nullptr;
    int m_rows = 0;
    QHash<QString, ColumnRef> m_columns;
};

QByteArray inflateIfGzip(const QByteArray &data)
{
    if (data.size() < 2 || quint8(data.at(0)) != 0x1f || quint8(data.at(1)) != 0x8b)
        return data;

    z_stream strm;
    std::memset(&strm, 0, sizeof(strm));
    if (inflateInit2(&strm, 16 + MAX_WBITS) != Z_OK)
        bad("anime mappings gzip header could not be read");
    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    strm.avail_in = uInt(data.size());

    QByteArray out;
    out.reserve(data.size() * 3);
    char buf[1 << 15];
    int rc = Z_OK;
    while (rc != Z_STREAM_END) {
        strm.next_out = reinterpret_cast<Bytef *>(buf);
        strm.avail_out = sizeof(buf);
        rc = inflate(&strm, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) {
            inflateEnd(&strm);
            bad("anime mappings gzip payload could not be inflated");
        }
        out.append(buf, int(sizeof(buf) - strm.avail_out));
    }
    inflateEnd(&strm);
    return out;
}

std::optional<qint64> cellInt(const Cell &cell)
{
    if (cell.kind != Cell::Int)
        return std::nullopt;
    return cell.n;
}

QString cellString(const Cell &cell)
{
    return cell.kind == Cell::Str ? cell.s : QString();
}

QVector<int> cellInts(const Cell &cell)
{
    QVector<int> out;
    if (cell.kind != Cell::Ints)
        return out;
    for (qint64 n : cell.ints) {
        if (n > 0 && n <= qint64(std::numeric_limits<int>::max()))
            out.append(int(n));
    }
    return out;
}

QStringList cellStrings(const Cell &cell)
{
    if (cell.kind != Cell::Strs)
        return {};
    QStringList out;
    for (const QString &s : cell.strs) {
        const QString trimmed = s.trimmed();
        if (!trimmed.isEmpty())
            out.append(trimmed);
    }
    return out;
}

int positiveId(const Cell &cell)
{
    const auto n = cellInt(cell);
    if (!n || *n <= 0 || *n > qint64(std::numeric_limits<int>::max()))
        return 0;
    return int(*n);
}

QVector<FribbRow> fribbRows(const Table *table)
{
    QVector<FribbRow> rows;
    if (!table)
        return rows;
    const auto type = table->column(QStringLiteral("type"));
    const auto anidb = table->column(QStringLiteral("anidb_id"));
    const auto mal = table->column(QStringLiteral("mal_id"));
    const auto tmdbTv = table->column(QStringLiteral("themoviedb_id.tv"));
    const auto tmdbMovies = table->column(QStringLiteral("themoviedb_id.movie"));
    const auto imdb = table->column(QStringLiteral("imdb_id"));
    const auto tvdb = table->column(QStringLiteral("tvdb_id"));
    const auto tvdbSeason = table->column(QStringLiteral("season.tvdb"));
    const auto tmdbSeason = table->column(QStringLiteral("season.tmdb"));
    rows.reserve(table->rows());
    for (int i = 0; i < table->rows(); ++i) {
        FribbRow row;
        row.type = cellString(type.value(i));
        row.anidbId = positiveId(anidb.value(i));
        row.malId = positiveId(mal.value(i));
        row.tmdbTvId = positiveId(tmdbTv.value(i));
        row.tmdbMovieIds = cellInts(tmdbMovies.value(i));
        row.imdbIds = cellStrings(imdb.value(i));
        row.tvdbId = positiveId(tvdb.value(i));
        if (auto season = cellInt(tvdbSeason.value(i)))
            row.tvdbSeason = int(*season);
        if (auto season = cellInt(tmdbSeason.value(i)))
            row.tmdbSeason = int(*season);
        rows.append(row);
    }
    return rows;
}

QHash<int, QVector<EpisodeMapping>> episodeMappings(const Table *table)
{
    QHash<int, QVector<EpisodeMapping>> byAnidb;
    if (!table)
        return byAnidb;
    const auto anidb = table->column(QStringLiteral("anidbid"));
    const auto anidbSeason = table->column(QStringLiteral("anidbseason"));
    const auto tvdbSeason = table->column(QStringLiteral("tvdbseason"));
    const auto tmdbSeason = table->column(QStringLiteral("tmdbseason"));
    const auto start = table->column(QStringLiteral("start"));
    const auto end = table->column(QStringLiteral("end"));
    const auto offset = table->column(QStringLiteral("offset"));
    const auto text = table->column(QStringLiteral("text"));
    for (int i = 0; i < table->rows(); ++i) {
        const int anidbId = positiveId(anidb.value(i));
        const auto season = cellInt(anidbSeason.value(i));
        if (anidbId <= 0 || !season)
            continue;
        const auto explicitMaps = parseExplicitMappings(cellString(text.value(i)));
        const int startN = cellInt(start.value(i)) ? int(*cellInt(start.value(i))) : 0;
        const auto endN = cellInt(end.value(i));
        const int off = cellInt(offset.value(i)) ? int(*cellInt(offset.value(i))) : 0;
        const struct ProviderSeason { ListProvider provider; std::optional<qint64> season; } providers[] = {
            {ListProvider::Tvdb, cellInt(tvdbSeason.value(i))},
            {ListProvider::Tmdb, cellInt(tmdbSeason.value(i))},
        };
        for (const auto &provider : providers) {
            if (!provider.season)
                continue;
            EpisodeMapping mapping;
            mapping.anidbSeason = int(*season);
            mapping.provider = provider.provider;
            mapping.externalSeason = int(*provider.season);
            if (cellInt(start.value(i)))
                mapping.start = startN;
            if (endN)
                mapping.end = int(*endN);
            mapping.offset = off;
            mapping.explicitMaps = explicitMaps;
            byAnidb[anidbId].append(mapping);
        }
    }
    return byAnidb;
}

QVector<AnimeListEntry> animeListEntries(const Table *table, const QHash<int, QVector<EpisodeMapping>> &mappings)
{
    QVector<AnimeListEntry> entries;
    if (!table)
        return entries;
    const auto anidb = table->column(QStringLiteral("anidbid"));
    const auto tvdb = table->column(QStringLiteral("tvdbid"));
    const auto defaultSeason = table->column(QStringLiteral("defaulttvdbseason"));
    const auto defaultSeasonText = table->column(QStringLiteral("defaulttvdbseason_text"));
    const auto episodeOffset = table->column(QStringLiteral("episodeoffset"));
    const auto tmdbTv = table->column(QStringLiteral("tmdbtv"));
    const auto tmdbSeason = table->column(QStringLiteral("tmdbseason"));
    const auto tmdbSeasonText = table->column(QStringLiteral("tmdbseason_text"));
    const auto tmdbOffset = table->column(QStringLiteral("tmdboffset"));
    entries.reserve(table->rows());
    for (int i = 0; i < table->rows(); ++i) {
        const int anidbId = positiveId(anidb.value(i));
        if (anidbId <= 0)
            continue;
        AnimeListEntry entry;
        entry.anidbId = anidbId;
        entry.tvdbId = positiveId(tvdb.value(i));
        entry.defaultTvdbSeason = seasonRefFrom(cellInt(defaultSeason.value(i)), cellString(defaultSeasonText.value(i)));
        if (auto off = cellInt(episodeOffset.value(i)))
            entry.episodeOffset = int(*off);
        entry.tmdbTvId = positiveId(tmdbTv.value(i));
        entry.tmdbSeason = seasonRefFrom(cellInt(tmdbSeason.value(i)), cellString(tmdbSeasonText.value(i)));
        if (auto off = cellInt(tmdbOffset.value(i)))
            entry.tmdbOffset = int(*off);
        entry.mappings = mappings.value(anidbId);
        entries.append(entry);
    }
    return entries;
}

} // namespace

AnimeMappings decodeAnimeMappings(const QByteArray &gzipOrRaw)
{
    const QByteArray data = inflateIfGzip(gzipOrRaw);
    Reader r(data, 0, data.size());
    const char magic[4] = {'A', 'M', 'A', 'P'};
    for (char expected : magic) {
        if (r.byte() != quint8(expected))
            bad("not an anime mappings packed list");
    }
    if (r.byte() != 1)
        bad("anime mappings format version is not 1");

    QHash<QString, Table> tables;
    const quint64 tableCount = r.varint();
    for (quint64 t = 0; t < tableCount; ++t) {
        const QString name = r.text();
        const int rows = int(r.varint());
        Table table(&data, rows);
        const quint64 columns = r.varint();
        for (quint64 c = 0; c < columns; ++c) {
            const QString column = r.text();
            const int type = r.byte();
            const int length = int(r.varint());
            ColumnRef ref;
            ref.type = type;
            ref.start = r.skip(length);
            ref.end = r.pos();
            table.addColumn(column, ref);
        }
        tables.insert(name, table);
    }
    if (r.pos() != data.size())
        bad("bytes follow the last anime mappings table");

    const auto tablePtr = [&tables](const QString &name) -> const Table * {
        const auto it = tables.constFind(name);
        return it == tables.cend() ? nullptr : &it.value();
    };
    const Table *fribb = tablePtr(QStringLiteral("fribb"));
    const Table *lists = tablePtr(QStringLiteral("anime_lists"));
    const Table *listMaps = tablePtr(QStringLiteral("anime_lists_mappings"));

    // Table objects hold a reference to `data`. Copy the rows out before
    // `data` goes out of scope at the end of this function — column() reads
    // it, and the returned indexes own their rows.
    AnimeMappings mappings;
    mappings.fribb = FribbIndex::fromRows(fribbRows(fribb));
    mappings.lists = AnimeListsIndex::fromEntries(animeListEntries(lists, episodeMappings(listMaps)));
    return mappings;
}

} // namespace mal
