#include "core/opds/OpdsClient.h"

#include "core/opds/OpdsParser.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>

#include <memory>

namespace KalibreBox::Opds
{

namespace
{

constexpr int maxRedirects = 8;
constexpr int transferTimeoutMs = 30000;

const QByteArray acceptFeeds = QByteArrayLiteral(
    "application/opds+json, application/atom+xml;profile=opds-catalog, application/atom+xml;q=0.9, "
    "application/xml;q=0.8, */*;q=0.5");

bool isRedirectStatus(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

int statusOf(const QNetworkReply *reply)
{
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

bool isSuccess(int status)
{
    return status >= 200 && status < 300;
}

}

OpdsClient::OpdsClient(QObject *parent)
    : QObject(parent)
{
}

OpdsClient::~OpdsClient()
{
    // Cleared first so the finished handlers see the requests as cancelled.
    const auto replies = std::exchange(m_active, {});
    for (const QPointer<QNetworkReply> &reply : replies) {
        if (reply)
            reply->abort();
    }
}

void OpdsClient::setCredentials(const QUrl &scopeUrl, const QString &user, const QString &password)
{
    if (user.isEmpty()) {
        m_authHost.clear();
        m_authScheme.clear();
        m_authHeader.clear();
        return;
    }
    m_authHost = scopeUrl.host().toLower();
    m_authScheme = scopeUrl.scheme();
    m_authHeader = "Basic " + (user + QLatin1Char(':') + password).toUtf8().toBase64();
}

bool OpdsClient::attachCredentials(const QUrl &url) const
{
    if (m_authHeader.isEmpty() || url.host().toLower() != m_authHost)
        return false;
    // Never send them over a connection weaker than the one they were set up for.
    return !(m_authScheme == QLatin1String("https") && url.scheme() != QLatin1String("https"));
}

QNetworkReply *OpdsClient::send(const QUrl &url, bool wantFeed)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(transferTimeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KalibreBox/%1").arg(QCoreApplication::applicationVersion()));
    request.setRawHeader("Accept", wantFeed ? acceptFeeds : QByteArrayLiteral("*/*"));
    if (attachCredentials(url))
        request.setRawHeader("Authorization", m_authHeader);
    return m_nam.get(request);
}

QUrl OpdsClient::redirectTarget(QNetworkReply *reply, const QUrl &from, int hopsLeft, QString *error) const
{
    // The parsed LocationHeader drops relative locations, which servers use freely.
    const QUrl location = QUrl::fromEncoded(reply->rawHeader("Location").trimmed(), QUrl::TolerantMode);
    if (!location.isValid() || location.isEmpty()) {
        *error = QStringLiteral("The server sent a redirect without a location.");
        return {};
    }
    if (hopsLeft <= 0) {
        *error = QStringLiteral("Too many redirects.");
        return {};
    }
    QUrl target = from.resolved(location);
    const bool webScheme = target.scheme() == QLatin1String("http") || target.scheme() == QLatin1String("https");
    if (!webScheme) {
        *error = QStringLiteral("The server redirected to an unsupported address.");
        return {};
    }
    if (from.scheme() == QLatin1String("https") && target.scheme() == QLatin1String("http")) {
        *error = QStringLiteral("Refusing a redirect from https to http.");
        return {};
    }
    return target;
}

QString OpdsClient::describeFailure(QNetworkReply *reply, int status, bool *networkError)
{
    *networkError = status == 0;
    if (status == 0)
        return reply->errorString();

    if (status == 401) {
        QString message = QStringLiteral("The server requires a valid username and password (HTTP 401).");
        const QString challenge = QString::fromLatin1(reply->rawHeader("WWW-Authenticate"));
        if (challenge.startsWith(QLatin1String("Digest"), Qt::CaseInsensitive))
            message += QStringLiteral(" It asks for Digest authentication, which is not supported; enable Basic authentication on the server.");
        return message;
    }
    if (status == 403)
        return QStringLiteral("Access denied (HTTP 403).");
    if (status == 404)
        return QStringLiteral("Not found (HTTP 404).");
    const QString reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    return QStringLiteral("HTTP %1 %2").arg(status).arg(reason).trimmed();
}

void OpdsClient::getDocument(RequestId id, const QUrl &url, bool wantFeed, int hops, std::function<void(const Outcome &)> done)
{
    QNetworkReply *reply = send(url, wantFeed);
    m_active.insert(id, reply);

    auto oversize = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::downloadProgress, this, [reply, oversize](qint64 received, qint64) {
        if (received > maxDocumentBytes && !*oversize) {
            *oversize = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, url, wantFeed, hops, done, oversize]() {
        reply->deleteLater();
        if (m_active.value(id) != reply)
            return; // cancelled

        const int status = statusOf(reply);
        if (isRedirectStatus(status)) {
            QString error;
            const QUrl target = redirectTarget(reply, url, hops, &error);
            if (target.isEmpty()) {
                m_active.remove(id);
                Outcome failure;
                failure.error = std::move(error);
                failure.url = url;
                done(failure);
                return;
            }
            getDocument(id, target, wantFeed, hops - 1, done);
            return;
        }

        m_active.remove(id);
        Outcome outcome;
        outcome.url = url;
        if (*oversize) {
            outcome.error = QStringLiteral("The document is larger than the allowed size.");
        } else if (reply->error() != QNetworkReply::NoError || !isSuccess(status)) {
            outcome.error = describeFailure(reply, status, &outcome.networkError);
        } else {
            outcome.ok = true;
            outcome.body = reply->readAll();
            outcome.contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        }
        done(outcome);
    });
}

RequestId OpdsClient::fetchFeed(const QUrl &url, std::function<void(const FeedResult &)> done)
{
    const RequestId id = ++m_nextId;
    getDocument(id, url, true, maxRedirects, [done = std::move(done)](const Outcome &outcome) {
        FeedResult result;
        result.url = outcome.url;
        result.networkError = outcome.networkError;
        if (!outcome.ok) {
            result.error = outcome.error;
        } else {
            result.feed = parseFeed(outcome.body, outcome.url, outcome.contentType, &result.error);
        }
        done(result);
    });
    return id;
}

RequestId OpdsClient::fetchBytes(const QUrl &url, std::function<void(const BytesResult &)> done)
{
    const RequestId id = ++m_nextId;
    getDocument(id, url, false, maxRedirects, [done = std::move(done)](const Outcome &outcome) {
        BytesResult result;
        result.ok = outcome.ok;
        result.error = outcome.error;
        result.networkError = outcome.networkError;
        result.body = outcome.body;
        result.url = outcome.url;
        done(result);
    });
    return id;
}

void OpdsClient::downloadHop(RequestId id,
                             const QUrl &url,
                             const QString &target,
                             int hops,
                             std::function<void(const FileResult &)> done,
                             std::function<void(qint64, qint64)> progress)
{
    QNetworkReply *reply = send(url, false);
    m_active.insert(id, reply);

    auto file = std::make_shared<QSaveFile>(target);
    auto failure = std::make_shared<QString>();

    const auto openTarget = [file, target]() {
        if (file->isOpen())
            return true;
        QDir().mkpath(QFileInfo(target).absolutePath());
        return file->open(QIODevice::WriteOnly);
    };

    connect(reply, &QNetworkReply::readyRead, this, [reply, file, failure, openTarget]() {
        if (!isSuccess(statusOf(reply))) {
            reply->readAll(); // an error page or a redirect body, not the file
            return;
        }
        if (!openTarget() || file->write(reply->readAll()) < 0) {
            *failure = file->errorString();
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [progress](qint64 received, qint64 total) {
        if (progress)
            progress(received, total);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, url, target, hops, done, progress, file, failure, openTarget]() {
        reply->deleteLater();
        if (m_active.value(id) != reply)
            return; // cancelled; the QSaveFile discards its temporary file

        const int status = statusOf(reply);
        if (isRedirectStatus(status)) {
            QString error;
            const QUrl next = redirectTarget(reply, url, hops, &error);
            if (next.isEmpty()) {
                m_active.remove(id);
                FileResult result;
                result.error = std::move(error);
                done(result);
                return;
            }
            downloadHop(id, next, target, hops - 1, done, progress);
            return;
        }

        m_active.remove(id);
        FileResult result;
        if (!failure->isEmpty()) {
            result.error = *failure;
        } else if (reply->error() != QNetworkReply::NoError || !isSuccess(status)) {
            result.error = describeFailure(reply, status, &result.networkError);
        } else if (!openTarget() || file->write(reply->readAll()) < 0 || !file->commit()) {
            result.error = QStringLiteral("Could not save %1: %2").arg(target, file->errorString());
        } else {
            result.ok = true;
        }
        done(result);
    });
}

RequestId
OpdsClient::downloadFile(const QUrl &url, const QString &targetPath, std::function<void(const FileResult &)> done, std::function<void(qint64, qint64)> progress)
{
    const RequestId id = ++m_nextId;
    downloadHop(id, url, targetPath, maxRedirects, std::move(done), std::move(progress));
    return id;
}

void OpdsClient::cancel(RequestId id)
{
    const QPointer<QNetworkReply> reply = m_active.take(id);
    if (reply)
        reply->abort();
}

}
