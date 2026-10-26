#include "core/dropbox/DropboxAuthProvider.h"

#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
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

// Lets the Android intent side (JNI) find the provider instance that is
// currently in the middle of a native sign-in.
QVector<QPointer<DropboxAuthProvider>> &providerRegistry()
{
    static QVector<QPointer<DropboxAuthProvider>> registry;
    return registry;
}

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

// Dropbox requires the redirect_uri to be pre-registered in the App Console,
// so the loopback port must be stable. Overridable via env/settings/port.
int oauthPort()
{
#ifdef KALIBREBOX_DROPBOX_OAUTH_PORT
    const int compiled = QString::fromUtf8(KALIBREBOX_DROPBOX_OAUTH_PORT).toInt();
    if (compiled > 0)
        return compiled;
#endif
    return 61196;
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
    providerRegistry().append(QPointer<DropboxAuthProvider>(this));
}

DropboxAuthProvider::~DropboxAuthProvider()
{
    providerRegistry().removeAll(this);
    providerRegistry().removeAll(nullptr);
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

void DropboxAuthProvider::dispatchAuthRedirect(const QString &url)
{
    for (const auto &provider : providerRegistry()) {
        if (provider) {
            // Called from the Android UI thread; the provider lives on the Qt thread.
            QMetaObject::invokeMethod(
                provider.data(),
                [provider, url]() {
                    if (provider)
                        provider->handleAuthRedirect(QUrl(url));
                },
                Qt::QueuedConnection);
            return;
        }
    }
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

    // Abort the whole flow if it is never completed.
    QTimer::singleShot(loginTimeoutMs, this, [this]() {
        if (m_authenticating) {
            cancelLogin();
            emit authFailed(QStringLiteral(
                "Authorization timed out. Please try again."));
        }
    });

    const QString mode = oauthMode();
    if (mode == QLatin1String("loopback"))
        startLoopbackLogin();
    else if (mode == QLatin1String("native")) {
#ifdef Q_OS_ANDROID
        startNativeLogin();
#else
        emit authFailed(QStringLiteral(
            "Native db-<appkey>:// sign-in needs the Android build. Choose the "
            "code or loopback mode on this platform instead."));
#endif
    } else
        startDirectCodeLogin();
}

void DropboxAuthProvider::startLoopbackLogin()
{
    m_redirectServer = new QTcpServer(this);
    // The port is registered in the Dropbox App Console, so only this one
    // port may be used (changing it would break the exact redirect_uri match).
    const int port = oauthPort();
    if (!m_redirectServer->listen(QHostAddress::LocalHost, port)) {
        const QString error = QStringLiteral(
            "Could not listen on http://127.0.0.1:%1 for the OAuth redirect: %2. "
            "Another process may be occupying the port; free it and retry.")
                                  .arg(port)
                                  .arg(m_redirectServer->errorString());
        emit authFailed(error);
        cancelLogin();
        return;
    }
    m_redirectUri =
        QStringLiteral("http://127.0.0.1:%1/callback").arg(m_redirectServer->serverPort());
    m_state = generateState();

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

void DropboxAuthProvider::startDirectCodeLogin()
{
    m_directCodeMode = true;

    // No redirect_uri: Dropbox then displays the authorization code on screen
    // for the user to copy and paste into the app. This is what makes sign-in
    // possible without any pre-registered redirect URI.
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("client_id"), appKey());
    query.addQueryItem(QStringLiteral("code_challenge"), m_pkce->challenge);
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("token_access_type"), QStringLiteral("offline"));

    QUrl url(authorizeEndpoint);
    url.setQuery(query);
    QDesktopServices::openUrl(url);
    emit codeRequested();
}

void DropboxAuthProvider::completeDirectSignIn(const QString &code)
{
    if (!m_directCodeMode || !m_pkce) {
        emit authFailed(QStringLiteral(
            "No sign-in in progress — press 'Sign in' first."));
        return;
    }

    // Accept a bare code or a whole pasted URL/parameter line.
    QString trimmed = code.trimmed();
    const int codeIndex = trimmed.indexOf(QStringLiteral("code="));
    if (codeIndex >= 0) {
        QString extracted = trimmed.mid(codeIndex + 5);
        const int amp = extracted.indexOf(u'&');
        if (amp >= 0)
            extracted.truncate(amp);
        extracted = QUrl::fromPercentEncoding(extracted.toUtf8());
        trimmed = extracted.trimmed();
    }

    exchangeAuthorizationCode(
        trimmed, m_pkce->verifier, QStringLiteral(""),
        [this](std::optional<DropboxCredential>, const QString &exchangeError) {
            cancelLogin();
            if (!exchangeError.isEmpty())
                emit authFailed(exchangeError);
            // On success exchangeAuthorizationCode already emitted
            // authSucceeded.
        });
}

void DropboxAuthProvider::startNativeLogin()
{
    m_nativeMode = true;
    // Same web flow as the official Dropbox Android SDK: www.dropbox.com/1/connect
    // redirects to db-<appkey>://1/connect, which is whitelisted for the app's
    // own key. The PKCE parameters ride along in the state / extra_query_params.
    m_state = QStringLiteral("oauth2code:%1:S256:offline").arg(m_pkce->challenge);
    const QString extra = QStringLiteral(
        "code_challenge=%1&code_challenge_method=S256&token_access_type=offline"
        "&response_type=code").arg(m_pkce->challenge);

    const QString url = QStringLiteral(
        "https://www.dropbox.com/1/connect?k=%1&n=0&state=%2&extra_query_params=%3")
        .arg(appKey(),
             QString::fromLatin1(QUrl::toPercentEncoding(m_state)),
             QString::fromLatin1(QUrl::toPercentEncoding(extra)));
    QDesktopServices::openUrl(QUrl::fromEncoded(url.toLatin1()));
}

void DropboxAuthProvider::handleAuthRedirect(const QUrl &url)
{
    if (!m_authenticating || !m_nativeMode || !m_pkce) {
        qWarning() << "Dropbox redirect ignored: no native sign-in in progress"
                   << "(authenticating =" << m_authenticating << "native =" << m_nativeMode << ")";
        return;
    }
    qInfo() << "Dropbox redirect received, exchanging code";

    const QUrlQuery query(url.query());
    const auto decoded = [&query](const char *key) {
        return query.queryItemValue(QString::fromLatin1(key), QUrl::FullyDecoded);
    };
    const QString tokenType = decoded("oauth_token");
    const QString code = decoded("oauth_token_secret");
    const QString state = decoded("state");

    if (tokenType != QLatin1String("oauth2code:") || code.isEmpty() || state != m_state) {
        QStringList keys;
        for (const auto &item : query.queryItems())
            keys << item.first;
        qWarning().noquote() << "Dropbox redirect rejected: scheme/host/path ="
                             << url.scheme() << url.host() << url.path()
                             << "keys =" << keys << "oauth_token =" << tokenType
                             << "state matches =" << (state == m_state) << "state len ="
                             << state.size() << "expected len =" << m_state.size()
                             << "fragment =" << url.fragment().left(40);
        cancelLogin();
        emit authFailed(QStringLiteral(
            "Unexpected authorization response (token type '%1', state %2, "
            "%3 query items).")
                            .arg(tokenType, state == m_state ? QStringLiteral("ok") : QStringLiteral("mismatch"))
                            .arg(keys.size()));
        return;
    }

    // The SDK sends no redirect_uri in the exchange either.
    const QString verifier = m_pkce->verifier;
    exchangeAuthorizationCode(
        code, verifier, QString(),
        [this](std::optional<DropboxCredential>, const QString &exchangeError) {
            cancelLogin();
            if (!exchangeError.isEmpty())
                emit authFailed(exchangeError);
        });
}

QString DropboxAuthProvider::oauthMode() const
{
    auto isValidMode = [](const QString &mode) {
        return mode == QLatin1String("code") || mode == QLatin1String("loopback")
               || mode == QLatin1String("native");
    };
    const QString env = qEnvironmentVariable("KALIBREBOX_DROPBOX_OAUTH_MODE");
    if (!env.isEmpty() && isValidMode(env))
        return env;
    if (m_settings) {
        const QString fromSettings =
            m_settings->value(QStringLiteral("DropboxPrefs/oauth_mode")).toString();
        if (isValidMode(fromSettings))
            return fromSettings;
    }
    // Copy-the-code flow on every platform; the native db-<appkey>:// flow is
    // opt-in (oauth_mode=native) because the redirect was unreliable.
    return QStringLiteral("code");
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
    m_directCodeMode = false;
    m_nativeMode = false;
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
    // An empty redirectUri (direct-code flow) is intentionally omitted:
    // Dropbox requires the exchange to mirror the authorize parameters.
    if (!redirectUri.isEmpty())
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
        qInfo() << "Dropbox token exchange finished, HTTP" << statusCode << reply->errorString();
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
