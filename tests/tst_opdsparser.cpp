#include "core/opds/OpdsFeed.h"
#include "core/opds/OpdsParser.h"

#include <QFile>
#include <QTest>

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

const QUrl gutenbergBase(QStringLiteral("https://www.gutenberg.org/ebooks.opds/"));
const QUrl calibreBase(QStringLiteral("http://192.168.1.10:8080/opds"));
const QUrl opds2Base(QStringLiteral("https://catalog.example.org/opds/new"));

Feed mustParse(const char *name, const QUrl &base, const QString &contentType = {})
{
    QString error;
    const std::optional<Feed> feed = parseFeed(fixture(name), base, contentType, &error);
    if (!feed)
        qFatal("Could not parse %s: %s", name, qPrintable(error));
    return *feed;
}

}

class tst_opdsparser : public QObject
{
    Q_OBJECT

private slots:
    void detectsFormats_data();
    void detectsFormats();

    void gutenbergRootIsNavigation();
    void gutenbergRootAdvertisesSearchDescription();
    void gutenbergRootFindsLatestByTitle();
    void gutenbergLatestPageOne();
    void gutenbergLatestPageTwoPaginates();
    void gutenbergSearchResults();
    void gutenbergBookEntryOffersDownloads();
    void gutenbergOpenSearchDescription();

    void calibreFeed();
    void calibreSearchTemplateKeepsBraces();

    void opds2Metadata();
    void opds2Navigation();
    void opds2Publications();
    void opds2Groups();

    void rejectsHtmlAndGarbage();
    void rejectsMalformedXml();
    void rejectsJsonThatIsNotAFeed();
    void parsesStandaloneAtomEntry();

    void expandsOpenSearchTemplates_data();
    void expandsOpenSearchTemplates();
};

void tst_opdsparser::detectsFormats_data()
{
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<QString>("contentType");
    QTest::addColumn<int>("expected");

    const int atom = static_cast<int>(FeedFormat::Atom);
    const int json = static_cast<int>(FeedFormat::Json);
    const int unknown = static_cast<int>(FeedFormat::Unknown);
    QTest::newRow("atom header") << QByteArray("x") << QStringLiteral("application/atom+xml; charset=UTF-8") << atom;
    QTest::newRow("atom opds profile") << QByteArray("x") << QStringLiteral("application/atom+xml;profile=opds-catalog;kind=navigation") << atom;
    QTest::newRow("opds2 header") << QByteArray("x") << QStringLiteral("application/opds+json") << json;
    QTest::newRow("plain json header") << QByteArray("x") << QStringLiteral("application/json") << json;
    QTest::newRow("sniff xml") << QByteArray("  \n<?xml version='1.0'?><feed/>") << QString() << atom;
    QTest::newRow("sniff json") << QByteArray("\xEF\xBB\xBF {\"metadata\":{}}") << QStringLiteral("text/plain") << json;
    QTest::newRow("unknown") << QByteArray("hello") << QStringLiteral("text/plain") << unknown;
    QTest::newRow("empty") << QByteArray() << QString() << unknown;
}

void tst_opdsparser::detectsFormats()
{
    QFETCH(QByteArray, body);
    QFETCH(QString, contentType);
    QFETCH(int, expected);
    QCOMPARE(static_cast<int>(detectFormat(body, contentType)), expected);
}

void tst_opdsparser::gutenbergRootIsNavigation()
{
    const Feed feed = mustParse("gutenberg_root.xml", gutenbergBase, QStringLiteral("application/atom+xml"));
    QCOMPARE(feed.title, QStringLiteral("Project Gutenberg"));
    QCOMPARE(feed.itemsPerPage, 25);
    QCOMPARE(feed.entries.size(), 3);
    for (const Entry &entry : feed.entries) {
        QVERIFY(entry.isNavigation());
        QVERIFY(!entry.hasAcquisition());
    }
    QCOMPARE(feed.entries.at(0).title, QStringLiteral("Popular"));
    QCOMPARE(feed.entries.at(0).navigationLink()->href, QStringLiteral("https://www.gutenberg.org/ebooks/search.opds/?sort_order=downloads"));
    QCOMPARE(feed.startLink()->href, QStringLiteral("https://www.gutenberg.org/ebooks.opds/"));
}

void tst_opdsparser::gutenbergRootAdvertisesSearchDescription()
{
    const Feed feed = mustParse("gutenberg_root.xml", gutenbergBase);
    const SearchTarget target = feed.searchTarget();
    QCOMPARE(target.kind, SearchKind::Description);
    QCOMPARE(target.href, QStringLiteral("https://www.gutenberg.org/catalog/osd-books.xml"));
}

void tst_opdsparser::gutenbergRootFindsLatestByTitle()
{
    // Gutenberg does not use the sort/new relation, only a "Latest" entry.
    const Feed feed = mustParse("gutenberg_root.xml", gutenbergBase);
    const std::optional<Link> latest = feed.newestLink();
    QVERIFY(latest.has_value());
    QCOMPARE(latest->href, QStringLiteral("https://www.gutenberg.org/ebooks/search.opds/?sort_order=release_date"));
}

void tst_opdsparser::gutenbergLatestPageOne()
{
    const Feed feed = mustParse("gutenberg_latest.xml", gutenbergBase);
    QCOMPARE(feed.title, QStringLiteral("All Books"));
    QCOMPARE(feed.entries.size(), 28);
    QCOMPARE(feed.startIndex, 1);
    QCOMPARE(feed.itemsPerPage, 25);

    const std::optional<Link> next = feed.nextLink();
    QVERIFY(next.has_value());
    QCOMPARE(next->href, QStringLiteral("https://www.gutenberg.org/ebooks/search.opds/?sort_order=release_date&start_index=26"));

    // The social links are stubs on other hosts; books are stubs pointing at their own feed.
    QCOMPARE(feed.entries.at(0).navigationLink()->href, QStringLiteral("https://www.facebook.com/gutenberg.new"));
    const Entry &book = feed.entries.at(3);
    QVERIFY(book.isNavigation());
    QVERIFY(book.navigationLink()->href.startsWith(QLatin1String("https://www.gutenberg.org/ebooks/")));
    QVERIFY(book.navigationLink()->href.endsWith(QLatin1String(".opds")));
    // The stub's content is the author name.
    QVERIFY(!book.summary.isEmpty());
    // Inline thumbnails are kept untouched, not resolved against the feed URL.
    QVERIFY(book.thumbnailLink().has_value());
    QVERIFY(book.thumbnailLink()->href.startsWith(QLatin1String("data:image/png;base64,")));
}

void tst_opdsparser::gutenbergLatestPageTwoPaginates()
{
    const Feed feed = mustParse("gutenberg_latest_page2.xml", gutenbergBase);
    QCOMPARE(feed.startIndex, 26);
    QCOMPARE(feed.entries.size(), 25);
    QVERIFY(feed.findLink(QStringLiteral("previous")).has_value());
    QVERIFY(feed.findLink(QStringLiteral("first")).has_value());
    QCOMPARE(feed.nextLink()->href, QStringLiteral("https://www.gutenberg.org/ebooks/search.opds/?sort_order=release_date&start_index=51"));
}

void tst_opdsparser::gutenbergSearchResults()
{
    const Feed feed = mustParse("gutenberg_search.xml", gutenbergBase);
    QCOMPARE(feed.title, QStringLiteral("Books: dickens"));
    QCOMPARE(feed.entries.at(0).title, QStringLiteral("Authors"));
    QCOMPARE(feed.entries.at(1).title, QStringLiteral("Subjects"));
    QCOMPARE(feed.entries.at(2).title, QStringLiteral("A Tale of Two Cities"));
    QVERIFY(feed.nextLink().has_value());
}

void tst_opdsparser::gutenbergBookEntryOffersDownloads()
{
    const Feed feed = mustParse("gutenberg_book.xml", QUrl(QStringLiteral("https://www.gutenberg.org/ebooks/79876.opds")));
    QCOMPARE(feed.entries.size(), 2);

    const Entry &entry = feed.entries.first();
    QCOMPARE(entry.title, QStringLiteral("The single track"));
    QCOMPARE(entry.id, QStringLiteral("urn:gutenberg:79876:2"));
    QCOMPARE(entry.authors, QStringList{QStringLiteral("Ostrander, Isabel")});
    QCOMPARE(entry.language, QStringLiteral("en"));
    QVERIFY(entry.categories.contains(QStringLiteral("Railroads -- Fiction")));

    QVERIFY(entry.hasAcquisition());
    QVERIFY(!entry.isNavigation());
    const QList<Link> downloads = entry.downloadLinks();
    QCOMPARE(downloads.size(), 1);
    QCOMPARE(downloads.first().type, QStringLiteral("application/epub+zip"));
    QCOMPARE(downloads.first().href, QStringLiteral("https://www.gutenberg.org/ebooks/79876.epub.noimages"));
    QCOMPARE(downloads.first().length, qint64(433436));

    QCOMPARE(entry.coverLink()->href, QStringLiteral("https://www.gutenberg.org/cache/epub/79876/pg79876.cover.medium.jpg"));
    QCOMPARE(entry.thumbnailLink()->href, QStringLiteral("https://www.gutenberg.org/cache/epub/79876/pg79876.cover.small.jpg"));

    // The xhtml content is reduced to plain text, one paragraph per line.
    QVERIFY(entry.summary.contains(QStringLiteral("Author: Ostrander, Isabel, 1883-1924")));
    QVERIFY(!entry.summary.contains(QLatin1Char('<')));
    QVERIFY(entry.summary.contains(QLatin1Char('\n')));

    // The second entry is the edition with images.
    QCOMPARE(feed.entries.at(1).id, QStringLiteral("urn:gutenberg:79876:3"));
}

void tst_opdsparser::gutenbergOpenSearchDescription()
{
    QString error;
    const std::optional<QString> urlTemplate = parseOpenSearchDescription(fixture("gutenberg_osd.xml"), &error);
    QVERIFY2(urlTemplate.has_value(), qPrintable(error));
    // The HTML and suggestion URLs are ignored.
    QCOMPARE(*urlTemplate, QStringLiteral("http://m.gutenberg.org/ebooks/search.opds/?query={searchTerms}"));
    QCOMPARE(expandSearchTemplate(*urlTemplate, QStringLiteral("charles dickens")).toString(QUrl::FullyEncoded),
             QStringLiteral("http://m.gutenberg.org/ebooks/search.opds/?query=charles%20dickens"));
}

void tst_opdsparser::calibreFeed()
{
    const Feed feed = mustParse("calibre_newest.xml", calibreBase, QStringLiteral("application/atom+xml"));
    QCOMPARE(feed.title, QStringLiteral("Newest"));
    QCOMPARE(feed.entries.size(), 3);
    QCOMPARE(feed.nextLink()->href, QStringLiteral("http://192.168.1.10:8080/opds/navcatalog/4F6E6577657374?offset=2&library_id=My_Library"));

    const Entry &dune = feed.entries.at(0);
    QCOMPARE(dune.title, QStringLiteral("Dune"));
    QCOMPARE(dune.authors, QStringList{QStringLiteral("Frank Herbert")});
    QCOMPARE(dune.downloadLinks().size(), 2);
    QCOMPARE(dune.downloadLinks().at(0).href, QStringLiteral("http://192.168.1.10:8080/get/EPUB/12/My_Library"));
    QCOMPARE(dune.downloadLinks().at(1).type, QStringLiteral("application/x-mobipocket-ebook"));
    // The draft "cover"/"thumbnail" relations still count.
    QCOMPARE(dune.coverLink()->href, QStringLiteral("http://192.168.1.10:8080/get/cover/12/My_Library"));
    QCOMPARE(dune.thumbnailLink()->href, QStringLiteral("http://192.168.1.10:8080/get/thumb/12/My_Library?sz=60x80"));
    QVERIFY(dune.summary.contains(QStringLiteral("A desert planet & its spice.")));
    QVERIFY(dune.summary.contains(QStringLiteral("TAGS: Science Fiction")));

    const Entry &unicode = feed.entries.at(1);
    QCOMPARE(unicode.title, QStringLiteral("Ünïcode & Co."));
    QCOMPARE(unicode.authors, (QStringList{QStringLiteral("Jane Doe"), QStringLiteral("John Roe")}));
    // Without a thumbnail relation the cover is used.
    QCOMPARE(unicode.thumbnailLink()->href, unicode.coverLink()->href);

    // A navigation entry whose link has no rel at all.
    const Entry &byAuthor = feed.entries.at(2);
    QVERIFY(byAuthor.isNavigation());
    QCOMPARE(byAuthor.navigationLink()->href, QStringLiteral("http://192.168.1.10:8080/opds/navcatalog/4F617574686F7273?library_id=My_Library"));
    // No sort/new relation and no "Newest"-style title in this feed.
    QVERIFY(!feed.newestLink().has_value());
}

void tst_opdsparser::calibreSearchTemplateKeepsBraces()
{
    const Feed feed = mustParse("calibre_newest.xml", calibreBase);
    const SearchTarget target = feed.searchTarget();
    QCOMPARE(target.kind, SearchKind::Template);
    QCOMPARE(target.href, QStringLiteral("http://192.168.1.10:8080/opds/search/{searchTerms}?library_id=My_Library"));
    QCOMPARE(expandSearchTemplate(target.href, QStringLiteral("frank herbert")).toString(QUrl::FullyEncoded),
             QStringLiteral("http://192.168.1.10:8080/opds/search/frank%20herbert?library_id=My_Library"));
}

void tst_opdsparser::opds2Metadata()
{
    const Feed feed = mustParse("opds2_catalog.json", opds2Base, QStringLiteral("application/opds+json"));
    QCOMPARE(feed.title, QStringLiteral("Example Catalog"));
    QCOMPARE(feed.totalResults, 120);
    QCOMPARE(feed.itemsPerPage, 2);
    // Page 2 of 2-per-page starts at the third item.
    QCOMPARE(feed.startIndex, 3);
    QCOMPARE(feed.nextLink()->href, QStringLiteral("https://catalog.example.org/opds/new?page=3"));
    QCOMPARE(feed.selfLink()->href, QStringLiteral("https://catalog.example.org/opds/new?page=2"));

    const SearchTarget search = feed.searchTarget();
    QCOMPARE(search.kind, SearchKind::Template);
    QCOMPARE(search.href, QStringLiteral("https://catalog.example.org/opds/search{?query}"));
    QCOMPARE(expandSearchTemplate(search.href, QStringLiteral("moby dick")).toString(QUrl::FullyEncoded),
             QStringLiteral("https://catalog.example.org/opds/search?query=moby%20dick"));
}

void tst_opdsparser::opds2Navigation()
{
    const Feed feed = mustParse("opds2_catalog.json", opds2Base);
    QVERIFY(feed.entries.at(0).isNavigation());
    QCOMPARE(feed.entries.at(0).title, QStringLiteral("New"));
    QCOMPARE(feed.newestLink()->href, QStringLiteral("https://catalog.example.org/opds/new"));
    // A navigation item without type or rel still leads somewhere; titles may be language maps.
    QCOMPARE(feed.entries.at(1).title, QStringLiteral("Popular"));
    QVERIFY(feed.entries.at(1).isNavigation());
    QCOMPARE(feed.entries.at(1).navigationLink()->href, QStringLiteral("https://catalog.example.org/opds/popular"));
}

void tst_opdsparser::opds2Publications()
{
    const Feed feed = mustParse("opds2_catalog.json", opds2Base);
    QCOMPARE(feed.entries.size(), 4);

    const Entry &moby = feed.entries.at(2);
    QCOMPARE(moby.title, QStringLiteral("Moby Dick"));
    QCOMPARE(moby.id, QStringLiteral("urn:isbn:9781503280786"));
    QCOMPARE(moby.authors, QStringList{QStringLiteral("Herman Melville")});
    QCOMPARE(moby.language, QStringLiteral("en"));
    QCOMPARE(moby.summary, QStringLiteral("A whale of a tale."));
    QCOMPARE(moby.series, QStringLiteral("Classics"));
    QCOMPARE(moby.categories, (QStringList{QStringLiteral("Whaling"), QStringLiteral("Sea stories")}));
    QVERIFY(!moby.isNavigation());

    // Buying is not downloading.
    QVERIFY(moby.hasAcquisition());
    QCOMPARE(moby.downloadLinks().size(), 1);
    QCOMPARE(moby.downloadLinks().first().href, QStringLiteral("https://catalog.example.org/dl/moby.epub"));
    // The widest image is the cover, the narrowest the thumbnail.
    QCOMPARE(moby.coverLink()->href, QStringLiteral("https://catalog.example.org/img/moby_600.jpg"));
    QCOMPARE(moby.thumbnailLink()->href, QStringLiteral("https://catalog.example.org/img/moby_100.jpg"));
    // A relation list yields one link per relation.
    int selfLinks = 0;
    int alternateLinks = 0;
    for (const Link &link : moby.links) {
        selfLinks += link.rel == QLatin1String("self");
        alternateLinks += link.rel == QLatin1String("alternate");
    }
    QCOMPARE(selfLinks, 1);
    QCOMPARE(alternateLinks, 1);

    const Entry &second = feed.entries.at(3);
    QCOMPARE(second.title, QStringLiteral("Localised Title"));
    QCOMPARE(second.authors, QStringList{QStringLiteral("Plain String Author")});
    QCOMPARE(second.downloadLinks().first().href, QStringLiteral("https://cdn.example.org/2.pdf"));
    QCOMPARE(second.coverLink()->href, QStringLiteral("https://catalog.example.org/img/2.jpg"));
}

void tst_opdsparser::opds2Groups()
{
    const Feed feed = mustParse("opds2_catalog.json", opds2Base);
    QCOMPARE(feed.groups.size(), 1);
    QCOMPARE(feed.groups.first().title, QStringLiteral("Popular"));
    QCOMPARE(feed.groups.first().moreLink->href, QStringLiteral("https://catalog.example.org/opds/popular"));
    QCOMPARE(feed.groups.first().entries.size(), 1);
    QCOMPARE(feed.groups.first().entries.first().title, QStringLiteral("Grouped Book"));
}

void tst_opdsparser::rejectsHtmlAndGarbage()
{
    QString error;
    QVERIFY(!parseFeed("<html><body>Login</body></html>", QUrl(), QStringLiteral("text/html"), &error).has_value());
    QVERIFY2(error.contains(QStringLiteral("html")), qPrintable(error));

    error.clear();
    QVERIFY(!parseFeed("just some text", QUrl(), QStringLiteral("text/plain"), &error).has_value());
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(!parseFeed({}, QUrl(), {}, &error).has_value());
    QVERIFY(!error.isEmpty());
}

void tst_opdsparser::rejectsMalformedXml()
{
    QString error;
    QVERIFY(!parseAtomFeed("<feed xmlns='http://www.w3.org/2005/Atom'><entry><title>x</entry></feed>", QUrl(), &error).has_value());
    QVERIFY2(error.contains(QStringLiteral("line")), qPrintable(error));

    // The parser keeps working with a null error pointer.
    QVERIFY(!parseAtomFeed("<feed", QUrl(), nullptr).has_value());
}

void tst_opdsparser::rejectsJsonThatIsNotAFeed()
{
    QString error;
    QVERIFY(!parseJsonFeed("[1, 2]", QUrl(), &error).has_value());
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(!parseJsonFeed("{\"unrelated\": true}", QUrl(), &error).has_value());
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(!parseJsonFeed("{not json", QUrl(), &error).has_value());
    QVERIFY(error.startsWith(QStringLiteral("Invalid JSON")));
}

void tst_opdsparser::parsesStandaloneAtomEntry()
{
    const QByteArray body =
        "<entry xmlns='http://www.w3.org/2005/Atom'><id>urn:x</id><title>Solo</title>"
        "<link rel='http://opds-spec.org/acquisition/open-access' href='solo.epub' type='application/epub+zip'/></entry>";
    const std::optional<Feed> feed = parseAtomFeed(body, QUrl(QStringLiteral("https://example.org/book/1/")));
    QVERIFY(feed.has_value());
    QCOMPARE(feed->entries.size(), 1);
    QCOMPARE(feed->entries.first().downloadLinks().first().href, QStringLiteral("https://example.org/book/1/solo.epub"));
}

void tst_opdsparser::expandsOpenSearchTemplates_data()
{
    QTest::addColumn<QString>("urlTemplate");
    QTest::addColumn<QString>("terms");
    QTest::addColumn<QString>("expected");

    QTest::newRow("path variable") << "https://h/opds/search/{searchTerms}" << "a b" << "https://h/opds/search/a%20b";
    QTest::newRow("optional parameters dropped") << "https://h/s?q={searchTerms}&page={startPage?}" << "x" << "https://h/s?q=x&page=";
    QTest::newRow("namespaced variable") << "https://h/s?q={os:searchTerms}" << "x" << "https://h/s?q=x";
    QTest::newRow("form query") << "https://h/s{?query}" << "x y" << "https://h/s?query=x%20y";
    QTest::newRow("form query known name wins over position") << "https://h/s{?page,query}" << "x" << "https://h/s?query=x";
    QTest::newRow("form query unknown name falls back to first") << "https://h/s{?title}" << "x" << "https://h/s?title=x";
    QTest::newRow("form continuation") << "https://h/s?lang=en{&query}" << "x" << "https://h/s?lang=en&query=x";
    QTest::newRow("form query after query") << "https://h/s?lang=en{?query}" << "x" << "https://h/s?lang=en&query=x";
    QTest::newRow("reserved characters") << "https://h/s?q={searchTerms}" << "a&b=c/d#e" << "https://h/s?q=a%26b%3Dc%2Fd%23e";
    QTest::newRow("unicode") << "https://h/s?q={searchTerms}" << "Ünï" << "https://h/s?q=%C3%9Cn%C3%AF";
    QTest::newRow("no variables") << "https://h/all" << "x" << "https://h/all";
    QTest::newRow("unterminated brace") << "https://h/s?q={searchTerms" << "x" << "https://h/s?q=%7BsearchTerms";
}

void tst_opdsparser::expandsOpenSearchTemplates()
{
    QFETCH(QString, urlTemplate);
    QFETCH(QString, terms);
    QFETCH(QString, expected);
    QCOMPARE(expandSearchTemplate(urlTemplate, terms).toString(QUrl::FullyEncoded), expected);
}

QTEST_MAIN(tst_opdsparser)

#include "tst_opdsparser.moc"
