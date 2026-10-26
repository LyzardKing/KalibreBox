#pragma once

#include "core/dropbox/DropboxCredential.h"
#include "core/dropbox/Pkce.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <functional>

class QNetworkAccessManager;
class QSettings;

QT_BEGIN_NAMESPACE
class QTcpServer;
QT_END_NAMESPACE

namespace KalibreBox {

/**
 * OAuth 2.0 flows for Dropbox (token_access_type=offline where possible).
 *
 * Modes:
 * - "native" (Android default): the implicit flow. The browser redirects to
 *   db-<appkey>:// (whitelisted by Dropbox without App Console setup, like
 *   the Kotlin SDK did), our Android activity receives the token directly —
 *   no code paste, no refresh token (implicit tokens do not expire).
 * - "code" (desktop default): the browser shows the authorization code on
 *   screen and the user pastes it into the app — no pre-registered redirect
 *   URI needed. Finish with completeDirectSignIn().
 * - "loopback": the app listens on the registered port
 *   (http://127.0.0.1:<port>/callback), receives the redirect automatically
 *   and exchanges the code on its own.
 *
 * Code/loopback tokens can always be refreshed later; native tokens are
 * static (no refresh_token is issued for the implicit flow).
 */
class DropboxAuthProvider : public QObject
{
    Q_OBJECT

public:
    explicit DropboxAuthProvider(QSettings *settings, QObject *parent = nullptr);
    ~DropboxAuthProvider() override;

    /// Resolves the app key: explicit override > settings > env > compile-time.
    QString appKey() const;
    void setAppKey(const QString &appKey);

    /// Resolved OAuth mode: "native" (Android default), "code", or "loopback".
    QString oauthMode() const;

    const std::optional<DropboxCredential> &credential() const { return m_credential; }
    void setCredential(const DropboxCredential &credential);
    void logout();

    bool isAuthenticating() const { return m_authenticating; }

    void startLogin();
    void cancelLogin();

    /// "code" mode: submits the code the user copied from the browser page.
    void completeDirectSignIn(const QString &code);

    /// Exchanges an authorization code for tokens (exposed for tests/tools).
    void exchangeAuthorizationCode(const QString &code, const QString &codeVerifier,
                                   const QString &redirectUri,
                                   const std::function<void(std::optional<DropboxCredential>,
                                                            const QString &)> &callback);

    /// Refreshes the access token using the stored refresh token.
    void refreshAccessToken(
        const std::function<void(std::optional<DropboxCredential>, const QString &)> &callback);

    /// Routes a db-<appkey>:// redirect URL (from the Android intent side) to
    /// the currently authenticating provider instance.
    static void dispatchAuthRedirect(const QString &url);

    /// Ensures the credential window is valid, refreshing when needed/unavailable.
    /// `callback` receives true when a usable access token is in credential().
    void ensureFreshToken(const std::function<void(bool, const QString &)> &callback);

signals:
    void authSucceeded(const KalibreBox::DropboxCredential &credential);
    void authFailed(const QString &message);
    /// "code" mode: the browser page with the authorization code is open.
    void codeRequested();

private:
    void startLoopbackLogin();
    void startDirectCodeLogin();
    void startNativeLogin();
    /// "native" mode: processes the db-<appkey>:// redirect with the token.
    void handleAuthRedirect(const QUrl &url);

    std::optional<DropboxCredential> m_credential;
    QSettings *m_settings = nullptr;
    QString m_appKey;
    QNetworkAccessManager *m_nam = nullptr;

    // Login flow state
    bool m_authenticating = false;
    bool m_directCodeMode = false;
    bool m_nativeMode = false;
    std::optional<PkceCodes> m_pkce;
    QString m_state;
    QString m_redirectUri;
    QTcpServer *m_redirectServer = nullptr;
    QElapsedTimer m_loginTimer;

    void cancelListening();
};

}
