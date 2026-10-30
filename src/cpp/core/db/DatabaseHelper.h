#pragma once

#include "core/model/Book.h"
#include "core/model/SortOrder.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

class QSqlDatabase;

namespace KalibreBox
{

/**
 * Local side of the Calibre metadata database.
 *
 * Each Calibre library has its own copy of Calibre's `metadata.db`
 * (downloaded from Dropbox; the download/sync part lives in the network
 * layer). This class opens and queries it.
 */
class DatabaseHelper
{
public:
    static QString databaseFilePath(const QString &libraryId);

    /// Opens (or reuses) the SQLite connection for a library.
    static bool openLibrary(const QString &libraryId);
    static bool isOpen(const QString &libraryId);
    static void closeLibrary(const QString &libraryId);

    /// Deletes the local database file(s). Empty libraryId clears all libraries.
    static bool clearDatabase(const QString &libraryId = {});

    /**
     * Atomically replaces the stored metadata copy of a library with `partPath`
     * (a freshly downloaded file) and reopens the connection. Used by the sync
     * flow so a failed download never destroys the previous offline copy.
     */
    static bool replaceDatabase(const QString &libraryId, const QString &partPath);

    static QList<Book> getBooks(const QString &libraryId);
    /// Names of all authors that have at least one book, in Calibre's sort order.
    static QStringList getAuthors(const QString &libraryId);

    static void sortBooks(QList<Book> &books, SortOrder order);
    static QList<Book> filterBooks(const QList<Book> &books, const QString &query);

    static QList<BookFormat> parseFormats(const QString &formatsAndFiles);

    /// Library-owned stub schema for testing/creating a fresh (empty) copy.
    static QStringList calibreSchemaStatements();
};

}
