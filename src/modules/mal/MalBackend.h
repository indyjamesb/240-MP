#pragma once

#include "MalIndex.h"
#include "MalProgressHook.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QVariantMap>

#include <functional>
#include <optional>

class PlexBackend;
class EmbyBackend;
class JellyfinBackend;
class QTcpSocket;
class QTimer;

namespace mal {
class MappingsStore;
class OAuthServer;
class MalClient;
}

// MyAnimeList account and scrobbler.
//
// Self-contained so upstream merges only touch the one-line timeline hooks,
// the registerModule call, and the build file. Login is a LAN OAuth callback
// plus a code-file fallback, because a CRT remote cannot type MAL's
// ~1000-character authorization code. See docs/MAL.md.
class MalBackend : public QObject, public mal::ProgressSink {
    Q_OBJECT
    Q_PROPERTY(QString phase READ phase NOTIFY updated)
    Q_PROPERTY(QString statusLine READ statusLine NOTIFY updated)
    Q_PROPERTY(QString username READ username NOTIFY updated)
    Q_PROPERTY(QString redirectUri READ redirectUri NOTIFY updated)
    Q_PROPERTY(QString shortUrl READ shortUrl NOTIFY updated)
    Q_PROPERTY(QString clientHint READ clientHint NOTIFY updated)
    Q_PROPERTY(QString qrImageUrl READ qrImageUrl NOTIFY updated)
    Q_PROPERTY(QString codeFileName READ codeFileName CONSTANT)

public:
    explicit MalBackend(const QString &dataRoot, PlexBackend *plex, EmbyBackend *emby,
                        JellyfinBackend *jellyfin, QObject *parent = nullptr);
    ~MalBackend() override;

    QString phase() const { return m_phase; }
    QString statusLine() const { return m_statusLine; }
    QString username() const { return m_username; }
    QString redirectUri() const;
    QString shortUrl() const;
    QString clientHint() const;
    QString qrImageUrl() const { return m_qrImageUrl; }
    QString codeFileName() const { return QStringLiteral("mal_oauth_code.txt"); }

    Q_INVOKABLE QString get_auth_state();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void connectAccount();
    Q_INVOKABLE void cancelConnect();
    Q_INVOKABLE void submitCodeFile();

public slots:
    void onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value);

signals:
    void authStateChanged();
    void updated();

protected:
    void observe(const mal::ProgressSample &sample) override;

private:
    struct ItemState {
        QString service;
        QString itemId;
        qint64 positionMs = 0;
        qint64 durationMs = 0;
        qint64 runtimeMs = 0;
        bool runtimeKnown = false;
        bool haveIdentity = false;
        bool playable = false;
        bool movie = false;
        std::optional<int> season;
        std::optional<int> episode;
        mal::ExternalIds ids;
        bool inflight = false;
        bool waitingMappings = false;
        int failures = 0;
        qint64 retryNotBefore = 0;
    };

    void reloadSettings();
    void loadTokens();
    void saveTokens() const;
    void clearTokens();
    bool hasCredentials() const;
    void publish();
    void setPhase(const QString &phase, const QString &status);
    QString lanHost() const;
    void beginExchange(const QString &code, QTcpSocket *socket);
    void refreshTokens(const std::function<void(bool)> &done);
    void postToken(const QList<QPair<QString, QString>> &fields,
                   const std::function<void(bool, int)> &done);
    void buildQr(const QString &text);
    void noteSample(const mal::ProgressSample &sample);
    void step(const QString &key);
    void finishItem(const QString &key, bool done, const QString &reason);
    void fetchIdentity(const QString &key);
    mal::PlaybackRef playbackRef(const ItemState &state) const;

    void applyTokenResponse(const QJsonObject &body, bool keepRefresh);

    QString m_dataRoot;
    PlexBackend *m_plex = nullptr;
    EmbyBackend *m_emby = nullptr;
    JellyfinBackend *m_jellyfin = nullptr;
    mal::MappingsStore *m_mappings = nullptr;
    mal::OAuthServer *m_oauth = nullptr;
    mal::MalClient *m_client = nullptr;
    QTimer *m_loginTimer = nullptr;

    bool m_enabled = false;
    QString m_clientId;
    QString m_clientSecret;
    int m_port = 8740;
    QString m_redirectHost;

    QString m_accessToken;
    QString m_refreshToken;
    qint64 m_expiresAt = 0;
    QString m_username;
    bool m_refreshing = false;
    QList<std::function<void(bool)>> m_refreshWaiters;

    QString m_phase = QStringLiteral("need_client");
    QString m_statusLine;
    QString m_verifier;
    QString m_state;
    QString m_qrImageUrl;

    QHash<QString, ItemState> m_items;
    QSet<QString> m_done;
};
