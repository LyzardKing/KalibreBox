#include "core/dropbox/DropboxAuthProvider.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>
#include <QSettings>
#include <QTimer>
#include <QUrl>

namespace KalibreBox
{

namespace
{

// 10 minutes to complete the browser flow.
constexpr int loginTimeoutMs = 10 * 60 * 1000;

const char *const authorizeEndpoint = "https://www.dropbox.com/oauth2/authorize";
const char *const tokenEndpointUrl = "https://api.dropbox.com/oauth2/token";

QString compileTimeAppKey()
{
#ifdef KALIBREBOX_DROPBOX_APP_KEY
    return QString::fromUtf8(KALIBREBOX_DROPBOX_APP_KEY);
#else
    return QString();
#endif
}

// Dropbox requires the redirect_uri to be pre-registered in the App Console,
// so the loopback port must be stable.
int oauthPort()
{
#ifdef KALIBREBOX_DROPBOX_OAUTH_PORT
    const int compiled = QString::fromUtf8(KALIBREBOX_DROPBOX_OAUTH_PORT).toInt();
    if (compiled > 0)
        return compiled;
#endif
    return 61196;
}

// Reply handler that can also be fed the code the user pasted from the
// browser (no redirect involved), reusing Qt's state check and token parsing.
class PasteCodeReplyHandler : public QOAuthHttpServerReplyHandler
{
public:
    using QOAuthHttpServerReplyHandler::QOAuthHttpServerReplyHandler;

    void submitCode(const QString &code, const QString &state)
    {
        emit callbackReceived(QVariantMap{
            {QStringLiteral("code"), code},
            {QStringLiteral("state"), state},
        });
    }
};

}

DropboxAuthProvider::DropboxAuthProvider(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_flow(new QOAuth2AuthorizationCodeFlow(this))
{
    auto *handler = new PasteCodeReplyHandler(QHostAddress::LocalHost, 0, this);
    handler->setCallbackPath(QStringLiteral("callback"));
    handler->setCallbackText(
        QStringLiteral("<html><body><h3>KalibreBox</h3><p>Signed in. You can close this tab.</p>"
                       "</body></html>"));
    m_replyHandler = handler;

    m_flow->setReplyHandler(m_replyHandler);
    m_flow->setAuthorizationUrl(QUrl(QString::fromLatin1(authorizeEndpoint)));
    m_flow->setTokenUrl(QUrl(QString::fromLatin1(tokenEndpointUrl)));
    m_flow->setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);
    m_flow->setModifyParametersFunction([this](QAbstractOAuth::Stage stage, QMultiMap<QString, QVariant> *parameters) {
        if (stage == QAbstractOAuth::Stage::RequestingAuthorization)
            parameters->replace(QStringLiteral("token_access_type"), QStringLiteral("offline"));
        // PKCE apps have no secret; Qt would send an empty one and Dropbox
        // answers "invalid_client".
        if (parameters->value(QStringLiteral("client_secret")).toString().isEmpty())
            parameters->remove(QStringLiteral("client_secret"));
        // Without a redirect Dropbox shows the code on screen instead.
        if (!m_loopbackMode)
            parameters->remove(QStringLiteral("redirect_uri"));
    });

    connect(m_flow, &QAbstractOAuth::authorizeWithBrowser, this, [this](const QUrl &url) {
        QDesktopServices::openUrl(url);
        if (!m_loopbackMode)
            emit codeRequested();
    });
    connect(m_flow, &QAbstractOAuth::granted, this, &DropboxAuthProvider::onGranted);
    connect(m_flow, &QAbstractOAuth2::serverReportedErrorOccurred, this, [this](const QString &error, const QString &description, const QUrl &) {
        onFailure(description.isEmpty() ? error : description);
    });
    // Qt only reports "status 400"; keep Dropbox's JSON error to show the real reason.
    auto *nam = new QNetworkAccessManager(this);
    m_flow->setNetworkAccessManager(nam);
    connect(nam, &QNetworkAccessManager::finished, this, [this](QNetworkReply *reply) {
        if (reply->error() != QNetworkReply::NoError)
            m_lastServerError = QString::fromUtf8(reply->peek(reply->bytesAvailable())).trimmed();
    });
    connect(m_flow, &QAbstractOAuth::requestFailed, this, [this](QAbstractOAuth::Error error) {
        // The network manager's finished signal fires just after this one.
        QTimer::singleShot(0, this, [this, error] {
            const QString detail = m_lastServerError.isEmpty() ? QStringLiteral("Request failed (error %1)").arg(static_cast<int>(error))
                                                               : QStringLiteral("Dropbox rejected the request: %1").arg(m_lastServerError);
            m_lastServerError.clear();
            onFailure(detail);
        });
    });
}

DropboxAuthProvider::~DropboxAuthProvider() = default;

QString DropboxAuthProvider::appKey() const
{
    if (!m_appKey.isEmpty())
        return m_appKey;
    if (m_settings) {
        const QString fromSettings = m_settings->value(QStringLiteral("DropboxPrefs/app_key")).toString();
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

QString DropboxAuthProvider::oauthMode() const
{
    const auto isValidMode = [](const QString &mode) {
        return mode == QLatin1String("code") || mode == QLatin1String("loopback");
    };
    const QString env = qEnvironmentVariable("KALIBREBOX_DROPBOX_OAUTH_MODE");
    if (isValidMode(env))
        return env;
    if (m_settings) {
        const QString fromSettings = m_settings->value(QStringLiteral("DropboxPrefs/oauth_mode")).toString();
        if (isValidMode(fromSettings))
            return fromSettings;
    }
    return QStringLiteral("code");
}

void DropboxAuthProvider::startLogin()
{
    if (appKey().isEmpty()) {
        emit authFailed(
            QStringLiteral("No Dropbox app key configured. Add dropbox.key=<your key> to "
                           "local.properties, or set KALIBREBOX_DROPBOX_APP_KEY."));
        return;
    }

    cancelLogin();
    m_authenticating = true;
    m_loopbackMode = oauthMode() == QLatin1String("loopback");
    m_flow->setClientIdentifier(appKey());

    if (m_loopbackMode) {
        const int port = oauthPort();
        if (!m_replyHandler->listen(QHostAddress::LocalHost, port)) {
            m_authenticating = false;
            emit authFailed(QStringLiteral("Could not listen on http://127.0.0.1:%1 for the OAuth redirect. "
                                           "Another process may be occupying the port; free it and retry.")
                                .arg(port));
            return;
        }
    }

    // Abort the whole flow if it is never completed.
    QTimer::singleShot(loginTimeoutMs, this, [this] {
        if (m_authenticating)
            onFailure(QStringLiteral("Authorization timed out. Please try again."));
    });

    m_flow->grant();
}

void DropboxAuthProvider::cancelLogin()
{
    m_replyHandler->close();
    m_authenticating = false;
}

void DropboxAuthProvider::completeDirectSignIn(const QString &code)
{
    if (!m_authenticating || m_loopbackMode) {
        emit authFailed(QStringLiteral("No sign-in in progress — press 'Sign in' first."));
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
        trimmed = QUrl::fromPercentEncoding(extracted.toUtf8()).trimmed();
    }

    static_cast<PasteCodeReplyHandler *>(m_replyHandler)->submitCode(trimmed, m_flow->state());
}

void DropboxAuthProvider::onGranted()
{
    DropboxCredential credential = m_credential.value_or(DropboxCredential{});
    credential.accessToken = m_flow->token();
    // Refresh responses carry no new refresh token; keep the stored one.
    if (!m_flow->refreshToken().isEmpty())
        credential.refreshToken = m_flow->refreshToken();
    const QString accountId = m_flow->extraTokens().value(QStringLiteral("account_id")).toString();
    if (!accountId.isEmpty())
        credential.accountId = accountId;
    credential.expiresAt = m_flow->expirationAt().isValid() ? m_flow->expirationAt().toSecsSinceEpoch() : 0;

    if (!credential.hasAccessToken()) {
        onFailure(QStringLiteral("Token response had no access token"));
        return;
    }
    setCredential(credential);

    if (m_refreshing) {
        finishRefresh(credential, QString());
    } else if (m_authenticating) {
        cancelLogin();
        emit authSucceeded(credential);
    }
}

void DropboxAuthProvider::onFailure(const QString &message)
{
    if (m_refreshing)
        finishRefresh(std::nullopt, message);
    else if (m_authenticating) {
        cancelLogin();
        emit authFailed(message);
    }
}

void DropboxAuthProvider::finishRefresh(const std::optional<DropboxCredential> &credential, const QString &error)
{
    m_refreshing = false;
    const std::vector<RefreshCallback> callbacks = std::exchange(m_refreshCallbacks, {});
    for (const RefreshCallback &callback : callbacks)
        callback(credential, error);
}

void DropboxAuthProvider::refreshAccessToken(const RefreshCallback &callback)
{
    if (!m_credential || !m_credential->hasRefreshToken()) {
        callback(std::nullopt, QStringLiteral("No refresh token available"));
        return;
    }

    m_refreshCallbacks.push_back(callback);
    if (m_refreshing)
        return; // Joins the refresh already in flight.

    m_refreshing = true;
    m_flow->setClientIdentifier(appKey());
    m_flow->setRefreshToken(m_credential->refreshToken);
    m_flow->refreshTokens();
}

void DropboxAuthProvider::ensureFreshToken(const std::function<void(bool, const QString &)> &callback)
{
    if (!m_credential || !m_credential->hasAccessToken()) {
        callback(false, QStringLiteral("Not signed in to Dropbox"));
        return;
    }
    if (m_credential->hasRefreshToken() && m_credential->needsRefresh()) {
        refreshAccessToken([callback](const std::optional<DropboxCredential> &refreshed, const QString &error) {
            callback(refreshed.has_value(), error);
        });
        return;
    }
    callback(true, QString());
}

}
