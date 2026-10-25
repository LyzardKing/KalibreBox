#include "core/db/DatabaseHelper.h"

#include "core/text/TextNormalization.h"

#include <QChar>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <algorithm>

namespace KalibreBox {

namespace {

QString connectionNameFor(const QString &libraryId)
{
    return QStringLiteral("kalibrebox_%1").arg(libraryId);
}

QSqlDatabase connectionFor(const QString &libraryId)
{
    if (QSqlDatabase::contains(connectionNameFor(libraryId)))
        return QSqlDatabase::database(connectionNameFor(libraryId), false);

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionNameFor(libraryId));
    db.setDatabaseName(DatabaseHelper::databaseFilePath(libraryId));
    return db;
}

const char *const getAllBookDetailsQuery = R"(
SELECT
    b.id,
    b.title,
    b.path,
    b.has_cover,
    c.text AS comment,
    GROUP_CONCAT(a.name, ', ') AS authors,
    (SELECT GROUP_CONCAT(d.format || ':' || d.name, '|||SEP|||') FROM data d WHERE d.book = b.id) AS formatsAndFiles
FROM books AS b
LEFT JOIN books_authors_link AS bal ON b.id = bal.book
LEFT JOIN authors AS a ON bal.author = a.id
LEFT JOIN comments AS c ON b.id = c.book
GROUP BY b.id
ORDER BY b.title
)";

}

QString DatabaseHelper::databaseFilePath(const QString &libraryId)
{
    const QDir dataDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    return dataDir.filePath(QStringLiteral("metadata_%1.db").arg(libraryId));
}

bool DatabaseHelper::openLibrary(const QString &libraryId)
{
    // SQLite refuses to create a file in a non-existent directory.
    QDir dataDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    dataDir.mkpath(dataDir.absolutePath());

    QSqlDatabase db = connectionFor(libraryId);
    if (!db.isOpen())
        db.open();
    return db.isOpen();
}

bool DatabaseHelper::isOpen(const QString &libraryId)
{
    return QSqlDatabase::contains(connectionNameFor(libraryId))
        && QSqlDatabase::database(connectionNameFor(libraryId), false).isOpen();
}

void DatabaseHelper::closeLibrary(const QString &libraryId)
{
    if (!QSqlDatabase::contains(connectionNameFor(libraryId)))
        return;
    {
        QSqlDatabase db = QSqlDatabase::database(connectionNameFor(libraryId), false);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(connectionNameFor(libraryId));
}

bool DatabaseHelper::clearDatabase(const QString &libraryId)
{
    if (libraryId.isEmpty()) {
        const QDir dataDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
        const QFileInfoList metadataFiles =
            dataDir.entryInfoList({QStringLiteral("metadata_*.db")}, QDir::Files);
        for (const QFileInfo &file : metadataFiles) {
            const QString id = file.completeBaseName().sliced(QStringLiteral("metadata_").size());
            closeLibrary(id);
            if (!QFile::remove(file.absoluteFilePath()))
                return false;
        }
        return true;
    }

    closeLibrary(libraryId);
    const QString filePath = databaseFilePath(libraryId);
    if (QFileInfo::exists(filePath))
        return QFile::remove(filePath);
    return true;
}

QList<Book> DatabaseHelper::getBooks(const QString &libraryId)
{
    QList<Book> books;
    if (!openLibrary(libraryId))
        return books;

    QSqlQuery query(connectionFor(libraryId));
    if (!query.exec(QString::fromLatin1(getAllBookDetailsQuery)))
        return books;

    while (query.next()) {
        Book book;
        book.id = query.value(0).toLongLong();
        book.title = query.value(1).toString();
        book.path = query.value(2).toString();
        book.hasCover = query.value(3).toInt() != 0;
        book.comment = query.value(4).toString();
        book.authors = query.value(5).toString();
        book.formats = parseFormats(query.value(6).toString());
        books.append(book);
    }
    return books;
}

void DatabaseHelper::sortBooks(QList<Book> &books, SortOrder order)
{
    switch (order) {
    case SortOrder::Title:
        std::stable_sort(books.begin(), books.end(), [](const Book &a, const Book &b) {
            return TextNormalization::normalizeForSearch(a.title)
                < TextNormalization::normalizeForSearch(b.title);
        });
        break;
    case SortOrder::Author:
        // Author-less books sort last, matching the Kotlin compareBy(nullsLast()).
        std::stable_sort(books.begin(), books.end(), [](const Book &a, const Book &b) {
            if (a.authors.isEmpty() != b.authors.isEmpty())
                return b.authors.isEmpty();
            if (a.authors.isEmpty())
                return false;
            return TextNormalization::normalizeForSearch(a.authors)
                < TextNormalization::normalizeForSearch(b.authors);
        });
        break;
    case SortOrder::RecentlyAdded:
        std::stable_sort(books.begin(), books.end(),
                         [](const Book &a, const Book &b) { return a.id > b.id; });
        break;
    }
}

QList<Book> DatabaseHelper::filterBooks(const QList<Book> &books, const QString &query)
{
    const QString normalizedQuery = TextNormalization::normalizeForSearch(query.trimmed());
    if (normalizedQuery.isEmpty())
        return books;

    QList<Book> filtered;
    filtered.reserve(books.size());
    for (const Book &book : books) {
        if (TextNormalization::normalizeForSearch(book.title).contains(normalizedQuery)
            || TextNormalization::normalizeForSearch(book.authors).contains(normalizedQuery))
            filtered.append(book);
    }
    return filtered;
}

QList<BookFormat> DatabaseHelper::parseFormats(const QString &formatsAndFiles)
{
    QList<BookFormat> formats;
    static const QString groupSeparator = QStringLiteral("|||SEP|||");

    if (formatsAndFiles.isEmpty())
        return formats;

    for (const QString &entry : formatsAndFiles.split(groupSeparator, Qt::SkipEmptyParts)) {
        const int colon = entry.indexOf(u':');
        if (colon < 0)
            continue;
        formats.append({entry.left(colon), entry.mid(colon + 1)});
    }
    return formats;
}

QStringList DatabaseHelper::calibreSchemaStatements()
{
    // Only what the app reads; Calibre owns the real database.
    return {
        QStringLiteral(
            "CREATE TABLE authors ("
            "  id   INTEGER PRIMARY KEY,"
            "  name TEXT NOT NULL COLLATE NOCASE,"
            "  sort TEXT COLLATE NOCASE,"
            "  link TEXT NOT NULL DEFAULT \"\","
            "  UNIQUE(name))"),
        QStringLiteral(
            "CREATE TABLE books ("
            "  id      INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  title   TEXT NOT NULL DEFAULT 'Unknown' COLLATE NOCASE,"
            "  sort    TEXT COLLATE NOCASE,"
            "  timestamp TEXT,"
            "  pubdate TEXT,"
            "  series_index REAL NOT NULL DEFAULT 1.0,"
            "  author_sort TEXT COLLATE NOCASE,"
            "  isbn TEXT DEFAULT \"\" COLLATE NOCASE,"
            "  lccn TEXT DEFAULT \"\" COLLATE NOCASE,"
            "  path TEXT NOT NULL DEFAULT \"\","
            "  flags INTEGER NOT NULL DEFAULT 1,"
            "  uuid TEXT,"
            "  has_cover INTEGER DEFAULT 0,"
            "  last_modified TEXT NOT NULL DEFAULT \"2000-01-01 00:00:00+00:00\")"),
        QStringLiteral(
            "CREATE TABLE books_authors_link ("
            "  id     INTEGER PRIMARY KEY,"
            "  book   INTEGER NOT NULL,"
            "  author INTEGER NOT NULL,"
            "  UNIQUE(book, author))"),
        QStringLiteral(
            "CREATE TABLE data ("
            "  id     INTEGER PRIMARY KEY,"
            "  book   INTEGER NOT NULL,"
            "  format TEXT NOT NULL COLLATE NOCASE,"
            "  uncompressed_size INTEGER NOT NULL,"
            "  name   TEXT NOT NULL,"
            "  UNIQUE(book, format))"),
        QStringLiteral(
            "CREATE TABLE comments ("
            "  id   INTEGER PRIMARY KEY,"
            "  book INTEGER NOT NULL,"
            "  text TEXT NOT NULL COLLATE NOCASE,"
            "  UNIQUE(book))"),
    };
}

}
