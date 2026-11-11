#pragma once

#include "MalIndex.h"

#include <QObject>

class QNetworkAccessManager;

namespace mal {

// Downloads and caches anime-mappings.bin.gz. A failed download or a stale
// cache never throws into playback; callers just see an empty index.
class MappingsStore : public QObject {
    Q_OBJECT
public:
    explicit MappingsStore(const QString &dataRoot, QObject *parent = nullptr);

    const AnimeMappings &index() const { return m_index; }
    bool ready() const { return !m_index.isEmpty(); }

    void ensureLoaded();

signals:
    void loadFinished(bool ok);

private:
    void decodeFile(const QString &path);
    void download(bool conditional);
    void writeMeta(const QByteArray &etag);
    QByteArray storedEtag() const;
    qint64 lastCheckMs() const;

    QString m_dataRoot;
    QNetworkAccessManager *m_nam = nullptr;
    AnimeMappings m_index;
    bool m_loading = false;
    bool m_refreshing = false;
};

} // namespace mal
