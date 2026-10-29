#include "core/db/DatabaseHelper.h"
#include "core/model/Book.h"
#include "core/model/SortOrder.h"

#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>

using namespace KalibreBox;

class tst_databasehelper : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void returnsBooksOrderedByTitle();
    void joinsAuthorsAndComments();
    void sortOrders();
    void filterBooksCaseAccentInsensitive();
    void clearDatabaseRemovesFile();

private:
    QString libraryId;
};

void tst_databasehelper::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    libraryId = QStringLiteral("testlibrary");

    QVERIFY(DatabaseHelper::clearDatabase(libraryId));
    QVERIFY(DatabaseHelper::openLibrary(libraryId));
    QVERIFY(DatabaseHelper::isOpen(libraryId));

    QSqlQuery query(QSqlDatabase::database(QStringLiteral("kalibrebox_%1").arg(libraryId), false));
    for (const QString &statement : DatabaseHelper::calibreSchemaStatements())
        QVERIFY(query.exec(statement));

    QVERIFY(query.exec("INSERT INTO authors (id, name) VALUES (1, 'J. R. R. Tolkien'), (2, 'José Saramago')"));
    QVERIFY(
        query.exec("INSERT INTO books (id, title, path, has_cover) VALUES "
                   "(1, 'The Hobbit', 'J. R. R. Tolkien/The Hobbit', 1), "
                   "(2, 'O Ano da Morte de Ricardo Reis', 'Saramago/Ano', 0)"));
    QVERIFY(query.exec("INSERT INTO books_authors_link VALUES (1, 1, 1), (2, 2, 2)"));
    QVERIFY(
        query.exec("INSERT INTO data (book, format, uncompressed_size, name) VALUES "
                   "(1, 'EPUB', 123, 'The-Hobbit'), (1, 'MOBI', 99, 'The-Hobbit')"));
    QVERIFY(query.exec("INSERT INTO comments (book, text) VALUES (1, 'There and back again')"));
}

void tst_databasehelper::cleanupTestCase()
{
    DatabaseHelper::clearDatabase();
}

void tst_databasehelper::returnsBooksOrderedByTitle()
{
    const QList<Book> books = DatabaseHelper::getBooks(libraryId);
    QCOMPARE(books.size(), 2);
    QCOMPARE(books.at(0).title, QStringLiteral("O Ano da Morte de Ricardo Reis"));
    QCOMPARE(books.at(1).title, QStringLiteral("The Hobbit"));
}

void tst_databasehelper::joinsAuthorsAndComments()
{
    const QList<Book> books = DatabaseHelper::getBooks(libraryId);
    const Book &hobbit = books.at(1);
    QCOMPARE(hobbit.authors, QStringLiteral("J. R. R. Tolkien"));
    QCOMPARE(hobbit.comment, QStringLiteral("There and back again"));
    QCOMPARE(hobbit.hasCover, true);
    QCOMPARE(hobbit.formats.size(), 2);
    QCOMPARE(hobbit.formats.at(0).format, QStringLiteral("EPUB"));
    QCOMPARE(hobbit.formats.at(0).fileName, QStringLiteral("The-Hobbit"));

    const Book &saramago = books.at(0);
    QCOMPARE(saramago.authors, QStringLiteral("José Saramago"));
    QCOMPARE(saramago.comment, QString());
    QCOMPARE(saramago.hasCover, false);
    QCOMPARE(saramago.formats.size(), 0);
}

void tst_databasehelper::sortOrders()
{
    const QList<Book> books = DatabaseHelper::getBooks(libraryId);
    // DB order is by title: Saramago first, Hobbit second.

    QList<Book> byRecently = books;
    DatabaseHelper::sortBooks(byRecently, SortOrder::RecentlyAdded);
    QCOMPARE(byRecently.at(0).id, 2);
    QCOMPARE(byRecently.at(1).id, 1);

    QList<Book> byAuthor = books;
    DatabaseHelper::sortBooks(byAuthor, SortOrder::Author);
    QCOMPARE(byAuthor.at(0).authors, QStringLiteral("J. R. R. Tolkien"));

    QList<Book> withOrphan = books;
    Book orphan;
    orphan.id = 3;
    orphan.title = QStringLiteral("Rarified");
    withOrphan.prepend(orphan); // no authors
    DatabaseHelper::sortBooks(withOrphan, SortOrder::Author);
    QCOMPARE(withOrphan.last().id, 3);
}

void tst_databasehelper::filterBooksCaseAccentInsensitive()
{
    const QList<Book> books = DatabaseHelper::getBooks(libraryId);

    QList<Book> result = DatabaseHelper::filterBooks(books, QStringLiteral("hobbit"));
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.at(0).id, 1);

    result = DatabaseHelper::filterBooks(books, QStringLiteral("SARAMA"));
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.at(0).id, 2);

    // Accents are stripped before matching
    result = DatabaseHelper::filterBooks(books, QStringLiteral("jose"));
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.at(0).id, 2);

    result = DatabaseHelper::filterBooks(books, QStringLiteral(""));
    QCOMPARE(result.size(), books.size());
}

void tst_databasehelper::clearDatabaseRemovesFile()
{
    QVERIFY(DatabaseHelper::clearDatabase(libraryId));
    QVERIFY(!QFile(DatabaseHelper::databaseFilePath(libraryId)).exists());
    QVERIFY(DatabaseHelper::getBooks(libraryId).isEmpty());
}

QTEST_MAIN(tst_databasehelper)
#include "tst_databasehelper.moc"
