#include "core/opds/OpdsParser.h"
#include "core/opds/OpdsSource.h"

#include <QFile>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

using namespace KalibreBox;
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

QString key(const QString &url)
{
    return QUrl(url).toString(QUrl::FullyEncoded);
}

QByteArray atomFeed(const QByteArray &links, const QByteArray &entries)
{
    return "<?xml version='1.0' encoding='utf-8'?><feed xmlns='http://www.w3.org/2005/Atom'><id>x</id><title>T</title>" + links + entries + "</feed>";
}

QByteArray bookEntry(const QByteArray &id, const QByteArray &title)
{
    return "<entry><id>" + id + "</id><title>" + title
        + "</title><author><name>A. Author</name></author><link rel='http://opds-spec.org/acquisition' "
          "type='application/epub+zip' href='/dl/"
        + id + ".epub'/></entry>";
}

/// Answers from a table, a tick after being asked, like a network would.
class FakeTransport : public OpdsTransport
{
public:
    struct Route {
        QByteArray body;
        QString contentType = QStringLiteral("application/atom+xml");
        QUrl finalUrl; // when the server redirected somewhere else
        bool networkError = false;
        QString error;
    };

    QHash<QString, Route> routes;
    QStringList requests;
    bool hold = false;
    bool ignoreCancel = false;

    void route(const QString &url, const QByteArray &body, const QUrl &finalUrl = {})
    {
        Route r;
        r.body = body;
        r.finalUrl = finalUrl;
        routes.insert(key(url), r);
    }

    void releaseAll()
    {
        while (!m_pending.isEmpty())
            m_pending.takeFirst().deliver();
    }

    RequestId fetchFeed(const QUrl &url, std::function<void(const FeedResult &)> done) override
    {
        const RequestId id = begin(url);
        schedule(id, [this, url, done = std::move(done)] {
            FeedResult result;
            const auto it = routes.constFind(key(url.toString()));
            result.url = url;
            if (it == routes.constEnd()) {
                result.error = QStringLiteral("no route for %1").arg(url.toString());
            } else if (it->networkError || !it->error.isEmpty()) {
                result.networkError = it->networkError;
                result.error = it->error;
            } else {
                result.url = it->finalUrl.isValid() ? it->finalUrl : url;
                result.feed = parseFeed(it->body, result.url, it->contentType, &result.error);
            }
            done(result);
        });
        return id;
    }

    RequestId fetchBytes(const QUrl &url, std::function<void(const BytesResult &)> done) override
    {
        const RequestId id = begin(url);
        schedule(id, [this, url, done = std::move(done)] {
            BytesResult result;
            result.url = url;
            const auto it = routes.constFind(key(url.toString()));
            if (it == routes.constEnd()) {
                result.error = QStringLiteral("no route");
            } else {
                result.ok = true;
                result.body = it->body;
            }
            done(result);
        });
        return id;
    }

    RequestId downloadFile(const QUrl &, const QString &, std::function<void(const FileResult &)>, std::function<void(qint64, qint64)>) override
    {
        return ++m_next;
    }

    void cancel(RequestId id) override
    {
        if (ignoreCancel)
            return;
        m_cancelled.insert(id);
        for (qsizetype i = 0; i < m_pending.size(); ++i) {
            if (m_pending.at(i).id == id) {
                m_pending.removeAt(i);
                break;
            }
        }
    }

private:
    struct Pending {
        RequestId id;
        std::function<void()> deliver;
    };

    RequestId begin(const QUrl &url)
    {
        requests.append(key(url.toString()));
        return ++m_next;
    }

    void schedule(RequestId id, std::function<void()> deliver)
    {
        if (hold) {
            m_pending.append({.id = id, .deliver = std::move(deliver)});
            return;
        }
        QTimer::singleShot(0, [this, id, deliver = std::move(deliver)] {
            if (!m_cancelled.contains(id))
                deliver();
        });
    }

    RequestId m_next = 0;
    QSet<RequestId> m_cancelled;
    QList<Pending> m_pending;
};

/// Records what a source reports.
struct Collector {
    explicit Collector(OpdsSource *source)
    {
        QObject::connect(source, &OpdsSource::reset, source, [this] {
            books.clear();
        });
        QObject::connect(source, &OpdsSource::booksAdded, source, [this](const QList<Book> &added) {
            books += added;
        });
        QObject::connect(source, &OpdsSource::failed, source, [this](const QString &message, bool offline) {
            failures.append(message);
            offlineFlags.append(offline);
        });
    }
    QList<Book> books;
    QStringList failures;
    QList<bool> offlineFlags;
};

const QString gutenbergRoot = QStringLiteral("https://www.gutenberg.org/ebooks.opds/");
const QString gutenbergLatest = QStringLiteral("https://www.gutenberg.org/ebooks/search.opds/?sort_order=release_date");
const QString gutenbergLatest2 = gutenbergLatest + QStringLiteral("&start_index=26");
const QString gutenbergOsd = QStringLiteral("https://www.gutenberg.org/catalog/osd-books.xml");
const QString gutenbergSearch = QStringLiteral("http://m.gutenberg.org/ebooks/search.opds/?query=dickens");
const QString calibreRoot = QStringLiteral("http://192.168.1.10:8080/opds");
// The "next" link of the Calibre fixture.
const QString calibreNext = QStringLiteral("http://192.168.1.10:8080/opds/navcatalog/4F6E6577657374?offset=2&library_id=My_Library");

void routeGutenberg(FakeTransport &transport)
{
    transport.route(gutenbergRoot, fixture("gutenberg_root.xml"));
    transport.route(gutenbergLatest, fixture("gutenberg_latest.xml"));
    transport.route(gutenbergLatest2, fixture("gutenberg_latest_page2.xml"));
    transport.route(gutenbergOsd, fixture("gutenberg_osd.xml"));
    // The server redirects the mobile host to www; links in the result are relative to that.
    transport.route(gutenbergSearch, fixture("gutenberg_search.xml"), QUrl(QStringLiteral("https://www.gutenberg.org/ebooks/search.opds/?query=dickens")));
    transport.route(QStringLiteral("https://www.gutenberg.org/ebooks/79876.opds"), fixture("gutenberg_book.xml"));
}

}

class tst_opdssource : public QObject
{
    Q_OBJECT

private slots:
    void gutenbergLatestNeedsOnePageForABatch();
    void gutenbergLargerBatchSpansPages();
    void fetchMoreLoadsTheNextBatch();
    void gutenbergSearchGoesThroughOpenSearch();
    void gutenbergBooksAreStubsUntilDetailsLoad();
    void detailsFailureIsReported();

    void calibreRootIsTheListing();
    void calibreSearchUsesTheTemplate();

    void searchWithoutServerSupportIsLocalOnly();
    void staleResponsesAreDropped();
    void offlineIsReported();
    void emptyResultsAreReported();
    void selfLinkingFeedsDoNotLoop();
    void batchSizeHasAFloor();

    void formatLabelsExtensionsAndFileNames();
};

void tst_opdssource::gutenbergLatestNeedsOnePageForABatch()
{
    FakeTransport transport;
    routeGutenberg(transport);
    OpdsSource source(&transport);
    Collector collected(&source);
    source.setRoot(QUrl(gutenbergRoot));
    source.setBatchSize(20);

    source.load({});
    QVERIFY(source.loading());
    QTRY_VERIFY(!source.loading());

    // Root, then the "Latest" entry it advertises; 25 books already cover a batch of 20.
    QCOMPARE(transport.requests, (QStringList{key(gutenbergRoot), key(gutenbergLatest)}));
    QCOMPARE(collected.books.size(), 25);
    QVERIFY(collected.failures.isEmpty());
    QVERIFY(source.canLoadMore());

    const Book &first = collected.books.first();
    QCOMPARE(first.title, QStringLiteral("The single track"));
    QCOMPARE(first.authors, QStringLiteral("Isabel Ostrander"));
    QCOMPARE(first.detailsUrl, QStringLiteral("https://www.gutenberg.org/ebooks/79876.opds"));
    QCOMPARE(first.coverUrl, QStringLiteral("https://www.gutenberg.org/cache/epub/79876/pg79876.cover.small.jpg"));
    QVERIFY(first.hasCover);
    QVERIFY(first.formats.isEmpty());

    // The "follow us" entries link to other sites and are not books.
    QSet<qlonglong> ids;
    for (const Book &book : std::as_const(collected.books)) {
        QVERIFY(!book.title.contains(QStringLiteral("Follow new books")));
        QVERIFY(book.id > 0);
        ids.insert(book.id);
    }
    QCOMPARE(ids.size(), collected.books.size());
}

void tst_opdssource::gutenbergLargerBatchSpansPages()
{
    FakeTransport transport;
    routeGutenberg(transport);
    OpdsSource source(&transport);
    Collector const collected(&source);
    source.setRoot(QUrl(gutenbergRoot));
    source.setBatchSize(40);

    source.load({});
    QTRY_VERIFY(!source.loading());

    QCOMPARE(transport.requests, (QStringList{key(gutenbergRoot), key(gutenbergLatest), key(gutenbergLatest2)}));
    QVERIFY(collected.books.size() >= 40);
    QVERIFY(collected.failures.isEmpty());
}

void tst_opdssource::fetchMoreLoadsTheNextBatch()
{
    FakeTransport transport;
    routeGutenberg(transport);
    OpdsSource source(&transport);
    Collector collected(&source);
    source.setRoot(QUrl(gutenbergRoot));
    source.setBatchSize(20);

    source.load({});
    QTRY_VERIFY(!source.loading());
    const qsizetype firstBatch = collected.books.size();

    source.fetchMore();
    QVERIFY(source.loading());
    // Nothing starts while a batch is in flight.
    source.fetchMore();
    QTRY_VERIFY(!source.loading());

    QCOMPARE(transport.requests.last(), key(gutenbergLatest2));
    QCOMPARE(transport.requests.count(key(gutenbergLatest2)), 1);
    QVERIFY(collected.books.size() > firstBatch);

    QSet<QString> titles;
    for (const Book &book : std::as_const(collected.books))
        titles.insert(book.detailsUrl);
    QCOMPARE(titles.size(), collected.books.size());
}

void tst_opdssource::gutenbergSearchGoesThroughOpenSearch()
{
    FakeTransport transport;
    routeGutenberg(transport);
    OpdsSource source(&transport);
    Collector collected(&source);
    source.setRoot(QUrl(gutenbergRoot));
    source.setBatchSize(20);

    source.load(QStringLiteral("  dickens "));
    QTRY_VERIFY(!source.loading());

    // The root advertises a description document; its template is the search endpoint.
    QCOMPARE(transport.requests, (QStringList{key(gutenbergRoot), key(gutenbergOsd), key(gutenbergSearch)}));
    QVERIFY(!source.searchIsLocalOnly());
    // The Authors/Subjects shortcuts in the results are not books.
    QCOMPARE(collected.books.size(), 25);
    QCOMPARE(collected.books.first().title, QStringLiteral("A Tale of Two Cities"));
    QCOMPARE(collected.books.first().authors, QStringLiteral("Charles Dickens"));
    QCOMPARE(collected.books.first().detailsUrl, QStringLiteral("https://www.gutenberg.org/ebooks/98.opds"));

    // The description is only fetched once.
    source.load(QStringLiteral("dickens"));
    QTRY_VERIFY(!source.loading());
    QCOMPARE(transport.requests.count(key(gutenbergOsd)), 1);
    QCOMPARE(transport.requests.count(key(gutenbergRoot)), 1);
    QCOMPARE(collected.books.size(), 25);
}

void tst_opdssource::gutenbergBooksAreStubsUntilDetailsLoad()
{
    FakeTransport transport;
    routeGutenberg(transport);
    OpdsSource source(&transport);
    Collector collected(&source);
    source.setRoot(QUrl(gutenbergRoot));
    source.load({});
    QTRY_VERIFY(!source.loading());

    const Book stub = collected.books.first();
    QSignalSpy ready(&source, &OpdsSource::detailsReady);
    source.loadDetails(stub);
    // Asking again while it is in flight does nothing.
    source.loadDetails(stub);
    QTRY_COMPARE(ready.count(), 1);
    QCOMPARE(transport.requests.count(key(stub.detailsUrl)), 1);

    const Book book = ready.first().first().value<Book>();
    QCOMPARE(book.id, stub.id);
    QCOMPARE(book.title, stub.title);
    QVERIFY(book.detailsUrl.isEmpty());
    // Both editions are offered: one plain EPUB, then the editions with images.
    QCOMPARE(book.formats.size(), 5);
    QCOMPARE(book.formats.first().format, QStringLiteral("EPUB (older e-readers, no images)"));
    QCOMPARE(book.formats.first().extension, QStringLiteral("epub"));
    QCOMPARE(book.formats.first().url, QStringLiteral("https://www.gutenberg.org/ebooks/79876.epub.noimages"));
    QCOMPARE(book.formats.first().fileName, QStringLiteral("The single track"));
    QSet<QString> labels;
    bool hasMobi = false;
    for (const BookFormat &format : book.formats) {
        labels.insert(format.format);
        hasMobi = hasMobi || format.extension == QLatin1String("mobi");
    }
    QCOMPARE(labels.size(), 5);
    QVERIFY(hasMobi);
    QVERIFY(book.comment.contains(QStringLiteral("Railroads -- Fiction")));
}

void tst_opdssource::detailsFailureIsReported()
{
    FakeTransport transport;
    OpdsSource source(&transport);
    Book stub;
    stub.id = 7;
    stub.title = QStringLiteral("Gone");
    stub.detailsUrl = QStringLiteral("https://example.org/ebooks/1.opds");

    QSignalSpy failed(&source, &OpdsSource::detailsFailed);
    source.loadDetails(stub);
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(failed.first().at(0).toLongLong(), qlonglong(7));
    // The failure is not sticky: the same book can be tried again.
    source.loadDetails(stub);
    QTRY_COMPARE(failed.count(), 2);
}

void tst_opdssource::calibreRootIsTheListing()
{
    FakeTransport transport;
    transport.route(calibreRoot, fixture("calibre_newest.xml"));
    transport.route(calibreNext, atomFeed({}, bookEntry("c", "Third")));
    OpdsSource source(&transport);
    Collector const collected(&source);
    source.setRoot(QUrl(calibreRoot));

    source.load({});
    QTRY_VERIFY(!source.loading());

    // The root already lists books, so it is fetched once; two books do not fill a
    // batch of 20, so the next page follows.
    QCOMPARE(transport.requests, (QStringList{key(calibreRoot), key(calibreNext)}));
    QCOMPARE(collected.books.size(), 3); // the "By Author" entry is navigation, not a book
    QVERIFY(collected.failures.isEmpty());
    QVERIFY(!source.canLoadMore());
    QCOMPARE(collected.books.at(2).title, QStringLiteral("Third"));

    const Book &dune = collected.books.at(0);
    QCOMPARE(dune.title, QStringLiteral("Dune"));
    QCOMPARE(dune.authors, QStringLiteral("Frank Herbert"));
    QVERIFY(dune.detailsUrl.isEmpty());
    QCOMPARE(dune.formats.size(), 2);
    QCOMPARE(dune.formats.at(0).format, QStringLiteral("EPUB"));
    QCOMPARE(dune.formats.at(0).url, QStringLiteral("http://192.168.1.10:8080/get/EPUB/12/My_Library"));
    QCOMPARE(dune.formats.at(1).format, QStringLiteral("MOBI"));
    QCOMPARE(dune.formats.at(1).extension, QStringLiteral("mobi"));
    QCOMPARE(dune.coverUrl, QStringLiteral("http://192.168.1.10:8080/get/thumb/12/My_Library?sz=60x80"));
    QCOMPARE(collected.books.at(1).authors, QStringLiteral("Jane Doe, John Roe"));

    // A refresh asks the server again instead of reusing the root from discovery.
    source.load({});
    QTRY_VERIFY(!source.loading());
    QCOMPARE(transport.requests.count(key(calibreRoot)), 2);
    QCOMPARE(collected.books.size(), 3);
}

void tst_opdssource::calibreSearchUsesTheTemplate()
{
    FakeTransport transport;
    transport.route(calibreRoot, fixture("calibre_newest.xml"));
    transport.route(QStringLiteral("http://192.168.1.10:8080/opds/search/frank%20herbert?library_id=My_Library"), fixture("calibre_newest.xml"));
    transport.route(calibreNext, atomFeed({}, {}));
    OpdsSource source(&transport);
    Collector const collected(&source);
    source.setRoot(QUrl(calibreRoot));

    source.load(QStringLiteral("frank herbert"));
    QTRY_VERIFY(!source.loading());
    QCOMPARE(transport.requests.size(), 3);
    QCOMPARE(transport.requests.at(1), key(QStringLiteral("http://192.168.1.10:8080/opds/search/frank%20herbert?library_id=My_Library")));
    QVERIFY(!source.searchIsLocalOnly());
    QCOMPARE(collected.books.size(), 2);
}

void tst_opdssource::searchWithoutServerSupportIsLocalOnly()
{
    FakeTransport transport;
    transport.route(calibreRoot, atomFeed({}, bookEntry("a", "Alpha") + bookEntry("b", "Beta")));
    OpdsSource source(&transport);
    Collector const collected(&source);
    source.setRoot(QUrl(calibreRoot));

    source.load(QStringLiteral("alp"));
    QTRY_VERIFY(!source.loading());
    // The listing is loaded as usual; filtering it is up to the caller.
    QVERIFY(source.searchIsLocalOnly());
    QCOMPARE(collected.books.size(), 2);

    source.load({});
    QTRY_VERIFY(!source.loading());
    QVERIFY(!source.searchIsLocalOnly());
}

void tst_opdssource::staleResponsesAreDropped()
{
    FakeTransport transport;
    routeGutenberg(transport);
    transport.hold = true;
    // Even a transport that still delivers cancelled requests must not leak into the new listing.
    transport.ignoreCancel = true;
    OpdsSource source(&transport);
    Collector collected(&source);
    source.setRoot(QUrl(gutenbergRoot));
    source.setBatchSize(20);

    source.load({});
    source.load(QStringLiteral("dickens"));
    transport.releaseAll();
    // Releasing may queue follow-up requests; keep delivering until nothing is pending.
    for (int i = 0; i < 10 && source.loading(); ++i)
        transport.releaseAll();

    QVERIFY(!source.loading());
    QVERIFY(collected.failures.isEmpty());
    QCOMPARE(collected.books.size(), 25);
    QCOMPARE(collected.books.first().title, QStringLiteral("A Tale of Two Cities"));
}

void tst_opdssource::offlineIsReported()
{
    FakeTransport transport;
    FakeTransport::Route down;
    down.networkError = true;
    down.error = QStringLiteral("Host not found");
    transport.routes.insert(key(gutenbergRoot), down);
    OpdsSource source(&transport);
    Collector collected(&source);
    source.setRoot(QUrl(gutenbergRoot));

    source.load({});
    QTRY_VERIFY(!source.loading());

    QVERIFY(collected.books.isEmpty());
    QCOMPARE(collected.failures, QStringList{QStringLiteral("Host not found")});
    QCOMPARE(collected.offlineFlags, QList<bool>{true});
    QVERIFY(!source.canLoadMore());

    // An HTTP-level failure is not "offline".
    transport.routes.clear();
    source.load({});
    QTRY_VERIFY(!source.loading());
    QCOMPARE(collected.offlineFlags.last(), false);
}

void tst_opdssource::emptyResultsAreReported()
{
    FakeTransport transport;
    transport.route(calibreRoot, atomFeed("<link rel='search' type='application/atom+xml' href='/opds/search/{searchTerms}'/>", bookEntry("a", "Alpha")));
    transport.route(QStringLiteral("http://192.168.1.10:8080/opds/search/zzz"), atomFeed({}, {}));
    OpdsSource source(&transport);
    Collector const collected(&source);
    source.setRoot(QUrl(calibreRoot));

    source.load(QStringLiteral("zzz"));
    QTRY_VERIFY(!source.loading());
    QVERIFY(collected.books.isEmpty());
    QCOMPARE(collected.failures, QStringList{QStringLiteral("No results for \"zzz\".")});
    QCOMPARE(collected.offlineFlags, QList<bool>{false});
}

void tst_opdssource::selfLinkingFeedsDoNotLoop()
{
    FakeTransport transport;
    transport.route(calibreRoot, atomFeed("<link rel='next' type='application/atom+xml' href='/opds'/>", bookEntry("a", "Alpha")));
    OpdsSource source(&transport);
    Collector const collected(&source);
    source.setRoot(QUrl(calibreRoot));
    source.setBatchSize(5);

    source.load({});
    QTRY_VERIFY(!source.loading());
    QCOMPARE(transport.requests.size(), 1);
    QCOMPARE(collected.books.size(), 1);
    QVERIFY(!source.canLoadMore());
}

void tst_opdssource::batchSizeHasAFloor()
{
    FakeTransport transport;
    OpdsSource source(&transport);
    QCOMPARE(source.batchSize(), 20);
    source.setBatchSize(0);
    QCOMPARE(source.batchSize(), 1);
    source.setBatchSize(-5);
    QCOMPARE(source.batchSize(), 1);
    source.setBatchSize(60);
    QCOMPARE(source.batchSize(), 60);
}

void tst_opdssource::formatLabelsExtensionsAndFileNames()
{
    const auto link = [](const QString &type, const QString &href, const QString &title = {}) {
        Link l;
        l.rel = QStringLiteral("http://opds-spec.org/acquisition");
        l.type = type;
        l.href = href;
        l.title = title;
        return l;
    };

    const QList<BookFormat> formats = OpdsSource::formatsFromLinks(
        {
            link(QStringLiteral("application/epub+zip"), QStringLiteral("https://h/a.epub")),
            link(QStringLiteral("application/epub+zip"), QStringLiteral("https://h/b.epub")), // same label
            link(QStringLiteral("application/epub+zip"), QStringLiteral("https://h/a.epub")), // same url
            link(QStringLiteral("application/octet-stream"), QStringLiteral("https://h/get/file.AZW3")),
            link(QStringLiteral("application/octet-stream"), QStringLiteral("https://h/get/download")),
            link(QStringLiteral("application/pdf; charset=binary"), QStringLiteral("https://h/c"), QStringLiteral("  Print edition ")),
            link(QStringLiteral("application/pdf"), QStringLiteral("ftp://h/d.pdf")), // not downloadable here
        },
        QStringLiteral("../A/B: \"C\"?"));

    QCOMPARE(formats.size(), 5);
    QCOMPARE(formats.at(0).format, QStringLiteral("EPUB"));
    QCOMPARE(formats.at(1).format, QStringLiteral("EPUB (2)"));
    QCOMPARE(formats.at(2).format, QStringLiteral("AZW3"));
    QCOMPARE(formats.at(2).extension, QStringLiteral("azw3"));
    QCOMPARE(formats.at(3).extension, QStringLiteral("bin"));
    QCOMPARE(formats.at(4).format, QStringLiteral("Print edition"));
    QCOMPARE(formats.at(4).extension, QStringLiteral("pdf"));
    // Nothing that could leave the target folder or hide the file survives in the name.
    QCOMPARE(formats.at(0).fileName, QStringLiteral("_A_B_ _C__"));
    QVERIFY(!formats.at(0).fileName.contains(u'/'));
    QVERIFY(!formats.at(0).fileName.startsWith(u'.'));
}

QTEST_MAIN(tst_opdssource)

#include "tst_opdssource.moc"
