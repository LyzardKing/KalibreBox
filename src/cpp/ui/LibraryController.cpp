#include "ui/LibraryController.h"

#include "core/data/LibraryRepository.h"
#include "core/db/DatabaseHelper.h"
#include "core/dropbox/DropboxClient.h"

#include <QDir>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

Q_LOGGING_CATEGORY(kalibreboxCore, "kalibrebox.core")


namespace KalibreBox {

LibraryController::LibraryController(DropboxClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_repository(new LibraryRepository())
    , m_model(new BookListModel(this))
{
    // Covers are requested lazily by the model's CoverRole; the download
    // outcome lands back in the model through setCover().
    m_model->setCoverRequestHandler([this](qlonglong bookId) {
        const QList<Book> all = DatabaseHelper::getBooks(m_repository->getCurrentLibraryId());
        const auto match = std::find_if(all.cbegin(), all.cend(),
                                        [bookId](const Book &book) { return book.id == bookId; });
        if (match != all.cend())
            requestCover(bookId, *match);
    });
    refresh();
}

bool LibraryController::hasLibrary() const
{
    return m_repository->getCurrentLibraryId().isEmpty() == false;
}

QString LibraryController::libraryName() const
{
    const std::optional<Library> current = m_repository->getCurrentLibrary();
    return current ? current->name : QString();
}

QString LibraryController::searchQuery() const
{
    return m_searchQuery;
}

int LibraryController::sortOrder() const
{
    return static_cast<int>(m_sortOrder);
}

void LibraryController::setSearchQuery(const QString &query)
{
    if (query == m_searchQuery)
        return;
    m_searchQuery = query;
    emit searchQueryChanged();
    refresh();
}

void LibraryController::setSortOrder(int order)
{
    if (order == static_cast<int>(m_sortOrder))
        return;
    m_sortOrder = static_cast<SortOrder>(order);
    emit sortOrderChanged();
    refresh();
}

void LibraryController::refresh()
{
    const std::optional<Library> current = m_repository->getCurrentLibrary();
    emit libraryChanged();
    if (!current) {
        m_model->setBooks({}, {});
        return;
    }

    QList<Book> loaded = DatabaseHelper::getBooks(current->id);
    DatabaseHelper::sortBooks(loaded, m_sortOrder);
    if (!m_searchQuery.trimmed().isEmpty())
        loaded = DatabaseHelper::filterBooks(loaded, m_searchQuery);

    // Covers that are already on disk are exposed immediately; anything else
    // is fetched on demand once the delegate reads CoverRole.
    QHash<qlonglong, QString> coverUrls;
    for (const Book &book : loaded) {
        const QString path = coverPath(current->id, book.id);
        if (QFileInfo::exists(path))
            coverUrls.insert(book.id, QUrl::fromLocalFile(path).toString());
    }
    m_model->setBooks(loaded, coverUrls);
}

QString LibraryController::coverPath(const QString &libraryId, qlonglong bookId) const
{
    const QDir cacheDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    return cacheDir.filePath(
        QStringLiteral("covers/%1_%2.jpg").arg(libraryId, QString::number(bookId)));
}

void LibraryController::requestCover(qlonglong bookId, const Book &book)
{
    if (m_pendingCovers.contains(bookId))
        return;
    const std::optional<Library> current = m_repository->getCurrentLibrary();
    if (!current || !book.hasCover)
        return;

    const QString target = coverPath(current->id, bookId);
    QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .mkpath(QStringLiteral("covers"));

    m_pendingCovers.insert(bookId);
    m_client->downloadToFile(
        current->sharedLinkUrl ? sharedLinkCoverPath(book) : directCoverDropboxPath(current->dropboxPath, book),
        current->sharedLinkUrl, target,
        [this, bookId, target](std::optional<DropboxFileMetadata> metadata,
                               const QString &error) {
            m_pendingCovers.remove(bookId);
            if (!error.isEmpty()) {
                // Missing covers are normal (not every book has one); log quietly.
                qCDebug(kalibreboxCore).noquote()
                    << QStringLiteral("Cover download for book %1 failed: %2")
                           .arg(bookId)
                           .arg(error);
                return;
            }
            m_model->setCover(bookId, QUrl::fromLocalFile(target).toString());
        });
}

QString LibraryController::directCoverDropboxPath(const QString &libraryPath,
                                                  const Book &book) const
{
    return QStringLiteral("%1/%2/cover.jpg").arg(libraryPath, book.path);
}

QString LibraryController::sharedLinkCoverPath(const Book &book) const
{
    return QStringLiteral("/%1/cover.jpg").arg(book.path);
}

}
