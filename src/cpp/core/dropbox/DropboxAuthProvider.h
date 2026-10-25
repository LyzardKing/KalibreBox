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
 * Desktop OAuth 2.0 + PKCE flow for Dropbox (token_access_type=offline).
 *
 * startLogin() opens the system browser, listens on a loopback port for the
 * OAuth redirect, validates the state, exchanges the authorization code and
 * emits authSucceeded() / authFailed(). Tokens can also be refreshed later
 * without user interaction.
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

    const std::optional<DropboxCredential> &credential() const { return m_credential; }
    void setCredential(const DropboxCredential &credential);
    void logout();

    bool isAuthenticating() const { return m_authenticating; }

    void startLogin();
    void cancelLogin();

    /// Exchanges an authorization code for tokens (exposed for tests/tools).
    void exchangeAuthorizationCode(const QString &code, const QString &codeVerifier,
                                   const QString &redirectUri,
                                   const std::function<void(std::optional<DropboxCredential>,
                                                            const QString &)> &callback);

    /// Refreshes the access token using the stored refresh token.
    void refreshAccessToken(
        const std::function<void(std::optional<DropboxCredential>, const QString &)> &callback);

    /// Ensures the credential window is valid, refreshing when needed/unavailable.
    /// `callback` receives true when a usable access token is in credential().
    void ensureFreshToken(const std::function<void(bool, const QString &)> &callback);

signals:
    void authSucceeded(const KalibreBox::DropboxCredential &credential);
    void authFailed(const QString &message);

private:
    QString tokenEndpoint() const;

    std::optional<DropboxCredential> m_credential;
    QSettings *m_settings = nullptr;
    QString m_appKey;
    QNetworkAccessManager *m_nam = nullptr;

    // Login flow state
    bool m_authenticating = false;
    std::optional<PkceCodes> m_pkce;
    QString m_state;
    QString m_redirectUri;
    QTcpServer *m_redirectServer = nullptr;
    QElapsedTimer m_loginTimer;

    void cancelListening();
};

}
