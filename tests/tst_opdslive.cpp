#include "core/opds/OpdsClient.h"
#include "core/opds/OpdsSource.h"

#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace KalibreBox;
using namespace KalibreBox::Opds;

/**
 * Exercises the real client and source against Project Gutenberg's public OPDS
 * catalogue. Needs the internet, so it only runs when KALIBREBOX_LIVE_TESTS=1.
 */
class tst_opdslive : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if (qEnvironmentVariable("KALIBREBOX_LIVE_TESTS") != QLatin1String("1"))
            QSKIP("Set KALIBREBOX_LIVE_TESTS=1 to run tests against the live Gutenberg catalogue.");
    }

    void latestSearchDetailsAndDownload();
};

void tst_opdslive::latestSearchDetailsAndDownload()
{
    OpdsClient client;
    OpdsSource source(&client);
    source.setRoot(QUrl(QStringLiteral("https://www.gutenberg.org/ebooks.opds/")));
    source.setBatchSize(20);

    QList<Book> books;
    QStringList failures;
    connect(&source, &OpdsSource::reset, this, [&books]() {
        books.clear();
    });
    connect(&source, &OpdsSource::booksAdded, this, [&books](const QList<Book> &added) {
        books += added;
    });
    connect(&source, &OpdsSource::failed, this, [&failures](const QString &message, bool) {
        failures.append(message);
    });

    // Latest books.
    source.load({});
    QTRY_VERIFY_WITH_TIMEOUT(!source.loading(), 30000);
    QVERIFY2(failures.isEmpty(), qPrintable(failures.join(u'\n')));
    QVERIFY2(books.size() >= 20, qPrintable(QString::number(books.size())));
    for (const Book &book : std::as_const(books)) {
        QVERIFY(!book.title.isEmpty());
        QVERIFY(!book.detailsUrl.isEmpty());
        QVERIFY(book.hasCover);
    }

    // Scrolling on loads further books without repeating any.
    const qsizetype firstBatch = books.size();
    QVERIFY(source.canLoadMore());
    source.fetchMore();
    QTRY_VERIFY_WITH_TIMEOUT(!source.loading(), 30000);
    QVERIFY(books.size() > firstBatch);
    QSet<QString> urls;
    for (const Book &book : std::as_const(books))
        urls.insert(book.detailsUrl);
    QCOMPARE(urls.size(), books.size());

    // Server-side search, through the OpenSearch description and its redirect.
    source.load(QStringLiteral("pride and prejudice"));
    QTRY_VERIFY_WITH_TIMEOUT(!source.loading(), 30000);
    QVERIFY2(failures.isEmpty(), qPrintable(failures.join(u'\n')));
    QVERIFY(!source.searchIsLocalOnly());
    QVERIFY(!books.isEmpty());
    bool found = false;
    for (const Book &book : std::as_const(books))
        found = found || book.title.contains(QStringLiteral("Pride and prejudice"), Qt::CaseInsensitive);
    QVERIFY2(found, "the search results do not mention the book");

    // A stub learns its formats when opened.
    QSignalSpy ready(&source, &OpdsSource::detailsReady);
    source.loadDetails(books.first());
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 30000);
    const Book detailed = ready.first().first().value<Book>();
    QVERIFY(!detailed.formats.isEmpty());

    // The first format really downloads.
    QTemporaryDir dir;
    const BookFormat &format = detailed.formats.first();
    const QString target = dir.filePath(QStringLiteral("book.") + format.extension);
    std::optional<FileResult> download;
    client.downloadFile(QUrl(format.url), target, [&download](const FileResult &r) {
        download = r;
    });
    QTRY_VERIFY_WITH_TIMEOUT(download.has_value(), 60000);
    QVERIFY2(download->ok, qPrintable(download->error));
    QVERIFY(QFileInfo(target).size() > 1000);

    // And so does a cover.
    const QString coverTarget = dir.filePath(QStringLiteral("cover.jpg"));
    std::optional<FileResult> cover;
    client.downloadFile(QUrl(books.first().coverUrl), coverTarget, [&cover](const FileResult &r) {
        cover = r;
    });
    QTRY_VERIFY_WITH_TIMEOUT(cover.has_value(), 30000);
    QVERIFY2(cover->ok, qPrintable(cover->error));
}

QTEST_MAIN(tst_opdslive)

#include "tst_opdslive.moc"
