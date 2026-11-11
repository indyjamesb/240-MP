#pragma once

#include "MalScrobble.h"

#include <QObject>
#include <functional>

class QNetworkAccessManager;

namespace mal {

// MAL API v2 calls used for scrobbling. A transport failure is reported to the
// callback and never surfaced as a playback error.
class MalClient : public QObject {
    Q_OBJECT
public:
    explicit MalClient(QObject *parent = nullptr);

    void setAccessToken(const QString &token);
    // Single-flight refresh. The callback is true when a new access token is stored
    // by the owner and setAccessToken has been called.
    void setRefresher(const std::function<void(std::function<void(bool)>)> &refresher);

    void getSnapshot(int malId, const std::function<void(ListSnapshot, bool)> &done);
    void updateList(int malId, int progress, const QString &status,
                    const std::function<void(bool, int)> &done);
    void getUsername(const std::function<void(QString, bool)> &done);

private:
    void send(const QByteArray &method, const QString &path, const QByteArray &body,
              const QByteArray &contentType, bool retrying,
              const std::function<void(int, QByteArray)> &done);

    QNetworkAccessManager *m_nam = nullptr;
    QString m_token;
    std::function<void(std::function<void(bool)>)> m_refresh;
};

} // namespace mal
