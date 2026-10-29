#include "core/opds/OpdsClient.h"

#include <QFile>
#include <QMap>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace KalibreBox::Opds;

namespace
{

QByteArray fixture(const char *name)
{
    QFile file(QStringLiteral(OPDS_FIXTURE_DIR "/") + QLatin1String(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Missing fixture %s", name);
    return file.readAll();
}

struct Request {
    QString path;
    QMap<QByteArray, QByteArray> headers; // names lower-cased
};

struct Response {
    int status = 200;
    QByteArray body;
    QByteArray contentType = "application/atom+xml";
    QMap<QByteArray, QByteArray> headers;
    int delayMs = 0;
};

/// A just-enough HTTP/1.1 server: one request per connection.
class TestServer : public QObject
{
public:
    TestServer()
    {
        if (!m_server.listen(QHostAddress::Any, 0))
            qFatal("cannot listen");
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *socket = m_server.nextPendingConnection())
                accept(socket);
        });
    }

    quint16 port() const
    {
        return m_server.serverPort();
    }
    QString url(const QString &host, const QString &path) const
    {
        return QStringLiteral("http://%1:%2%3").arg(host).arg(port()).arg(path);
    }
    QString url(const QString &path) const
    {
        return url(QStringLiteral("127.0.0.1"), path);
    }

    std::function<Response(const Request &)> handler;
    QList<Request> seen;

    const Request *last(const QString &path) const
    {
        for (qsizetype i = seen.size() - 1; i >= 0; --i) {
            if (seen.at(i).path == path)
                return &seen.at(i);
        }
        return nullptr;
    }

private:
    void accept(QTcpSocket *socket)
    {
        auto *buffer = new QByteArray;
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            const qsizetype end = buffer->indexOf("\r\n\r\n");
            if (end < 0)
                return;
            Request request;
            const QList<QByteArray> lines = buffer->left(end).split('\n');
            request.path = QString::fromLatin1(lines.first().split(' ').value(1));
            for (qsizetype i = 1; i < lines.size(); ++i) {
                const qsizetype colon = lines.at(i).indexOf(':');
                if (colon > 0)
                    request.headers.insert(lines.at(i).left(colon).trimmed().toLower(), lines.at(i).mid(colon + 1).trimmed());
            }
            buffer->clear();
            seen.append(request);
            const Response response = handler ? handler(request) : Response{};
            QTimer::singleShot(response.delayMs, socket, [socket, response]() {
                if (socket->state() != QAbstractSocket::ConnectedState)
                    return;
                QByteArray head = "HTTP/1.1 " + QByteArray::number(response.status) + " X\r\nContent-Type: " + response.contentType
                    + "\r\nContent-Length: " + QByteArray::number(response.body.size()) + "\r\nConnection: close\r\n";
                for (auto it = response.headers.begin(); it != response.headers.end(); ++it)
                    head += it.key() + ": " + it.value() + "\r\n";
                socket->write(head + "\r\n" + response.body);
                socket->disconnectFromHost();
            });
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QObject::destroyed, [buffer]() {
            delete buffer;
        });
    }

    QTcpServer m_server;
};

Response redirect(int status, const QString &location)
{
    Response r;
    r.status = status;
    r.headers.insert("Location", location.toLatin1());
    return r;
}

}

class tst_opdsclient : public QObject
{
    Q_OBJECT

private slots:
    void sendsBasicCredentialsToTheirHost();
    void sendsNothingWithoutCredentials();
    void keepsCredentialsOnSameHostRedirects();
    void dropsCredentialsWhenRedirectedToAnotherHost();
    void explainsRejectedCredentials();
    void explainsDigestOnlyServers();
    void reportsUnreachableServersAsNetworkErrors();
    void parsesFeedsAndReportsTheFinalUrl();
    void rejectsPagesThatAreNotFeeds();
    void givesUpOnRedirectLoops();
    void downloadsIntoNewFolders();
    void failedDownloadsLeaveNoFile();
    void downloadFollowsRedirects();
    void cancelledRequestsStaySilent();
    void sendsAFeedAcceptHeader();
};

void tst_opdsclient::sendsBasicCredentialsToTheirHost()
{
    TestServer server;
    server.handler = [](const Request &) {
        return Response{200, fixture("gutenberg_root.xml")};
    };
    OpdsClient client;
    client.setCredentials(QUrl(server.url(QStringLiteral("/opds"))), QStringLiteral("reader"), QStringLiteral("s3cret: é"));

    std::optional<FeedResult> result;
    client.fetchFeed(QUrl(server.url(QStringLiteral("/opds"))), [&result](const FeedResult &r) {
        result = r;
    });
    QTRY_VERIFY(result.has_value());

    QVERIFY2(result->feed.has_value(), qPrintable(result->error));
    QCOMPARE(server.last(QStringLiteral("/opds"))->headers.value("authorization"), "Basic " + QByteArray("reader:s3cret: é").toBase64());
}

void tst_opdsclient::sendsNothingWithoutCredentials()
{
    TestServer server;
    server.handler = [](const Request &) {
        return Response{200, fixture("gutenberg_root.xml")};
    };
    OpdsClient client;

    std::optional<FeedResult> result;
    client.fetchFeed(QUrl(server.url(QStringLiteral("/opds"))), [&result](const FeedResult &r) {
        result = r;
    });
    QTRY_VERIFY(result.has_value());
    QVERIFY(!server.last(QStringLiteral("/opds"))->headers.contains("authorization"));

    // Setting an empty user clears credentials again.
    client.setCredentials(QUrl(server.url(QStringLiteral("/opds"))), QStringLiteral("reader"), QStringLiteral("x"));
    client.setCredentials(QUrl(server.url(QStringLiteral("/opds"))), QString(), QString());
    result.reset();
    client.fetchFeed(QUrl(server.url(QStringLiteral("/opds"))), [&result](const FeedResult &r) {
        result = r;
    });
    QTRY_VERIFY(result.has_value());
    QVERIFY(!server.seen.last().headers.contains("authorization"));
}

void tst_opdsclient::keepsCredentialsOnSameHostRedirects()
{
    TestServer server;
    server.handler = [](const Request &request) {
        return request.path == QLatin1String("/start") ? redirect(302, QStringLiteral("/other")) : Response{200, fixture("gutenberg_root.xml")};
    };
    OpdsClient client;
    client.setCredentials(QUrl(server.url(QStringLiteral("/"))), QStringLiteral("reader"), QStringLiteral("pw"));

    std::optional<FeedResult> result;
    client.fetchFeed(QUrl(server.url(QStringLiteral("/start"))), [&result](const FeedResult &r) {
        result = r;
    });
    QTRY_VERIFY(result.has_value());
    QVERIFY(result->feed.has_value());
    QVERIFY(server.last(QStringLiteral("/start"))->headers.contains("authorization"));
    QVERIFY(server.last(QStringLiteral("/other"))->headers.contains("authorization"));
}

void tst_opdsclient::dropsCredentialsWhenRedirectedToAnotherHost()
{
    TestServer server;
    // "localhost" and "127.0.0.1" are different hosts as far as credentials go.
    const QString elsewhere = server.url(QStringLiteral("localhost"), QStringLiteral("/other"));
    server.handler = [elsewhere](const Request &request) {
        return request.path == QLatin1String("/start") ? redirect(302, elsewhere) : Response{200, fixture("gutenberg_root.xml")};
    };
    OpdsClient client;
    client.setCredentials(QUrl(server.url(QStringLiteral("/"))), QStringLiteral("reader"), QStringLiteral("pw"));

    std::optional<FeedResult> result;
    client.fetchFeed(QUrl(server.url(QStringLiteral("/start"))), [&result](const FeedResult &r) {
        result = r;
    });
    QTRY_VERIFY(result.has_value());
    QVERIFY2(result->feed.has_value(), qPrintable(result->error));

    QVERIFY(server.last(QStringLiteral("/start"))->headers.contains("authorization"));
    QVERIFY(!server.last(QStringLiteral("/other"))->headers.contains("authorization"));
}

void tst_opdsclient::explainsRejectedCredentials()
{
    TestServer server;
    server.handler = [](const Request &) {
        Response r;
        r.status = 401;
        r.headers.insert("WWW-Authenticate", "Basic realm=\"calibre\"");
        return r;
    };
    OpdsClient client;

    std::optional<FeedResult> result;
    client.fetchFeed(QUrl(server.url(QStringLiteral("/opds"))), [&result](const FeedResult &r) {
        result = r;
    });
    QTRY_VERIFY(result.has_value());
    QVERIFY(!result->feed.has_value());
    QVERIFY(!result->networkError);
    QVERIFY2(result->error.contains(QStringLiteral("401")), qPrintable(result->error));
    QVERIFY(!result->error.contains(QStringLiteral("Digest")));
}

void tst_opdsclient::explainsDigestOnlyServers()
{
    TestServer server;
    server.handler = [](const Request &) {
        Response r;
        r.status = 401;
        r.headers.insert("WWW-Authenticate", "Digest realm=\"calibre\", nonce=\"abc\"");
        return r;
    };
    OpdsClient client;

    std::optional<BytesResult> result;
    client.fetchBytes(QUrl(server.url(QStringLiteral("/opds"))), [&result](const BytesResult &r) {
        result = r;
    });
    QTRY_VERIFY(result.has_value());
    QVERIFY(!result->ok);
    QVERIFY2(result->error.contains(QStringLiteral("Digest")), qPrintable(result->error));
    QVERIFY(result->error.contains(QStringLiteral("Basic")));
}

void tst_opdsclient::reportsUnreachableServersAsNetworkErrors()
{
    quint16 closedPort = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        closedPort = probe.serverPort();
    }
    OpdsClient client;
    std::optional<FeedResult> result;
    client.fetchFeed(QUrl(QStringLiteral("http://127.0.0.1:%1/opds").arg(closedPort)), [&result](const FeedResult &r) {
        result = r;
    });
    QTRY_VERIFY(result.has_value());
    QVERIFY(result->networkError);
    QVERIFY(!result->error.isEmpty());
    QVERIFY(!result->feed.has_value());
}

void tst_opdsclient::parsesFeedsAndReportsTheFinalUrl()
{
    TestServer server;
    server.handler = [](const Request &request) {
        if (request.path == QLatin1String("/old"))
            return redirect(301, QStringLiteral("/new/feed"));
        Response r{200, fixture("gutenberg_root.xml")};
        r.contentType = "application/atom+xml;profile=opds-catalog;kind=navigation";
        return r;
    };
    OpdsClient client;

    std::optional<FeedResult> result;
    client.fetchFeed(QUrl(server.url(QStringLiteral("/old"))), [&result](const FeedResult &r) {
        result = r;
    });
    QTRY_VERIFY(result.has_value());
    QVERIFY2(result->feed.has_value(), qPrintable(result->error));
    QCOMPARE(result->url, QUrl(server.url(QStringLiteral("/new/feed"))));
    QCOMPARE(result->feed->title, QStringLiteral("Project Gutenberg"));
    // Relative links resolve against where the feed really came from.
    QCOMPARE(result->feed->selfLink()->href, server.url(QStringLiteral("/ebooks.opds/")));
}

void tst_opdsclient::rejectsPagesThatAreNotFeeds()
{
    TestServer server;
    server.handler = [](const Request &) {
        Response r{200, "<html><body>Please log in</body></html>"};
        r.contentType = "text/html";
        return r;
    };
    OpdsClient client;

    std::optional<FeedResult> result;
    client.fetchFeed(QUrl(server.url(QStringLiteral("/opds"))), [&result](const FeedResult &r) {
        result = r;
    });
    QTRY_VERIFY(result.has_value());
    QVERIFY(!result->feed.has_value());
    QVERIFY(!result->networkError);
    QVERIFY(!result->error.isEmpty());
}

void tst_opdsclient::givesUpOnRedirectLoops()
{
    TestServer server;
    server.handler = [](const Request &) {
        return redirect(302, QStringLiteral("/again"));
    };
    OpdsClient client;

    std::optional<BytesResult> result;
    client.fetchBytes(QUrl(server.url(QStringLiteral("/again"))), [&result](const BytesResult &r) {
        result = r;
    });
    QTRY_VERIFY(result.has_value());
    QVERIFY(!result->ok);
    QVERIFY2(result->error.contains(QStringLiteral("redirect"), Qt::CaseInsensitive), qPrintable(result->error));
}

void tst_opdsclient::downloadsIntoNewFolders()
{
    QByteArray payload(300 * 1024, 'x');
    for (qsizetype i = 0; i < payload.size(); i += 97)
        payload[i] = static_cast<char>('a' + (i / 97) % 26);

    TestServer server;
    server.handler = [payload](const Request &) {
        Response r{200, payload};
        r.contentType = "application/epub+zip";
        return r;
    };
    QTemporaryDir dir;
    const QString target = dir.filePath(QStringLiteral("made/on/demand/book.epub"));

    OpdsClient client;
    std::optional<FileResult> result;
    qint64 lastReceived = 0;
    client.downloadFile(
        QUrl(server.url(QStringLiteral("/book"))),
        target,
        [&result](const FileResult &r) {
            result = r;
        },
        [&lastReceived](qint64 received, qint64) {
            lastReceived = received;
        });
    QTRY_VERIFY(result.has_value());

    QVERIFY2(result->ok, qPrintable(result->error));
    QFile file(target);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), payload);
    QCOMPARE(lastReceived, qint64(payload.size()));
}

void tst_opdsclient::failedDownloadsLeaveNoFile()
{
    TestServer server;
    server.handler = [](const Request &) {
        return Response{404, "<html>missing</html>", "text/html"};
    };
    QTemporaryDir dir;
    const QString target = dir.filePath(QStringLiteral("book.epub"));
    QFile existing(target);
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.write("keep me");
    existing.close();

    OpdsClient client;
    std::optional<FileResult> result;
    client.downloadFile(QUrl(server.url(QStringLiteral("/book"))), target, [&result](const FileResult &r) {
        result = r;
    });
    QTRY_VERIFY(result.has_value());

    QVERIFY(!result->ok);
    QVERIFY2(result->error.contains(QStringLiteral("404")), qPrintable(result->error));
    // The error page was not written, and the earlier file is untouched.
    QVERIFY(existing.open(QIODevice::ReadOnly));
    QCOMPARE(existing.readAll(), QByteArray("keep me"));
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 1);
}

void tst_opdsclient::downloadFollowsRedirects()
{
    TestServer server;
    server.handler = [](const Request &request) {
        if (request.path == QLatin1String("/get/1"))
            return redirect(302, QStringLiteral("/files/book.epub"));
        Response r{200, "EPUBDATA"};
        r.contentType = "application/epub+zip";
        return r;
    };
    QTemporaryDir dir;
    const QString target = dir.filePath(QStringLiteral("book.epub"));

    OpdsClient client;
    std::optional<FileResult> result;
    client.downloadFile(QUrl(server.url(QStringLiteral("/get/1"))), target, [&result](const FileResult &r) {
        result = r;
    });
    QTRY_VERIFY(result.has_value());

    QVERIFY2(result->ok, qPrintable(result->error));
    QFile file(target);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("EPUBDATA"));
}

void tst_opdsclient::cancelledRequestsStaySilent()
{
    TestServer server;
    server.handler = [](const Request &) {
        Response r{200, fixture("gutenberg_root.xml")};
        r.delayMs = 150;
        return r;
    };
    OpdsClient client;

    bool called = false;
    const RequestId id = client.fetchFeed(QUrl(server.url(QStringLiteral("/opds"))), [&called](const FeedResult &) {
        called = true;
    });
    QTRY_VERIFY(!server.seen.isEmpty());
    client.cancel(id);
    QTest::qWait(400);
    QVERIFY(!called);

    // Destroying the client with a request in flight is just as quiet.
    {
        OpdsClient shortLived;
        shortLived.fetchFeed(QUrl(server.url(QStringLiteral("/opds"))), [&called](const FeedResult &) {
            called = true;
        });
    }
    QTest::qWait(300);
    QVERIFY(!called);
}

void tst_opdsclient::sendsAFeedAcceptHeader()
{
    TestServer server;
    server.handler = [](const Request &) {
        return Response{200, fixture("gutenberg_root.xml")};
    };
    OpdsClient client;

    std::optional<FeedResult> result;
    client.fetchFeed(QUrl(server.url(QStringLiteral("/opds"))), [&result](const FeedResult &r) {
        result = r;
    });
    QTRY_VERIFY(result.has_value());

    const QByteArray accept = server.last(QStringLiteral("/opds"))->headers.value("accept");
    QVERIFY(accept.contains("application/opds+json"));
    QVERIFY(accept.contains("application/atom+xml"));
}

QTEST_MAIN(tst_opdsclient)

#include "tst_opdsclient.moc"
