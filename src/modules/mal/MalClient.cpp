#include "MalClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace mal {

namespace {
constexpr auto kApi = "https://api.myanimelist.net/v2";

ListSnapshot snapshotFromJson(const QJsonObject &body)
{
    ListSnapshot snap;
    const int count = body.value(QStringLiteral("num_episodes")).toInt();
    if (count > 0)
        snap.episodeCount = count;
    const QJsonObject list = body.value(QStringLiteral("my_list_status")).toObject();
    if (list.isEmpty())
        return snap;
    snap.onList = true;
    snap.progress = qMax(0, list.value(QStringLiteral("num_watched_episodes")).toInt());
    snap.completed = list.value(QStringLiteral("status")).toString() == QLatin1String("completed");
    return snap;
}
}

MalClient::MalClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void MalClient::setAccessToken(const QString &token)
{
    m_token = token;
}

void MalClient::setRefresher(const std::function<void(std::function<void(bool)>)> &refresher)
{
    m_refresh = refresher;
}

void MalClient::send(const QByteArray &method, const QString &path, const QByteArray &body,
                     const QByteArray &contentType, bool retrying,
                     const std::function<void(int, QByteArray)> &done)
{
    QNetworkRequest req(QUrl(QString::fromLatin1(kApi) + path));
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toUtf8());
    req.setTransferTimeout(20000);
    if (!contentType.isEmpty())
        req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    QNetworkReply *reply = nullptr;
    if (method == "GET")
        reply = m_nam->get(req);
    else if (method == "PUT")
        reply = m_nam->put(req, body);
    else
        reply = m_nam->sendCustomRequest(req, method, body);

    connect(reply, &QNetworkReply::finished, this, [this, reply, method, path, body, contentType, retrying, done]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool networkFail = reply->error() != QNetworkReply::NoError && status == 0;
        if (status == 401 && !retrying && m_refresh) {
            m_refresh([this, method, path, body, contentType, done](bool refreshed) {
                if (!refreshed) {
                    done(401, {});
                    return;
                }
                send(method, path, body, contentType, true, done);
            });
            return;
        }
        if (networkFail) {
            qWarning("[MAL] request failed: %s", qPrintable(reply->errorString()));
            done(0, {});
            return;
        }
        done(status, reply->readAll());
    });
}

void MalClient::getSnapshot(int malId, const std::function<void(ListSnapshot, bool)> &done)
{
    const QString path = QStringLiteral("/anime/%1?fields=num_episodes,my_list_status").arg(malId);
    send("GET", path, {}, {}, false, [done](int status, const QByteArray &body) {
        if (status == 404) {
            done(ListSnapshot{}, false);
            return;
        }
        if (status < 200 || status >= 300) {
            qWarning("[MAL] list snapshot failed (HTTP %d)", status);
            done(ListSnapshot{}, false);
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        done(snapshotFromJson(obj), true);
    });
}

void MalClient::updateList(int malId, int progress, const QString &status,
                           const std::function<void(bool, int)> &done)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("num_watched_episodes"), QString::number(progress));
    form.addQueryItem(QStringLiteral("status"), status);
    const QString path = QStringLiteral("/anime/%1/my_list_status").arg(malId);
    send("PUT", path, form.query(QUrl::FullyEncoded).toUtf8(),
         "application/x-www-form-urlencoded", false,
         [done](int http, const QByteArray &) {
             if (http < 200 || http >= 300)
                 qWarning("[MAL] list update failed (HTTP %d)", http);
             done(http >= 200 && http < 300, http);
         });
}

void MalClient::getUsername(const std::function<void(QString, bool)> &done)
{
    send("GET", QStringLiteral("/users/@me"), {}, {}, false, [done](int status, const QByteArray &body) {
        if (status < 200 || status >= 300) {
            done({}, false);
            return;
        }
        done(QJsonDocument::fromJson(body).object().value(QStringLiteral("name")).toString(), true);
    });
}

} // namespace mal
