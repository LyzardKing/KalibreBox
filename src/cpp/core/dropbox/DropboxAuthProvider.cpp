#include "core/dropbox/DropboxAuthProvider.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace KalibreBox {

namespace {

// 10 minutes to complete the browser flow.
constexpr int loginTimeoutMs = 10 * 60 * 1000;
constexpr qsizetype stateBytes = 16;

const char *const authorizeEndpoint = "https://www.dropbox.com/oauth2/authorize";
const char *const tokenEndpointUrl = "https://api.dropbox.com/oauth2/token";

const char *const successHtml = R"(HTTP/1.1 200 OK
Content-Type: text/html; charset=utf-8
Connection: close

<html><body><h3>KalibreBox</h3><p>Signed in. You can close this tab.</p></body></html>
)";

const char *const failureHtml = R"(HTTP/1.1 200 OK
Content-Type: text/html; charset=utf-8
Connection: close

<html><body><h3>KalibreBox</h3><p>Authorization was not completed.</p></body></html>
)";

QString compileTimeAppKey()
{
#ifdef KALIBREBOX_DROPBOX_APP_KEY
    return QString::fromUtf8(KALIBREBOX_DROPBOX_APP_KEY);
#else
    return QString();
#endif
}

QString generateState()
{
    QByteArray raw(stateBytes, Qt::Uninitialized);
    QRandomGenerator::system()->generate(raw.begin(), raw.end());
    return QString::fromUtf8(raw.toHex());
}

void respondAndClose(QTcpSocket *socket, const char *response)
{
    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

}

DropboxAuthProvider::DropboxAuthProvider(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_nam(new QNetworkAccessManager(this))
{
}

DropboxAuthProvider::~DropboxAuthProvider()
{
    cancelListening();
}

QString DropboxAuthProvider::appKey() const
{
    if (!m_appKey.isEmpty())
        return m_appKey;
    if (m_settings) {
        const QString fromSettings =
            m_settings->value(QStringLiteral("DropboxPrefs/app_key")).toString();
        if (!fromSettings.isEmpty())
            return fromSettings;
    }

    const QString fromEnv = qEnvironmentVariable("KALIBREBOX_DROPBOX_APP_KEY");
    if (!fromEnv.isEmpty())
        return fromEnv;

    return compileTimeAppKey();
}

void DropboxAuthProvider::setAppKey(const QString &appKey)
{
    m_appKey = appKey;
}

void DropboxAuthProvider::setCredential(const DropboxCredential &credential)
{
    m_credential = credential;
    if (m_settings)
        DropboxCredential::save(m_settings, credential);
}

void DropboxAuthProvider::logout()
{
    m_credential.reset();
    if (m_settings)
        DropboxCredential::clear(m_settings);
}

void DropboxAuthProvider::startLogin()
{
    if (appKey().isEmpty()) {
        emit authFailed(QStringLiteral(
            "No Dropbox app key configured. Add dropbox.key=<your key> to "
            "local.properties, or set KALIBREBOX_DROPBOX_APP_KEY."));
        return;
    }

    cancelLogin();
    m_authenticating = true;
    m_pkce = Pkce::generate();
    m_state = generateState();

    m_redirectServer = new QTcpServer(this);
    if (!m_redirectServer->listen(QHostAddress::LocalHost)) {
        const QString error = QStringLiteral("Could not open a local redirect port: %1")
                                  .arg(m_redirectServer->errorString());
        emit authFailed(error);
        cancelLogin();
        return;
    }
    m_redirectUri =
        QStringLiteral("http://127.0.0.1:%1/callback").arg(m_redirectServer->serverPort());

    // Abort the whole flow if the user never returns from the browser.
    QTimer::singleShot(loginTimeoutMs, this, [this]() {
        if (m_authenticating) {
            cancelLogin();
            emit authFailed(QStringLiteral(
                "Authorization timed out. Please try again."));
        }
    });

    connect(m_redirectServer, &QTcpServer::newConnection, this, [this]() {
        while (m_redirectServer && m_redirectServer->hasPendingConnections()) {
            QTcpSocket *socket = m_redirectServer->nextPendingConnection();
            if (!socket)
                continue;
            connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                // The first line is "GET /callback?code=..&state=.. HTTP/1.1".
                if (!socket->canReadLine())
                    return;
                const QString requestLine = QString::fromUtf8(socket->readLine());
                socket->readAll(); // discard the remaining request bytes

                const int firstSpace = requestLine.indexOf(u' ');
                const int secondSpace = requestLine.indexOf(u' ', firstSpace + 1);
                if (firstSpace < 0 || secondSpace < 0) {
                    respondAndClose(socket, failureHtml);
                    cancelLogin();
                    emit authFailed(QStringLiteral(
                        "Malformed authorization response."));
                    return;
                }
                const QString target =
                    requestLine.mid(firstSpace + 1, secondSpace - firstSpace - 1);
                const QUrl url(QStringLiteral("http://127.0.0.1") + target);
                const QUrlQuery urlQuery(url.query());

                const QString authCode = urlQuery.queryItemValue(QStringLiteral("code"));
                const QString state = urlQuery.queryItemValue(QStringLiteral("state"));
                QString error = urlQuery.queryItemValue(QStringLiteral("error_description"));
                if (error.isEmpty())
                    error = urlQuery.queryItemValue(QStringLiteral("error"));

                if (!error.isEmpty()) {
                    respondAndClose(socket, failureHtml);
                    cancelLogin();
                    emit authFailed(error);
                    return;
                }
                if (!m_pkce || state != m_state || authCode.isEmpty()) {
                    respondAndClose(socket, failureHtml);
                    cancelLogin();
                    emit authFailed(QStringLiteral(
                        "Unexpected or incomplete authorization response."));
                    return;
                }

                respondAndClose(socket, successHtml);
                // The redirect URI (incl. port) must be identical in the exchange.
                const QString redirectUri = m_redirectUri;
                const QString verifier = m_pkce->verifier;
                m_redirectServer->close();

                exchangeAuthorizationCode(
                    authCode, verifier, redirectUri,
                    [this](std::optional<DropboxCredential>, const QString &exchangeError) {
                        cancelLogin();
                        if (!exchangeError.isEmpty()) {
                            emit authFailed(exchangeError);
                        }
                        // On success exchangeAuthorizationCode already emitted
                        // authSucceeded.
                    });
            });
        }
    });

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("client_id"), appKey());
    query.addQueryItem(QStringLiteral("code_challenge"), m_pkce->challenge);
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("state"), m_state);
    query.addQueryItem(QStringLiteral("redirect_uri"), m_redirectUri);
    query.addQueryItem(QStringLiteral("token_access_type"), QStringLiteral("offline"));

    QUrl url(authorizeEndpoint);
    url.setQuery(query);
    QDesktopServices::openUrl(url);
}

void DropboxAuthProvider::cancelListening()
{
    if (m_redirectServer) {
        m_redirectServer->close();
        m_redirectServer->deleteLater();
        m_redirectServer = nullptr;
    }
}

void DropboxAuthProvider::cancelLogin()
{
    cancelListening();
    m_pkce.reset();
    m_state.clear();
    m_redirectUri.clear();
    m_authenticating = false;
}

void DropboxAuthProvider::exchangeAuthorizationCode(
    const QString &code, const QString &codeVerifier, const QString &redirectUri,
    const std::function<void(std::optional<DropboxCredential>, const QString &)> &callback)
{
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("code"), code);
    params.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    params.addQueryItem(QStringLiteral("client_id"), appKey());
    params.addQueryItem(QStringLiteral("redirect_uri"), redirectUri);
    params.addQueryItem(QStringLiteral("code_verifier"), codeVerifier);
    const QByteArray body = params.query(QUrl::FullyEncoded).toUtf8();

    QNetworkRequest request{QUrl{tokenEndpointUrl}};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));

    QNetworkReply *reply = m_nam->post(request, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        reply->deleteLater();
        const int statusCode =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        if (statusCode != 200) {
            callback(std::nullopt,
                     QStringLiteral("Token exchange failed (HTTP %1): %2")
                         .arg(statusCode)
                         .arg(QString::fromUtf8(payload.left(512))));
            return;
        }
        const QJsonDocument document = QJsonDocument::fromJson(payload);
        if (!document.isObject()) {
            callback(std::nullopt, QStringLiteral("Malformed token response"));
            return;
        }
        const QJsonObject object = document.object();
        DropboxCredential credential;
        credential.accessToken = object.value(QStringLiteral("access_token")).toString();
        credential.refreshToken = object.value(QStringLiteral("refresh_token")).toString();
        credential.accountId = object.value(QStringLiteral("account_id")).toString();
        const qint64 expiresIn = object.value(QStringLiteral("expires_in")).toInt(0);
        if (expiresIn > 0)
            credential.expiresAt = QDateTime::currentSecsSinceEpoch() + expiresIn;

        if (!credential.hasAccessToken()) {
            callback(std::nullopt, QStringLiteral("Token response had no access token"));
            return;
        }
        setCredential(credential);
        callback(credential, QString());
        emit authSucceeded(credential);
    });
}

void DropboxAuthProvider::refreshAccessToken(
    const std::function<void(std::optional<DropboxCredential>, const QString &)> &callback)
{
    if (!m_credential || !m_credential->hasRefreshToken()) {
        callback(std::nullopt, QStringLiteral("No refresh token available"));
        return;
    }

    QUrlQuery params;
    params.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    params.addQueryItem(QStringLiteral("refresh_token"), m_credential->refreshToken);
    params.addQueryItem(QStringLiteral("client_id"), appKey());
    const QByteArray body = params.query(QUrl::FullyEncoded).toUtf8();

    QNetworkRequest request{QUrl{tokenEndpointUrl}};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));

    QNetworkReply *reply = m_nam->post(request, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        reply->deleteLater();
        const int statusCode =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        if (statusCode != 200) {
            callback(std::nullopt,
                     QStringLiteral("Token refresh failed (HTTP %1): %2")
                         .arg(statusCode)
                         .arg(QString::fromUtf8(payload.left(512))));
            return;
        }
        const QJsonDocument document = QJsonDocument::fromJson(payload);
        if (!document.isObject()) {
            callback(std::nullopt, QStringLiteral("Malformed refresh response"));
            return;
        }
        DropboxCredential fresh = *m_credential;
        fresh.accessToken = document.object().value(QStringLiteral("access_token")).toString();
        const qint64 expiresIn =
            document.object().value(QStringLiteral("expires_in")).toInt(0);
        if (expiresIn > 0)
            fresh.expiresAt = QDateTime::currentSecsSinceEpoch() + expiresIn;

        if (!fresh.hasAccessToken()) {
            callback(std::nullopt, QStringLiteral("Refresh response had no access token"));
            return;
        }
        setCredential(fresh);
        callback(fresh, QString());
    });
}

void DropboxAuthProvider::ensureFreshToken(
    const std::function<void(bool, const QString &)> &callback)
{
    if (!m_credential || !m_credential->hasAccessToken()) {
        callback(false, QStringLiteral("Not signed in to Dropbox"));
        return;
    }
    if (m_credential->hasRefreshToken() && m_credential->needsRefresh()) {
        refreshAccessToken([callback](std::optional<DropboxCredential> refreshed,
                                      const QString &error) {
            callback(refreshed.has_value(), error);
        });
        return;
    }
    callback(true, QString());
}

}
