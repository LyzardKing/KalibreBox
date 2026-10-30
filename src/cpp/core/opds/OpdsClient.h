#pragma once

#include "core/opds/OpdsTransport.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <cstddef>

class QNetworkReply;
class QNetworkRequest;

namespace KalibreBox::Opds
{

/**
 * HTTP transport for OPDS catalogues. Follows redirects itself so credentials
 * can be kept away from other hosts: HTTP Basic credentials are attached only
 * to requests for the host they were configured for, and a redirect from https
 * to http is refused.
 */
class OpdsClient : public QObject, public OpdsTransport
{
    Q_OBJECT

public:
    explicit OpdsClient(QObject *parent = nullptr);
    ~OpdsClient() override;

    /// Credentials are sent only to the host of `scopeUrl`. Empty user clears them.
    void setCredentials(const QUrl &scopeUrl, const QString &user, const QString &password);

    RequestId fetchFeed(const QUrl &url, std::function<void(const FeedResult &)> done) override;
    RequestId fetchBytes(const QUrl &url, std::function<void(const BytesResult &)> done) override;
    RequestId downloadFile(const QUrl &url,
                           const QString &targetPath,
                           std::function<void(const FileResult &)> done,
                           std::function<void(qint64, qint64)> progress = {}) override;
    void cancel(RequestId id) override;

    /// Documents above this size are refused (a catalogue page is a few hundred KB at most).
    static constexpr qint64 maxDocumentBytes = static_cast<const qint64>(16 * 1024 * 1024);

private:
    struct Outcome {
        bool ok = false;
        QString error;
        bool networkError = false;
        QByteArray body;
        QString contentType;
        QUrl url;
    };

    QNetworkReply *send(const QUrl &url, bool wantFeed);
    [[nodiscard]] bool attachCredentials(const QUrl &url) const;
    /// Handles a 3xx: returns the target, or an empty URL and an error.
    QUrl redirectTarget(QNetworkReply *reply, const QUrl &from, int hopsLeft, QString *error) const;
    static QString describeFailure(QNetworkReply *reply, int status, bool *networkError);

    void getDocument(RequestId id, const QUrl &url, bool wantFeed, int hops, const std::function<void(const Outcome &)> &done);
    void downloadHop(RequestId id,
                     const QUrl &url,
                     const QString &target,
                     int hops,
                     const std::function<void(const FileResult &)> &done,
                     const std::function<void(qint64, qint64)> &progress);

    QNetworkAccessManager m_nam;
    QHash<RequestId, QPointer<QNetworkReply>> m_active;
    RequestId m_nextId = 0;

    QString m_authHost;
    QString m_authScheme;
    QByteArray m_authHeader;
};

}
