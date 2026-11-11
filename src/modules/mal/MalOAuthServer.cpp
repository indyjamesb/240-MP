#include "MalOAuthServer.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

namespace mal {

namespace {
QString htmlEscape(QString text)
{
    text.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    text.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    text.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return text;
}
}

OAuthServer::OAuthServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &OAuthServer::onNewConnection);
}

OAuthServer::~OAuthServer()
{
    stop();
}

bool OAuthServer::start(quint16 port, QString *error)
{
    stop();
    if (!m_server->listen(QHostAddress::Any, port)) {
        if (error)
            *error = m_server->errorString();
        return false;
    }
    return true;
}

void OAuthServer::stop()
{
    m_buffers.clear();
    const auto sockets = findChildren<QTcpSocket *>();
    for (QTcpSocket *socket : sockets)
        socket->disconnectFromHost();
    m_server->close();
}

void OAuthServer::pauseAccepting()
{
    if (m_server->isListening())
        m_server->pauseAccepting();
}

bool OAuthServer::isListening() const
{
    return m_server->isListening();
}

void OAuthServer::setAuthorizeUrl(const QString &url)
{
    m_authorizeUrl = url;
}

void OAuthServer::finish(QTcpSocket *socket, bool ok, const QString &message)
{
    if (!socket)
        return;
    respond(socket, ok ? 200 : 400, ok ? QStringLiteral("Connected") : QStringLiteral("Not connected"),
            message);
}

void OAuthServer::onNewConnection()
{
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        socket->setParent(this);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void OAuthServer::onReadyRead(QTcpSocket *socket)
{
    if (!socket || socket->property("malHeld").toBool())
        return;
    m_buffers[socket] += socket->readAll();
    QByteArray &buf = m_buffers[socket];
    if (!buf.contains("\r\n\r\n")) {
        if (buf.size() > 65536) {
            respond(socket, 414, QStringLiteral("Request too large"),
                    QStringLiteral("The authorization response was too large to read."));
        }
        return;
    }
    const QByteArray requestLine = buf.left(buf.indexOf("\r\n"));
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2 || parts.at(0) != "GET") {
        respond(socket, 405, QStringLiteral("Unsupported"), QStringLiteral("Only GET is accepted."));
        return;
    }
    const QUrl url(QString::fromUtf8(parts.at(1)));
    const QString path = url.path().isEmpty() ? QStringLiteral("/") : url.path();
    if (path == QLatin1String("/") || path == QLatin1String("/a")) {
        respond(socket, 200, QStringLiteral("Continue to MyAnimeList"),
                QStringLiteral("Opening MyAnimeList so you can approve 240-MP. If nothing happens, tap the link."),
                m_authorizeUrl);
        return;
    }
    if (path == QLatin1String("/callback")) {
        const QUrlQuery query(url);
        const QString code = query.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded);
        const QString state = query.queryItemValue(QStringLiteral("state"), QUrl::FullyDecoded);
        const QString error = query.queryItemValue(QStringLiteral("error"), QUrl::FullyDecoded);
        if (!error.isEmpty() || code.isEmpty()) {
            respond(socket, 400, QStringLiteral("Not approved"),
                    error.isEmpty() ? QStringLiteral("MyAnimeList did not return a code.")
                                    : QStringLiteral("MyAnimeList declined the request."));
            return;
        }
        socket->setProperty("malHeld", true);
        m_buffers.remove(socket);
        emit authorizationCode(code, state, socket);
        return;
    }
    respond(socket, 404, QStringLiteral("Not found"), QStringLiteral("Unknown path."));
}

void OAuthServer::respond(QTcpSocket *socket, int status, const QString &title, const QString &body,
                          const QString &redirect)
{
    if (!socket)
        return;
    const QString refresh = redirect.isEmpty()
        ? QString()
        : QStringLiteral("<meta http-equiv=\"refresh\" content=\"0;url=%1\">").arg(htmlEscape(redirect));
    const QString link = redirect.isEmpty()
        ? QString()
        : QStringLiteral("<p><a href=\"%1\">Continue to MyAnimeList</a></p>").arg(htmlEscape(redirect));
    const QByteArray html = QStringLiteral(
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">%1<title>%2</title></head>"
        "<body style=\"font-family:sans-serif;background:#111;color:#eee;padding:2rem\">"
        "<h1>%2</h1><p>%3</p>%4</body></html>")
        .arg(refresh, htmlEscape(title), htmlEscape(body), link)
        .toUtf8();
    const QByteArray reason = status == 200 ? "OK" : status == 404 ? "Not Found" : "Error";
    const QByteArray header = QByteArray("HTTP/1.1 ") + QByteArray::number(status) + ' ' + reason
        + "\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: "
        + QByteArray::number(html.size()) + "\r\nConnection: close\r\n\r\n";
    socket->write(header);
    socket->write(html);
    socket->disconnectFromHost();
    m_buffers.remove(socket);
}

} // namespace mal
