#pragma once

#include "core/dropbox/DropboxCredential.h"
#include "core/dropbox/DropboxTypes.h"

#include <QJsonDocument>
#include <QObject>
#include <QString>
#include <functional>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;
class QSettings;

namespace KalibreBox {

class DropboxAuthProvider;

/**
 * Minimal Qt client over the Dropbox HTTP API (RPC + content hosts).
 * All operations are asynchronous and driven by callbacks. Access tokens
 * are refreshed transparently (including one automatic retry on HTTP 401).
 *
 * Supported operations, matching the Android app's use of the Dropbox SDK:
 * - listFolder / listFolderContinue
 * - getMetadata
 * - downloadToFile (direct path or shared link)
 * - copyFile (send-to-Kobo)
 * - deletePath
 */
class DropboxClient : public QObject
{
    Q_OBJECT

public:
    explicit DropboxClient(QSettings *settings, QObject *parent = nullptr);
    ~DropboxClient() override;

    DropboxAuthProvider *authProvider() const { return m_authProvider; }

    std::optional<DropboxCredential> credential() const;
    bool hasCredential() const;
    void setCredential(const DropboxCredential &credential);
    void logout();

    // --- API operations (callbacks receive the error, empty string on success) ---

    void listFolder(const QString &path,
                    const std::function<void(DropboxListFolderResult, const QString &)> &callback);
    void listFolderContinue(const QString &cursor,
                            const std::function<void(DropboxListFolderResult, const QString &)> &callback);

    void getMetadata(const QString &path,
                     const std::function<void(std::optional<DropboxFileMetadata>, const QString &)> &callback);

    /**
     * Streams a file to `localTargetPath`. When sharedLinkUrl is set the file
     * is fetched through that link instead of the authorized path namespace.
     */
    void downloadToFile(const QString &dropboxPath,
                        const std::optional<QString> &sharedLinkUrl,
                        const QString &localTargetPath,
                        const std::function<void(std::optional<DropboxFileMetadata>, const QString &)> &callback);

    void copyFile(const QString &fromPath, const QString &toPath,
                  const std::function<void(bool, const QString &)> &callback);
    void deletePath(const QString &path,
                    const std::function<void(bool, const QString &)> &callback);

private:
    /// Validates the credential, refreshing if needed, then calls `then`.
    void withValidAccessToken(const std::function<void(bool, const QString &)> &then);

    /// POSTs a JSON body to an RPC endpoint with one refresh-retry on 401.
    void postRpc(const QString &endpoint, const QString &accessToken,
                 const QJsonDocument &body,
                 const std::function<void(int statusCode, const QString &errorSummary,
                                          const QJsonDocument &response)> &callback);

    void handleReplyJson(QNetworkReply *reply,
                         const std::function<void(int statusCode, const QString &errorSummary,
                                                  const QJsonDocument &response)> &callback);

    static QString errorSummaryFrom(const QJsonDocument &response, int statusCode);

    QSettings *m_settings = nullptr;
    DropboxAuthProvider *m_authProvider = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
};

}
