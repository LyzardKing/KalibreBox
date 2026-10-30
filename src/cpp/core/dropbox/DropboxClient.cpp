#include "core/dropbox/DropboxClient.h"

#include "core/dropbox/DropboxApiArg.h"
#include "core/dropbox/DropboxAuthProvider.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSettings>

namespace KalibreBox
{

namespace
{

Q_LOGGING_CATEGORY(kalibreboxDropbox, "kalibrebox.dropbox")

const QString rpcHost = QStringLiteral("https://api.dropbox.com/2/");
const QString contentHost = QStringLiteral("https://content.dropboxapi.com/2/");
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
        const QDateTime modified = QDateTime::fromString(serverModified, Qt::ISODateWithMs);
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

void DropboxClient::withValidAccessToken(const std::function<void(bool, const QString &)> &then)
{
    m_authProvider->ensureFreshToken(then);
}

QString DropboxClient::errorSummaryFrom(const QJsonDocument &response, int statusCode)
{
    if (response.isObject()) {
        const QString summary = response.object().value(QStringLiteral("error_summary")).toString();
        if (!summary.isEmpty())
            return summary;
    }
    return QStringLiteral("HTTP %1").arg(statusCode);
}

void DropboxClient::handleReplyJson(QNetworkReply *reply,
                                    const std::function<void(int statusCode, const QString &errorSummary, const QJsonDocument &response)> &callback)
{
    connect(reply, &QNetworkReply::finished, reply, [reply, callback] {
        reply->deleteLater();
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        const QJsonDocument document = QJsonDocument::fromJson(payload);
        QString errorSummary;
        if (statusCode != 200)
            errorSummary = errorSummaryFrom(document, statusCode);
        callback(statusCode, errorSummary, document);
    });
}

void DropboxClient::postRpc(const QString &endpoint,
                            const QString &accessToken,
                            const QJsonDocument &body,
                            const std::function<void(int statusCode, const QString &errorSummary, const QJsonDocument &response)> &callback)
{
    bool allowRefreshRetry = true;

    const auto handle =
        [this, endpoint, body, callback, allowRefreshRetry](int statusCode, const QString &errorSummary, const QJsonDocument &response) mutable {
            if (statusCode == 401 && allowRefreshRetry && m_authProvider->credential() && m_authProvider->credential()->hasRefreshToken()) {
                allowRefreshRetry = false;
                m_authProvider->refreshAccessToken([this, endpoint, body, callback](std::optional<DropboxCredential> refreshed, const QString &refreshError) {
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
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!accessToken.isEmpty())
        request.setRawHeader(QByteArrayLiteral("Authorization"), (bearerPrefix + accessToken).toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    handleReplyJson(m_nam->post(request, body.toJson(QJsonDocument::Compact)), handle);
}

void DropboxClient::listFolder(const QString &path, const std::function<void(DropboxListFolderResult, const QString &)> &callback)
{
    withValidAccessToken([this, path, callback](bool ok, const QString &error) {
        if (!ok) {
            callback({}, error);
            return;
        }
        const std::optional<DropboxCredential> credential = m_authProvider->credential();
        if (!credential) {
            callback({}, QStringLiteral("Not signed in to Dropbox."));
            return;
        }
        QJsonObject body;
        body.insert(QStringLiteral("path"), path);
        body.insert(QStringLiteral("recursive"), false);
        body.insert(QStringLiteral("include_deleted"), false);
        body.insert(QStringLiteral("include_media_info"), false);

        const auto onCompleted = [callback](int statusCode, const QString &errorSummary, const QJsonDocument &response) {
            if (statusCode == 200)
                callback(listFolderResultFromJson(response), QString());
            else
                callback({}, errorSummary);
        };
        // Handle refresh-retry semantics by wrapping retry-once-on-401.
        postRpc(QStringLiteral("files/list_folder"), credential->accessToken, QJsonDocument(body), onCompleted);
    });
}

void DropboxClient::listFolderContinue(const QString &cursor, const std::function<void(DropboxListFolderResult, const QString &)> &callback)
{
    withValidAccessToken([this, cursor, callback](bool ok, const QString &error) {
        if (!ok) {
            callback({}, error);
            return;
        }
        const std::optional<DropboxCredential> credential = m_authProvider->credential();
        if (!credential) {
            callback({}, QStringLiteral("Not signed in to Dropbox."));
            return;
        }
        QJsonObject body;
        body.insert(QStringLiteral("cursor"), cursor);
        postRpc(QStringLiteral("files/list_folder/continue"),
                credential->accessToken,
                QJsonDocument(body),
                [callback](int statusCode, const QString &errorSummary, const QJsonDocument &response) {
                    if (statusCode == 200)
                        callback(listFolderResultFromJson(response), QString());
                    else
                        callback({}, errorSummary);
                });
    });
}

void DropboxClient::getMetadata(const QString &path, const std::function<void(std::optional<DropboxFileMetadata>, const QString &)> &callback)
{
    withValidAccessToken([this, path, callback](bool ok, const QString &error) {
        if (!ok) {
            callback(std::nullopt, error);
            return;
        }
        const std::optional<DropboxCredential> credential = m_authProvider->credential();
        if (!credential) {
            callback(std::nullopt, QStringLiteral("Not signed in to Dropbox."));
            return;
        }
        QJsonObject body;
        body.insert(QStringLiteral("path"), path);
        body.insert(QStringLiteral("include_media_info"), false);
        body.insert(QStringLiteral("include_deleted"), false);
        postRpc(QStringLiteral("files/get_metadata"),
                credential->accessToken,
                QJsonDocument(body),
                [callback](int statusCode, const QString &errorSummary, const QJsonDocument &response) {
                    if (statusCode == 200)
                        callback(fileMetadataFromJson(response.object()), QString());
                    else
                        callback(std::nullopt, errorSummary);
                });
    });
}

void DropboxClient::downloadToFile(const QString &dropboxPath,
                                   const std::optional<QString> &sharedLinkUrl,
                                   const QString &localTargetPath,
                                   const std::function<void(std::optional<DropboxFileMetadata>, const QString &)> &callback)
{
    downloadImpl(dropboxPath, sharedLinkUrl, QString(), localTargetPath, {}, callback);
}

void DropboxClient::downloadToSink(const QString &dropboxPath,
                                   const std::optional<QString> &sharedLinkUrl,
                                   const ByteSink &sink,
                                   const std::function<void(std::optional<DropboxFileMetadata>, const QString &)> &callback)
{
    downloadImpl(dropboxPath, sharedLinkUrl, QString(), QString(), sink, callback);
}

void DropboxClient::downloadThumbnailToFile(const QString &dropboxPath,
                                            const QString &size,
                                            const QString &localTargetPath,
                                            const std::function<void(std::optional<DropboxFileMetadata>, const QString &)> &callback)
{
    downloadImpl(dropboxPath, std::nullopt, size, localTargetPath, {}, callback);
}

void DropboxClient::downloadImpl(const QString &dropboxPath,
                                 const std::optional<QString> &sharedLinkUrl,
                                 const QString &thumbnailSize,
                                 const QString &localTargetPath,
                                 const ByteSink &sink,
                                 const std::function<void(std::optional<DropboxFileMetadata>, const QString &)> &callback,
                                 bool allowRefreshRetry)
{
    withValidAccessToken([this, dropboxPath, sharedLinkUrl, thumbnailSize, localTargetPath, sink, callback, allowRefreshRetry](bool ok, const QString &error) {
        if (!ok) {
            callback(std::nullopt, error);
            return;
        }
        const std::optional<DropboxCredential> credential = m_authProvider->credential();
        if (!credential) {
            // A logout between ensureFreshToken and here must not crash.
            callback(std::nullopt, QStringLiteral("Not signed in to Dropbox."));
            return;
        }
        const QString accessToken = credential->accessToken;

        const auto onCompletion = [this, callback, localTargetPath, sink, dropboxPath, sharedLinkUrl, thumbnailSize, allowRefreshRetry](QNetworkReply *reply) {
            const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

            // Refresh-and-retry once when the access token was rejected.
            if (statusCode == 401 && allowRefreshRetry && m_authProvider->credential() && m_authProvider->credential()->hasRefreshToken()) {
                QNetworkReply *capturedReply = reply;
                m_authProvider->refreshAccessToken(
                    [this, dropboxPath, sharedLinkUrl, thumbnailSize, localTargetPath, sink, callback, allowRefreshRetry, capturedReply](
                        const std::optional<DropboxCredential> &refreshed,
                        const QString &refreshError) {
                        capturedReply->deleteLater();
                        if (!refreshed) {
                            callback(std::nullopt, refreshError);
                            return;
                        }
                        downloadImpl(dropboxPath, sharedLinkUrl, thumbnailSize, localTargetPath, sink, callback, false);
                    });
                return;
            }
            qCDebug(kalibreboxDropbox).noquote() << QStringLiteral("Content response: HTTP %1, %2 bytes").arg(statusCode).arg(QString::number(reply->size()));

            reply->deleteLater();

            const QByteArray apiResultHeader = reply->rawHeader(QByteArrayLiteral("Dropbox-API-Result"));
            const QByteArray body = reply->readAll();

            if (statusCode != 200) {
                const QJsonDocument errorDocument = QJsonDocument::fromJson(body);
                QString error;
                if (errorDocument.isObject()) {
                    error = errorDocument.object().value(QStringLiteral("error_summary")).toString();
                }
                if (error.isEmpty()) {
                    if (statusCode != 0) {
                        error = QStringLiteral("Download failed (HTTP %1): %2").arg(statusCode).arg(QString::fromUtf8(body.left(256)));
                    } else {
                        // Network-level failure before any HTTP response
                        // (DNS, connection, TLS …) — report the real cause.
                        error = QStringLiteral("Download failed: %1").arg(reply->errorString());
                    }
                }
                callback(std::nullopt, error);
                return;
            }

            DropboxFileMetadata metadata;
            if (!apiResultHeader.isEmpty()) {
                const QJsonDocument resultDocument = QJsonDocument::fromJson(apiResultHeader);
                if (resultDocument.isObject())
                    metadata = fileMetadataFromJson(resultDocument.object());
            }

            if (sink) {
                QString sinkError;
                if (!sink(body, &sinkError)) {
                    callback(std::nullopt, sinkError);
                    return;
                }
                callback(metadata, QString());
                return;
            }

            // QSaveFile refuses to create files in non-existent directories.
            QFileInfo(localTargetPath).dir().mkpath(QStringLiteral("."));
            QSaveFile file(localTargetPath);
            if (!file.open(QIODevice::WriteOnly)) {
                callback(std::nullopt, QStringLiteral("Could not open %1 for writing").arg(localTargetPath));
                return;
            }
            file.write(body);
            if (!file.commit()) {
                callback(std::nullopt, QStringLiteral("Could not save %1: %2").arg(localTargetPath, file.errorString()));
                return;
            }
            callback(metadata, QString());
        };

        // Dropbox-API-Arg carries the parameters for content-host calls.
        QJsonObject apiArguments;
        QUrl endpoint;
        if (!thumbnailSize.isEmpty()) {
            endpoint = QUrl(contentHost + QStringLiteral("files/get_thumbnail_v2"));
            apiArguments.insert(QStringLiteral("resource"),
                                QJsonObject{
                                    {QStringLiteral(".tag"), QStringLiteral("path")},
                                    {QStringLiteral("path"), dropboxPath},
                                });
            apiArguments.insert(QStringLiteral("format"), QStringLiteral("jpeg"));
            apiArguments.insert(QStringLiteral("size"), thumbnailSize);
            apiArguments.insert(QStringLiteral("mode"), QStringLiteral("fitone_bestfit"));
        } else if (sharedLinkUrl) {
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
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
        request.setRawHeader(QByteArrayLiteral("Authorization"), (bearerPrefix + accessToken).toUtf8());
        request.setRawHeader(QByteArrayLiteral("Dropbox-API-Arg"), asciiJsonArgument(QJsonDocument(apiArguments)).toUtf8());
        qCDebug(kalibreboxDropbox).noquote()
            << QStringLiteral("GET content: %1 (local: %2)").arg(QString::fromUtf8(request.rawHeader(QByteArrayLiteral("Dropbox-API-Arg"))), localTargetPath);
        QNetworkReply *reply = m_nam->post(request, QByteArray());
        connect(reply, &QNetworkReply::finished, reply, [onCompletion, reply] {
            onCompletion(reply);
        });
    });
}

void DropboxClient::copyFile(const QString &fromPath, const QString &toPath, const std::function<void(bool, const QString &)> &callback)
{
    withValidAccessToken([this, fromPath, toPath, callback](bool ok, const QString &error) {
        if (!ok) {
            callback(false, error);
            return;
        }
        const std::optional<DropboxCredential> credential = m_authProvider->credential();
        if (!credential) {
            callback(false, QStringLiteral("Not signed in to Dropbox."));
            return;
        }
        QJsonObject body;
        body.insert(QStringLiteral("from_path"), fromPath);
        body.insert(QStringLiteral("to_path"), toPath);
        body.insert(QStringLiteral("autorename"), false);

        postRpc(QStringLiteral("files/copy_v2"),
                credential->accessToken,
                QJsonDocument(body),
                [callback](int statusCode, const QString &errorSummary, const QJsonDocument & /*response*/) {
                    if (statusCode == 200)
                        callback(true, QString());
                    else
                        callback(false, errorSummary);
                });
    });
}

void DropboxClient::deletePath(const QString &path, const std::function<void(bool, const QString &)> &callback)
{
    withValidAccessToken([this, path, callback](bool ok, const QString &error) {
        if (!ok) {
            callback(false, error);
            return;
        }
        const std::optional<DropboxCredential> credential = m_authProvider->credential();
        if (!credential) {
            callback(false, QStringLiteral("Not signed in to Dropbox."));
            return;
        }
        QJsonObject body;
        body.insert(QStringLiteral("path"), path);
        postRpc(QStringLiteral("files/delete_v2"),
                credential->accessToken,
                QJsonDocument(body),
                [callback](int statusCode, const QString &errorSummary, const QJsonDocument &) {
                    callback(statusCode == 200, errorSummary);
                });
    });
}

}
