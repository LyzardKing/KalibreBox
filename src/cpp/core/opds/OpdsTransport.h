#pragma once

#include "core/opds/OpdsFeed.h"

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <functional>
#include <optional>

namespace KalibreBox::Opds
{

/// Identifies an in-flight request so it can be cancelled; 0 is never issued.
using RequestId = quint64;

struct FeedResult {
    std::optional<Feed> feed;
    QString error;
    /// True when no HTTP response arrived at all (offline, DNS, refused, timeout).
    bool networkError = false;
    /// The URL the feed was finally served from, after redirects.
    QUrl url;
};

struct BytesResult {
    QByteArray body;
    QString error;
    bool networkError = false;
    bool ok = false;
    QUrl url;
};

struct FileResult {
    bool ok = false;
    QString error;
    bool networkError = false;
};

/**
 * How the catalogue logic reaches a server. The real implementation is
 * OpdsClient; tests substitute a scripted one. Callbacks are invoked at most
 * once, from the event loop, and never after the request was cancelled.
 */
class OpdsTransport
{
public:
    virtual ~OpdsTransport() = default;

    virtual RequestId fetchFeed(const QUrl &url, std::function<void(const FeedResult &)> done) = 0;
    virtual RequestId fetchBytes(const QUrl &url, std::function<void(const BytesResult &)> done) = 0;
    /// Streams `url` into `targetPath` (written atomically on success).
    virtual RequestId downloadFile(const QUrl &url,
                                   const QString &targetPath,
                                   std::function<void(const FileResult &)> done,
                                   std::function<void(qint64 received, qint64 total)> progress = {}) = 0;
    virtual void cancel(RequestId id) = 0;
};

}
