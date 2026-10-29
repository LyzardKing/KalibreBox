#pragma once

#include "core/model/Book.h"
#include "core/model/Library.h"
#include "core/model/SortOrder.h"
#include "ui/BookListModel.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <functional>

namespace KalibreBox
{

class DropboxClient;
class LibraryRepository;

namespace Opds
{
class OpdsClient;
class OpdsSource;
}

/**
 * UI controller for the library book list: loads the current library's
 * metadata.db, applies sort order / search, lazily fetches covers, and owns
 * the library setup (add/sync) and Dropbox sign-in flows used by the app.
 */
class LibraryController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(KalibreBox::BookListModel *books READ books CONSTANT)
    Q_PROPERTY(bool hasLibrary READ hasLibrary NOTIFY libraryChanged)
    Q_PROPERTY(QString libraryName READ libraryName NOTIFY libraryChanged)
    /// Every configured library as {id, name, location, isOpds, current} maps.
    Q_PROPERTY(QVariantList libraries READ libraries NOTIFY librariesChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)
    Q_PROPERTY(int sortOrder READ sortOrder WRITE setSortOrder NOTIFY sortOrderChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY signedInChanged)
    Q_PROPERTY(bool awaitingAuthCode READ awaitingAuthCode NOTIFY awaitingAuthCodeChanged)
    Q_PROPERTY(bool isOpds READ isOpds NOTIFY libraryChanged)
    /// More of the current OPDS listing can be loaded (scroll to the end).
    Q_PROPERTY(bool canLoadMore READ canLoadMore NOTIFY canLoadMoreChanged)
    /// The formats of an OPDS book are being fetched.
    Q_PROPERTY(bool detailsLoading READ detailsLoading NOTIFY detailsLoadingChanged)
    /// Books gathered per load step in OPDS libraries.
    Q_PROPERTY(int opdsBatchSize READ opdsBatchSize WRITE setOpdsBatchSize NOTIFY opdsBatchSizeChanged)
    Q_PROPERTY(QUrl downloadFolder READ downloadFolder WRITE setDownloadFolder NOTIFY downloadFolderChanged)
    Q_PROPERTY(QString downloadFolderPath READ downloadFolderPath NOTIFY downloadFolderChanged)
    Q_PROPERTY(bool usePublicDownloads READ usePublicDownloads WRITE setUsePublicDownloads NOTIFY usePublicDownloadsChanged)
    Q_PROPERTY(int theme READ theme WRITE setTheme NOTIFY themeChanged)

public:
    explicit LibraryController(DropboxClient *client, QObject *parent = nullptr);

    [[nodiscard]] BookListModel *books() const
    {
        return m_model;
    }
    [[nodiscard]] bool hasLibrary() const;
    [[nodiscard]] QString libraryName() const;
    [[nodiscard]] QVariantList libraries() const;
    [[nodiscard]] QString searchQuery() const;
    [[nodiscard]] int sortOrder() const;
    [[nodiscard]] QString status() const;
    [[nodiscard]] bool syncing() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] bool signedIn() const;
    [[nodiscard]] bool awaitingAuthCode() const;
    [[nodiscard]] bool isOpds() const;
    [[nodiscard]] bool canLoadMore() const;
    [[nodiscard]] bool detailsLoading() const;
    [[nodiscard]] int opdsBatchSize() const;
    void setOpdsBatchSize(int count);
    [[nodiscard]] QUrl downloadFolder() const;
    [[nodiscard]] QString downloadFolderPath() const;
    void setDownloadFolder(const QUrl &folder);
    [[nodiscard]] bool usePublicDownloads() const;
    void setUsePublicDownloads(bool usePublicDownloads);
    [[nodiscard]] int theme() const;
    void setTheme(int theme);

    void setSearchQuery(const QString &query);
    void setSortOrder(int order);

    Q_INVOKABLE void refresh();
    /// OPDS: loads the next batch of the listing.
    Q_INVOKABLE void loadMore();
    /// OPDS: fetches the formats of a book that was listed without them.
    Q_INVOKABLE void loadBookDetails(qlonglong bookId);
    /// The current state of an OPDS book, shaped like a book-grid row.
    Q_INVOKABLE [[nodiscard]] QVariantMap bookDetails(qlonglong bookId) const;
    /// All authors of the current library (unaffected by the search filter).
    Q_INVOKABLE [[nodiscard]] QStringList authors() const;
    /// Moves pending cover downloads for rows [first, last] to the queue front.
    Q_INVOKABLE void prioritizeCovers(int first, int last);
    /// Downloads the current library's metadata.db and reloads the list.
    Q_INVOKABLE void syncLibrary();
    /// Path may be a plain folder path or a shared link URL.
    Q_INVOKABLE void addLibrary(const QString &nameOrPath, const QString &name);
    /// Adds an OPDS catalogue; username/password are optional (HTTP Basic).
    Q_INVOKABLE void addOpdsLibrary(const QString &url, const QString &name, const QString &username, const QString &password);
    Q_INVOKABLE void removeCurrentLibrary();
    /// Removes any library together with its local data (metadata copy, saved password).
    Q_INVOKABLE void removeLibrary(const QString &libraryId);
    Q_INVOKABLE void renameLibrary(const QString &libraryId, const QString &name);
    /// Makes the library the current one and loads it.
    Q_INVOKABLE void selectLibrary(const QString &libraryId);
    /// Folder new downloads land in (XDG DownloadLocation).
    Q_INVOKABLE [[nodiscard]] QString downloadsFolder() const;
    Q_INVOKABLE void signIn();
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void submitAuthCode(const QString &code);
    /// Downloads the format file into the cache and opens it with the
    /// system default application.
    Q_INVOKABLE void openFormat(qlonglong bookId, const QString &bookPath, const QString &fileName, const QString &format);
    /// Downloads the format file into the cache and offers it to the
    /// system share sheet (Android only).
    Q_INVOKABLE void shareFormat(qlonglong bookId, const QString &bookPath, const QString &fileName, const QString &format);
    /// Downloads the format file into the given destination folder
    /// (file URL), mirroring Android's save-to-Downloads behavior.
    Q_INVOKABLE void saveFormat(qlonglong bookId, const QString &bookPath, const QString &fileName, const QString &format, const QUrl &destinationFolder);

signals:
    void libraryChanged();
    /// The set of libraries or the current one changed (not on every refresh).
    void librariesChanged();
    void searchQueryChanged();
    void sortOrderChanged();
    void statusChanged();
    void syncingChanged();
    void coversQueued();
    void busyChanged();
    void signedInChanged();
    void awaitingAuthCodeChanged();
    void canLoadMoreChanged();
    void detailsLoadingChanged();
    void bookDetailsLoaded(qlonglong bookId);
    void opdsBatchSizeChanged();
    void downloadFolderChanged();
    void usePublicDownloadsChanged();
    void themeChanged();

private:
    [[nodiscard]] QString coverPath(const QString &libraryId, qlonglong bookId) const;
    void requestCover(qlonglong bookId, const Book &book);
    /// Queues every book whose cover is not cached yet (as the Android app's
    /// background cover worker does) and downloads them a few at a time.
    void cacheCovers();
    void pumpCoverQueue();
    void setStatus(const QString &status);
    // Shows a message that clears itself after a few seconds.
    void setTransientStatus(const QString &status);
    void setupAuthConnections();
    void setupOpdsConnections();
    void loadOpdsLibrary(const Library &library);
    void onOpdsBooksAdded(const QList<Book> &books);
    void pumpOpdsCovers();
    [[nodiscard]] QString opdsCoverPath(const QString &url) const;
    [[nodiscard]] std::optional<BookFormat> opdsFormat(qlonglong bookId, const QString &formatLabel) const;
    /// File name for saving an OPDS format; tells same-extension editions apart.
    [[nodiscard]] QString opdsDisplayName(qlonglong bookId, const BookFormat &format) const;
    void setBusy(bool busy);
    void downloadMetadata(const std::optional<Library> &library);
    /// Downloads a format file into the cache, then calls `onReady` with
    /// the local path (used by open and share).
    void fetchFormatToCache(qlonglong bookId,
                            const QString &bookPath,
                            const QString &fileName,
                            const QString &format,
                            const std::function<void(const QString &)> &onReady);

    /// Absolute Dropbox path of a book format file, or the shared-link
    /// relative path in shared-link libraries.
    [[nodiscard]] QString remotePathFor(const QString &bookPath, const QString &fileName, const QString &format) const;

    /// Absolute Dropbox path of a book's cover.jpg.
    [[nodiscard]] QString directCoverDropboxPath(const QString &libraryPath, const Book &book) const;
    /// Cover relative to the shared link root.
    [[nodiscard]] QString sharedLinkCoverPath(const Book &book) const;

    DropboxClient *m_client = nullptr;
    LibraryRepository *m_repository = nullptr;
    BookListModel *m_model = nullptr;
    Opds::OpdsClient *m_opdsClient = nullptr;
    Opds::OpdsSource *m_opds = nullptr;
    QHash<qlonglong, Book> m_opdsBooks;
    QList<qlonglong> m_opdsCoverQueue;
    int m_opdsCoversPending = 0;
    QTimer m_searchTimer;
    bool m_detailsLoading = false;

    QString m_searchQuery;
    SortOrder m_sortOrder = SortOrder::RecentlyAdded;
    QSet<qlonglong> m_pendingCovers;
    QList<Book> m_coverQueue;
    QString m_coverQueueLibraryId;
    std::optional<qlonglong> m_currentBookId;

    QString m_status;
    QTimer m_statusTimer;
    bool m_syncing = false;
    bool m_busy = false;
    bool m_awaitingAuthCode = false;
};

}
