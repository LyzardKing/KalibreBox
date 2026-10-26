#pragma once

#include "core/model/Book.h"
#include "core/model/Library.h"
#include "core/model/SortOrder.h"
#include "ui/BookListModel.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>
#include <functional>

namespace KalibreBox {

class DropboxClient;
class LibraryRepository;

/**
 * UI controller for the library book list: loads the current library's
 * metadata.db, applies sort order / search, lazily fetches covers, and owns
 * the library setup (add/sync) and Dropbox sign-in flows used by the app.
 */
class LibraryController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(BookListModel *books READ books CONSTANT)
    Q_PROPERTY(bool hasLibrary READ hasLibrary NOTIFY libraryChanged)
    Q_PROPERTY(QString libraryName READ libraryName NOTIFY libraryChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)
    Q_PROPERTY(int sortOrder READ sortOrder WRITE setSortOrder NOTIFY sortOrderChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY signedInChanged)
    Q_PROPERTY(bool awaitingAuthCode READ awaitingAuthCode NOTIFY awaitingAuthCodeChanged)

public:
    explicit LibraryController(DropboxClient *client, QObject *parent = nullptr);

    BookListModel *books() const { return m_model; }
    bool hasLibrary() const;
    QString libraryName() const;
    QString searchQuery() const;
    int sortOrder() const;
    QString status() const;
    bool syncing() const;
    bool busy() const;
    bool signedIn() const;
    bool awaitingAuthCode() const;

    void setSearchQuery(const QString &query);
    void setSortOrder(int order);

    Q_INVOKABLE void refresh();
    /// Downloads the current library's metadata.db and reloads the list.
    Q_INVOKABLE void syncLibrary();
    /// Path may be a plain folder path or a shared link URL.
    Q_INVOKABLE void addLibrary(const QString &nameOrPath, const QString &name);
    Q_INVOKABLE void removeCurrentLibrary();
    /// Folder new downloads land in (XDG DownloadLocation).
    Q_INVOKABLE QString downloadsFolder() const;
    Q_INVOKABLE void signIn();
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void submitAuthCode(const QString &code);
    /// Downloads the format file into the cache and opens it with the
    /// system default application.
    Q_INVOKABLE void openFormat(qlonglong bookId, const QString &bookPath,
                                const QString &fileName, const QString &format);
    /// Downloads the format file into the given destination folder
    /// (file URL), mirroring Android's save-to-Downloads behavior.
    Q_INVOKABLE void saveFormat(qlonglong bookId, const QString &bookPath,
                                const QString &fileName, const QString &format,
                                const QUrl &destinationFolder);

signals:
    void libraryChanged();
    void searchQueryChanged();
    void sortOrderChanged();
    void statusChanged();
    void syncingChanged();
    void busyChanged();
    void signedInChanged();
    void awaitingAuthCodeChanged();

private:
    QString coverPath(const QString &libraryId, qlonglong bookId) const;
    void requestCover(qlonglong bookId, const Book &book);
    void setStatus(const QString &status);
    void setupAuthConnections();
    void setBusy(bool busy);
    void downloadMetadata(const std::optional<Library> &library);

    /// Absolute Dropbox path of a book format file, or the shared-link
    /// relative path in shared-link libraries.
    QString remotePathFor(const QString &bookPath, const QString &fileName,
                          const QString &format) const;

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

    QString m_status;
    bool m_syncing = false;
    bool m_busy = false;
    bool m_awaitingAuthCode = false;
};

}
