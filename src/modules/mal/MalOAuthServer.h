#pragma once

#include <QObject>
#include <QHash>

class QTcpServer;
class QTcpSocket;

namespace mal {

// Tiny HTTP server so a phone on the LAN can open a short URL, jump to MAL,
// and bounce the authorization code back to the Pi. The code itself is about
// a thousand characters, which is why the QR encodes the short local URL
// rather than the authorize URL.
class OAuthServer : public QObject {
    Q_OBJECT
public:
    explicit OAuthServer(QObject *parent = nullptr);
    ~OAuthServer() override;

    bool start(quint16 port, QString *error);
    void stop();
    bool isListening() const;
    void setAuthorizeUrl(const QString &url);

    // Called once the token exchange finishes, so the phone sees the result.
    void finish(QTcpSocket *socket, bool ok, const QString &message);

signals:
    void authorizationCode(const QString &code, const QString &state, QTcpSocket *socket);

private:
    void onNewConnection();
    void onReadyRead(QTcpSocket *socket);
    void respond(QTcpSocket *socket, int status, const QString &title, const QString &body, const QString &redirect = {});

    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    QString m_authorizeUrl;
};

} // namespace mal
