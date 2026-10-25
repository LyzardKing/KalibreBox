#include "core/dropbox/DropboxUrls.h"

#include <QTest>

using namespace KalibreBox::DropboxUrls;

class tst_dropboxurls : public QObject
{
    Q_OBJECT

private slots:
    void absolutePathsPassThrough();
    void relativePathsGetLeadingSlash();
    void homeLinksMapToPath();
    void sharedLinksAreMarkers();
    void oldSharedLinksAreMarkers();
    void nonDropboxUrlsFail();
    void isUrlDetection();
    void isValidPathDetection();
};

void tst_dropboxurls::absolutePathsPassThrough()
{
    QCOMPARE(extractPath(QStringLiteral("/Calibre Library")),
             QStringLiteral("/Calibre Library"));
    QCOMPARE(extractPath(QStringLiteral("/Books/My Library")),
             QStringLiteral("/Books/My Library"));
}

void tst_dropboxurls::relativePathsGetLeadingSlash()
{
    QCOMPARE(extractPath(QStringLiteral("Calibre Library")),
             QStringLiteral("/Calibre Library"));
    QCOMPARE(extractPath(QStringLiteral("  Books  ")),
             QStringLiteral("/Books"));
}

void tst_dropboxurls::homeLinksMapToPath()
{
    QCOMPARE(extractPath(
                 QStringLiteral("https://www.dropbox.com/home/Calibre%20Library")),
             QStringLiteral("/Calibre Library"));
    QCOMPARE(extractPath(
                 QStringLiteral("https://www.dropbox.com/home/Books/Calibre")),
             QStringLiteral("/Books/Calibre"));
}

void tst_dropboxurls::sharedLinksAreMarkers()
{
    const QString link =
        QStringLiteral("https://www.dropbox.com/scl/fo/xyzabc123?rlkey=ab12&dl=0");
    QCOMPARE(extractPath(link), link);
    QVERIFY(isSharedLink(link));

    // Multiple segments preserved
    const QString linkTwo = QStringLiteral(
        "https://www.dropbox.com/scl/fo/abcdef?rlkey=abc&st=%2F&raw=1");
    QCOMPARE(extractPath(linkTwo), linkTwo);
    QVERIFY(isSharedLink(linkTwo));
}

void tst_dropboxurls::oldSharedLinksAreMarkers()
{
    const QString link = QStringLiteral("https://www.dropbox.com/sh/xyz/ab?dl=0");
    QCOMPARE(extractPath(link), link);
    QVERIFY(isSharedLink(link));
}

void tst_dropboxurls::nonDropboxUrlsFail()
{
    QVERIFY(extractPath(QStringLiteral("https://example.com/scl/fo/xyz")).isEmpty());
    QVERIFY(!isSharedLink(QStringLiteral("https://example.com/scl/fo/xyz")));
}

void tst_dropboxurls::isUrlDetection()
{
    QVERIFY(isUrl(QStringLiteral("https://www.dropbox.com/home/x")));
    QVERIFY(isUrl(QStringLiteral("http://localhost:1/callback")));
    QVERIFY(!isUrl(QStringLiteral("/Calibre Library")));
    QVERIFY(!isUrl(QStringLiteral(""))); // NOLINT(readability-container-size-empty)
}

void tst_dropboxurls::isValidPathDetection()
{
    QVERIFY(isValidPath(QStringLiteral("/Calibre")));
    QVERIFY(!isValidPath(QStringLiteral("/")));
    QVERIFY(!isValidPath(QStringLiteral("Calibre")));
    QVERIFY(!isValidPath(QStringLiteral("")));
}

QTEST_MAIN(tst_dropboxurls)
#include "tst_dropboxurls.moc"
