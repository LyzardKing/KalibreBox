#pragma once

#include "core/dropbox/DropboxCredential.h"

#include <QObject>
#include <QString>
#include <functional>
#include <vector>

class QSettings;
class QOAuth2AuthorizationCodeFlow;
class QOAuthHttpServerReplyHandler;

namespace KalibreBox {

/**
 * OAuth 2.0 (authorization code + PKCE, token_access_type=offline) for
 * Dropbox, built on Qt Network Authorization.
 *
 * Modes:
 * - "code" (default): the browser shows the authorization code and the user
 *   pastes it into the app (completeDirectSignIn) — no pre-registered
 *   redirect URI needed.
 * - "loopback": the app listens on the registered port
 *   (http://127.0.0.1:<port>/callback) and finishes sign-in automatically.
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

    /// Resolved OAuth mode: "code" (default) or "loopback".
    QString oauthMode() const;

    const std::optional<DropboxCredential> &credential() const { return m_credential; }
    void setCredential(const DropboxCredential &credential);
    void logout();

    bool isAuthenticating() const { return m_authenticating; }

    void startLogin();
    void cancelLogin();

    /// "code" mode: submits the code the user copied from the browser page.
    void completeDirectSignIn(const QString &code);

    /// Refreshes the access token using the stored refresh token.
    void refreshAccessToken(
        const std::function<void(std::optional<DropboxCredential>, const QString &)> &callback);

    /// Ensures the credential window is valid, refreshing when needed/unavailable.
    /// `callback` receives true when a usable access token is in credential().
    void ensureFreshToken(const std::function<void(bool, const QString &)> &callback);

signals:
    void authSucceeded(const KalibreBox::DropboxCredential &credential);
    void authFailed(const QString &message);
    /// "code" mode: the browser page with the authorization code is open.
    void codeRequested();

private:
    using RefreshCallback =
        std::function<void(std::optional<DropboxCredential>, const QString &)>;

    void onGranted();
    void onFailure(const QString &message);
    void finishRefresh(const std::optional<DropboxCredential> &credential,
                       const QString &error);

    std::optional<DropboxCredential> m_credential;
    QSettings *m_settings = nullptr;
    QString m_appKey;

    QOAuth2AuthorizationCodeFlow *m_flow = nullptr;
    QOAuthHttpServerReplyHandler *m_replyHandler = nullptr;

    bool m_authenticating = false;
    bool m_loopbackMode = false;
    bool m_refreshing = false;
    std::vector<RefreshCallback> m_refreshCallbacks;
};

}
