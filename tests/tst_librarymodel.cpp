#include "core/model/Book.h"
#include "core/model/Library.h"
#include "core/model/SortOrder.h"

#include <QTest>

using namespace KalibreBox;

class tst_librarymodel : public QObject
{
    Q_OBJECT

private slots:
    void libraryDefaults();
    void bookDefaultsAndEquality();
};

void tst_librarymodel::libraryDefaults()
{
    Library const library;
    QVERIFY(library.id.isEmpty());
    QVERIFY(!library.isDefault);
    QVERIFY(!library.sharedLinkUrl.has_value());

    Library withLink = library;
    withLink.sharedLinkUrl = QStringLiteral("https://www.dropbox.com/s/xyz");
    QVERIFY(withLink != library);
}

void tst_librarymodel::bookDefaultsAndEquality()
{
    Book const book;
    QCOMPARE(book.id, qlonglong(0));
    QCOMPARE(book.hasCover, false);
    QVERIFY(book.formats.isEmpty());

    Book same = book;
    QCOMPARE(book == same, true);

    same.id = 42;
    QVERIFY(book != same);
}

QTEST_MAIN(tst_librarymodel)
#include "tst_librarymodel.moc"
