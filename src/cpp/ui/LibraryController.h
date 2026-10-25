#pragma once

#include "core/model/Book.h"
#include "core/model/SortOrder.h"
#include "ui/BookListModel.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <functional>

namespace KalibreBox {

class DropboxClient;
class LibraryRepository;

/**
 * UI controller for the library book list: loads the local metadata.db of the
 * current library, applies sort order / search, and lazily fetches covers
 * through the Dropbox client.
 */
class LibraryController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(BookListModel *books READ books CONSTANT)
    Q_PROPERTY(bool hasLibrary READ hasLibrary NOTIFY libraryChanged)
    Q_PROPERTY(QString libraryName READ libraryName NOTIFY libraryChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)
    Q_PROPERTY(int sortOrder READ sortOrder WRITE setSortOrder NOTIFY sortOrderChanged)

public:
    explicit LibraryController(DropboxClient *client, QObject *parent = nullptr);

    BookListModel *books() const { return m_model; }
    bool hasLibrary() const;
    QString libraryName() const;
    QString searchQuery() const;
    int sortOrder() const;

    void setSearchQuery(const QString &query);
    void setSortOrder(int order);

    Q_INVOKABLE void refresh();

signals:
    void libraryChanged();
    void searchQueryChanged();
    void sortOrderChanged();

private:
    QString coverPath(const QString &libraryId, qlonglong bookId) const;
    void requestCover(qlonglong bookId, const Book &book);

    /// Absolute Dropbox path of a book's cover.jpg.
    QString directCoverDropboxPath(const QString &libraryPath, const Book &book) const;
    /// Cover relative to the shared link root.
    QString sharedLinkCoverPath(const Book &book) const;

    DropboxClient *m_client = nullptr;
    LibraryRepository *m_repository = nullptr;
    BookListModel *m_model = nullptr;

    QString m_searchQuery;
    SortOrder m_sortOrder = SortOrder::RecentlyAdded;
    QSet<qlonglong> m_pendingCovers;
    std::optional<qlonglong> m_currentBookId;
};

}
