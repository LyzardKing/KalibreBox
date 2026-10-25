#include "core/dropbox/DropboxClient.h"

#include "core/dropbox/DropboxAuthProvider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSettings>

namespace KalibreBox {

namespace {

const QString rpcHost = QStringLiteral("https://api.dropbox.com/2/");
const QString contentHost = QStringLiteral("https://content.dropbox.com/2/");
const QString bearerPrefix = QStringLiteral("Bearer ");

DropboxFileMetadata fileMetadataFromJson(const QJsonObject &object)
{
    DropboxFileMetadata metadata;
    metadata.name = object.value(QStringLiteral("name")).toString();
    metadata.pathLower = object.value(QStringLiteral("path_lower")).toString();
    metadata.id = object.value(QStringLiteral("id")).toString();
    metadata.contentHash = object.value(QStringLiteral("content_hash")).toString();
    if (!object.contains(QStringLiteral("size"))) {
        // Folder metadata has no size field.
        metadata.size = -1;
    } else {
        metadata.size = object.value(QStringLiteral("size")).toVariant().toLongLong();
    }
    const QString serverModified = object.value(QStringLiteral("server_modified")).toString();
    if (!serverModified.isEmpty()) {
        const QDateTime modified =
            QDateTime::fromString(serverModified, Qt::ISODateWithMs);
        if (modified.isValid())
            metadata.serverModifiedEpoch = modified.toSecsSinceEpoch();
    }
    return metadata;
}

DropboxEntry entryFromJson(const QJsonObject &object)
{
    DropboxEntry entry;
    entry.name = object.value(QStringLiteral("name")).toString();
    entry.pathLower = object.value(QStringLiteral("path_lower")).toString();
    entry.id = object.value(QStringLiteral("id")).toString();
    if (object.value(QStringLiteral(".tag")).toString() == QLatin1String("folder"))
        entry.isFolder = true;
    else
        entry.size = object.value(QStringLiteral("size")).toVariant().toLongLong();
    return entry;
}

DropboxListFolderResult listFolderResultFromJson(const QJsonDocument &document)
{
    DropboxListFolderResult result;
    if (!document.isObject())
        return result;
    const QJsonObject object = document.object();
    result.cursor = object.value(QStringLiteral("cursor")).toString();
    result.hasMore = object.value(QStringLiteral("has_more")).toBool();
    const QJsonArray entries = object.value(QStringLiteral("entries")).toArray();
    for (const QJsonValue &value : entries)
        result.entries.append(entryFromJson(value.toObject()));
    return result;
}

}

DropboxClient::DropboxClient(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_authProvider(new DropboxAuthProvider(settings, this))
    , m_nam(new QNetworkAccessManager(this))
{
}

DropboxClient::~DropboxClient() = default;

std::optional<DropboxCredential> DropboxClient::credential() const
{
    return m_authProvider->credential();
}

bool DropboxClient::hasCredential() const
{
    return m_authProvider->credential().has_value();
}

void DropboxClient::setCredential(const DropboxCredential &credential)
{
    m_authProvider->setCredential(credential);
}

void DropboxClient::logout()
{
    m_authProvider->logout();
}

void DropboxClient::withValidAccessToken(
    const std::function<void(bool, const QString &)> &then)
{
    m_authProvider->ensureFreshToken(then);
}

QString DropboxClient::errorSummaryFrom(const QJsonDocument &response, int statusCode)
{
    if (response.isObject()) {
        const QString summary =
            response.object().value(QStringLiteral("error_summary")).toString();
        if (!summary.isEmpty())
            return summary;
    }
    return QStringLiteral("HTTP %1").arg(statusCode);
}

void DropboxClient::handleReplyJson(
    QNetworkReply *reply,
    const std::function<void(int statusCode, const QString &errorSummary,
                             const QJsonDocument &response)> &callback)
{
    connect(reply, &QNetworkReply::finished, reply, [reply, callback]() {
        reply->deleteLater();
        const int statusCode =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        const QJsonDocument document = QJsonDocument::fromJson(payload);
        QString errorSummary;
        if (statusCode != 200)
            errorSummary = errorSummaryFrom(document, statusCode);
        callback(statusCode, errorSummary, document);
    });
}

void DropboxClient::postRpc(
    const QString &endpoint, const QString &accessToken, const QJsonDocument &body,
    const std::function<void(int statusCode, const QString &errorSummary,
                             const QJsonDocument &response)> &callback)
{
    bool allowRefreshRetry = true;

    const auto handle = [this, endpoint, body, callback, allowRefreshRetry](
                            int statusCode, const QString &errorSummary,
                            const QJsonDocument &response) mutable {
        if (statusCode == 401 && allowRefreshRetry
            && m_authProvider->credential()
            && m_authProvider->credential()->hasRefreshToken()) {
            allowRefreshRetry = false;
            m_authProvider->refreshAccessToken(
                [this, endpoint, body, callback](std::optional<DropboxCredential> refreshed,
                                                 const QString &refreshError) {
                    if (!refreshed) {
                        callback(401, refreshError, {});
                        return;
                    }
                    postRpc(endpoint, refreshed->accessToken, body, callback);
                });
            return;
        }
        callback(statusCode, errorSummary, response);
    };

    QNetworkRequest request(QUrl(rpcHost + endpoint));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    if (!accessToken.isEmpty())
        request.setRawHeader(QByteArrayLiteral("Authorization"),
                         (bearerPrefix + accessToken).toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    handleReplyJson(m_nam->post(request, body.toJson(QJsonDocument::Compact)), handle);
}

void DropboxClient::listFolder(
    const QString &path,
    const std::function<void(DropboxListFolderResult, const QString &)> &callback)
{
    withValidAccessToken([this, path, callback](bool ok, const QString &error) {
        if (!ok) {
            callback({}, error);
            return;
        }
        QJsonObject body;
        body.insert(QStringLiteral("path"), path);
        body.insert(QStringLiteral("recursive"), false);
        body.insert(QStringLiteral("include_deleted"), false);
        body.insert(QStringLiteral("include_media_info"), false);

        const auto onCompleted = [callback](int statusCode, const QString &errorSummary,
                                            const QJsonDocument &response) {
            if (statusCode == 200)
                callback(listFolderResultFromJson(response), QString());
            else
                callback({}, errorSummary);
        };
        // Handle refresh-retry semantics by wrapping retry-once-on-401.
        postRpc(QStringLiteral("files/list_folder"),
                m_authProvider->credential()->accessToken,
                QJsonDocument(body), onCompleted);
    });
}

void DropboxClient::listFolderContinue(
    const QString &cursor,
    const std::function<void(DropboxListFolderResult, const QString &)> &callback)
{
    withValidAccessToken([this, cursor, callback](bool ok, const QString &error) {
        if (!ok) {
            callback({}, error);
            return;
        }
        QJsonObject body;
        body.insert(QStringLiteral("cursor"), cursor);
        postRpc(QStringLiteral("files/list_folder/continue"),
                m_authProvider->credential()->accessToken,
                QJsonDocument(body),
                [callback](int statusCode, const QString &errorSummary,
                           const QJsonDocument &response) {
                    if (statusCode == 200)
                        callback(listFolderResultFromJson(response), QString());
                    else
                        callback({}, errorSummary);
                });
    });
}

void DropboxClient::getMetadata(
    const QString &path,
    const std::function<void(std::optional<DropboxFileMetadata>, const QString &)> &callback)
{
    withValidAccessToken([this, path, callback](bool ok, const QString &error) {
        if (!ok) {
            callback(std::nullopt, error);
            return;
        }
        QJsonObject body;
        body.insert(QStringLiteral("path"), path);
        body.insert(QStringLiteral("include_media_info"), false);
        body.insert(QStringLiteral("include_deleted"), false);
        postRpc(QStringLiteral("files/get_metadata"),
                m_authProvider->credential()->accessToken,
                QJsonDocument(body),
                [callback](int statusCode, const QString &errorSummary,
                           const QJsonDocument &response) {
                    if (statusCode == 200)
                        callback(fileMetadataFromJson(response.object()), QString());
                    else
                        callback(std::nullopt, errorSummary);
                });
    });
}

void DropboxClient::downloadToFile(
    const QString &dropboxPath, const std::optional<QString> &sharedLinkUrl,
    const QString &localTargetPath,
    const std::function<void(std::optional<DropboxFileMetadata>, const QString &)> &callback)
{
    withValidAccessToken([this, dropboxPath, sharedLinkUrl, localTargetPath, callback](
                             bool ok, const QString &error) {
        if (!ok) {
            callback(std::nullopt, error);
            return;
        }

        const auto onCompletion = [this, callback, localTargetPath, dropboxPath,
                                   sharedLinkUrl](QNetworkReply *reply) {
            const int statusCode =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

            // Refresh-and-retry once when the access token was rejected.
            if (statusCode == 401 && m_authProvider->credential()
                && m_authProvider->credential()->hasRefreshToken()) {
                QNetworkReply *capturedReply = reply;
                m_authProvider->refreshAccessToken(
                    [this, dropboxPath, sharedLinkUrl, localTargetPath, callback,
                     capturedReply](std::optional<DropboxCredential> refreshed,
                                    const QString &refreshError) {
                        capturedReply->deleteLater();
                        if (!refreshed) {
                            callback(std::nullopt, refreshError);
                            return;
                        }
                        downloadToFile(dropboxPath, sharedLinkUrl, localTargetPath,
                                       callback);
                    });
                return;
            }
            reply->deleteLater();

            const QByteArray apiResultHeader =
                reply->rawHeader(QByteArrayLiteral("Dropbox-API-Result"));
            const QByteArray body = reply->readAll();

            if (statusCode != 200) {
                const QJsonDocument errorDocument = QJsonDocument::fromJson(body);
                QString error;
                if (errorDocument.isObject()) {
                    error = errorDocument.object()
                                .value(QStringLiteral("error_summary"))
                                .toString();
                }
                if (error.isEmpty())
                    error = QStringLiteral("Download failed (HTTP %1): %2")
                                .arg(statusCode)
                                .arg(QString::fromUtf8(body.left(256)));
                callback(std::nullopt, error);
                return;
            }

            DropboxFileMetadata metadata;
            if (!apiResultHeader.isEmpty()) {
                const QJsonDocument resultDocument =
                    QJsonDocument::fromJson(apiResultHeader);
                if (resultDocument.isObject())
                    metadata = fileMetadataFromJson(resultDocument.object());
            }

            QSaveFile file(localTargetPath);
            if (!file.open(QIODevice::WriteOnly)) {
                callback(std::nullopt,
                         QStringLiteral("Could not open %1 for writing").arg(localTargetPath));
                return;
            }
            file.write(body);
            if (!file.commit()) {
                callback(std::nullopt, QStringLiteral("Could not save %1: %2")
                                           .arg(localTargetPath, file.errorString()));
                return;
            }
            callback(metadata, QString());
        };

        // Dropbox-API-Arg carries the parameters for content-host calls.
        QJsonObject apiArguments;
        QUrl endpoint;
        if (sharedLinkUrl) {
            endpoint = QUrl(contentHost + QStringLiteral("sharing/get_shared_link_file"));
            apiArguments.insert(QStringLiteral("url"), *sharedLinkUrl);
            // Paths inside a shared link are relative to the link root.
            QString relative = dropboxPath;
            while (relative.startsWith(u'/'))
                relative.removeFirst();
            apiArguments.insert(QStringLiteral("path"), QChar(u'/') + relative);
        } else {
            endpoint = QUrl(contentHost + QStringLiteral("files/download"));
            apiArguments.insert(QStringLiteral("path"), dropboxPath);
        }

        QNetworkRequest request(endpoint);
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/octet-stream"));
        request.setRawHeader(QByteArrayLiteral("Authorization"),
                             (bearerPrefix + m_authProvider->credential()->accessToken).toUtf8());
        request.setRawHeader(QByteArrayLiteral("Dropbox-API-Arg"),
                             QJsonDocument(apiArguments).toJson(QJsonDocument::Compact));
        QNetworkReply *reply = m_nam->post(request, QByteArray());
        connect(reply, &QNetworkReply::finished, reply,
                [onCompletion, reply]() { onCompletion(reply); });
    });
}

void DropboxClient::copyFile(const QString &fromPath, const QString &toPath,
                             const std::function<void(bool, const QString &)> &callback)
{
    withValidAccessToken([this, fromPath, toPath, callback](bool ok, const QString &error) {
        if (!ok) {
            callback(false, error);
            return;
        }
        QJsonObject body;
        body.insert(QStringLiteral("from_path"), fromPath);
        body.insert(QStringLiteral("to_path"), toPath);
        body.insert(QStringLiteral("autorename"), false);

        postRpc(QStringLiteral("files/copy_v2"),
                m_authProvider->credential()->accessToken,
                QJsonDocument(body),
                [callback](int statusCode, const QString &errorSummary,
                           const QJsonDocument &response) {
                    if (statusCode == 200)
                        callback(true, QString());
                    else
                        callback(false, errorSummary);
                });
    });
}

void DropboxClient::deletePath(const QString &path,
                               const std::function<void(bool, const QString &)> &callback)
{
    withValidAccessToken([this, path, callback](bool ok, const QString &error) {
        if (!ok) {
            callback(false, error);
            return;
        }
        QJsonObject body;
        body.insert(QStringLiteral("path"), path);
        postRpc(QStringLiteral("files/delete_v2"),
                m_authProvider->credential()->accessToken,
                QJsonDocument(body),
                [callback](int statusCode, const QString &errorSummary, const QJsonDocument &) {
                    callback(statusCode == 200, errorSummary);
                });
    });
}

}
