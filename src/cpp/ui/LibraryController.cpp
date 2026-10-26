#include "ui/LibraryController.h"

#include "core/data/LibraryRepository.h"
#include "core/db/DatabaseHelper.h"
#include "core/dropbox/DropboxAuthProvider.h"
#include "core/dropbox/DropboxClient.h"
#include "core/dropbox/DropboxUrls.h"

#include <QDesktopServices>
#include <QDir>
#include <QLoggingCategory>
#include <QSettings>
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
    m_statusTimer.setSingleShot(true);
    m_statusTimer.setInterval(4000);
    connect(&m_statusTimer, &QTimer::timeout, this, [this] { setStatus(QString()); });
    // Cover downloads are disabled for now: hasCover is only reported
    // when a cover is actually available locally (m_coverUrls).
    setupAuthConnections();

    // Bring the persisted library up to date before showing the list.
    qCInfo(kalibreboxCore) << "KalibreBox: startup -"
                           << "signedIn:" << signedIn()
                           << "hasLibrary:" << hasLibrary();
    if (hasLibrary() && signedIn())
        syncLibrary();
    else
        refresh();
}

void LibraryController::setupAuthConnections()
{
    DropboxAuthProvider *auth = m_client->authProvider();
    connect(auth, &DropboxAuthProvider::authSucceeded, this, [this](const DropboxCredential &) {
        m_awaitingAuthCode = false;
        emit awaitingAuthCodeChanged();
        emit signedInChanged();
        setTransientStatus(QStringLiteral("Signed in to Dropbox."));
        if (hasLibrary())
            syncLibrary();
    });
    connect(auth, &DropboxAuthProvider::authFailed, this, [this](const QString &message) {
        m_awaitingAuthCode = false;
        emit awaitingAuthCodeChanged();
        emit signedInChanged();
        setStatus(QStringLiteral("Sign-in failed: %1").arg(message));
    });
    connect(auth, &DropboxAuthProvider::codeRequested, this, [this]() {
        m_awaitingAuthCode = true;
        emit awaitingAuthCodeChanged();
        setStatus(QStringLiteral("Paste the code shown in the browser."));
    });
}

bool LibraryController::hasLibrary() const
{
    return !m_repository->getCurrentLibraryId().isEmpty();
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

QString LibraryController::status() const
{
    return m_status;
}

bool LibraryController::syncing() const
{
    return m_syncing;
}

bool LibraryController::signedIn() const
{
    const std::optional<DropboxCredential> credential = m_client->credential();
    return credential && credential->hasAccessToken();
}

bool LibraryController::awaitingAuthCode() const
{
    return m_awaitingAuthCode;
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

void LibraryController::setTransientStatus(const QString &status)
{
    setStatus(status);
    m_statusTimer.start();
}

void LibraryController::setStatus(const QString &status)
{
    m_statusTimer.stop();
    if (status == m_status)
        return;
    m_status = status;
    emit statusChanged();
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
    qCInfo(kalibreboxCore)
        << "Loaded" << loaded.size() << "books"
        << (m_searchQuery.trimmed().isEmpty()
                ? QString()
                : QStringLiteral("(filter: %1)").arg(m_searchQuery));
}

void LibraryController::syncLibrary()
{
    const std::optional<Library> current = m_repository->getCurrentLibrary();
    if (!current) {
        setStatus(QStringLiteral("No library configured."));
        return;
    }
    if (m_syncing)
        return;
    m_syncing = true;
    emit syncingChanged();

    // Skip the download when the remote metadata.db still matches the copy
    // the last sync saw (size + server_modified persisted in QSettings).
    QSettings settings;
    const QString prefix = QStringLiteral("metadata_%1_").arg(current->id);
    const bool hasState = settings.contains(prefix + QStringLiteral("size"));
    const qint64 storedSize = settings.value(prefix + QStringLiteral("size")).toLongLong();
    const qint64 storedModified =
        settings.value(prefix + QStringLiteral("modified")).toLongLong();

    if (!hasState) {
        downloadMetadata(current);
        return;
    }

    qCInfo(kalibreboxCore).noquote()
        << QStringLiteral("Checking remote metadata.db state for library %1 …")
               .arg(current->id);
    m_client->getMetadata(
        current->dropboxPath + QStringLiteral("/metadata.db"),
        [this, current, storedSize, storedModified](
            std::optional<DropboxFileMetadata> metadata, const QString &error) {
            if (!metadata) {
                // Cannot prove it is unchanged; fall through to a full sync.
                qCInfo(kalibreboxCore).noquote()
                    << QStringLiteral("get_metadata failed (%1); syncing anyway.")
                           .arg(error);
                downloadMetadata(current);
                return;
            }
            if (metadata->size == storedSize
                && metadata->serverModifiedEpoch == storedModified) {
                qCInfo(kalibreboxCore).noquote()
                    << QStringLiteral("metadata.db unchanged; skipping download.")
                           .arg(current->id);
                DatabaseHelper::openLibrary(current->id);
                setTransientStatus(QStringLiteral("Library up to date."));
                m_syncing = false;
                emit syncingChanged();
                refresh();
                return;
            }
            qCInfo(kalibreboxCore).noquote()
                << QStringLiteral("metadata.db changed (remote %1/%2, local %3/%4); syncing.")
                       .arg(metadata->size)
                       .arg(metadata->serverModifiedEpoch)
                       .arg(storedSize)
                       .arg(storedModified);
            downloadMetadata(current);
        });
}

void LibraryController::downloadMetadata(const std::optional<Library> &library)
{
    const std::optional<Library> current = library;
    if (!current) {
        m_syncing = false;
        emit syncingChanged();
        return;
    }

    setStatus(QStringLiteral("Downloading %1/metadata.db …").arg(current->dropboxPath));

    const QString target = DatabaseHelper::databaseFilePath(current->id);
    // Close any open connection and drop the stale copy first, so the
    // freshly downloaded file is what SQLite opens afterwards.
    DatabaseHelper::clearDatabase(current->id);
    qCInfo(kalibreboxCore).noquote()
        << QStringLiteral("Syncing library '%1': downloading %2/metadata.db -> %3")
               .arg(current->name, current->dropboxPath, target);

    m_client->downloadToFile(
        current->dropboxPath + QStringLiteral("/metadata.db"),
        current->sharedLinkUrl, target,
        [this, current, target](std::optional<DropboxFileMetadata> metadata,
                                const QString &error) {
            m_syncing = false;
            emit syncingChanged();
            if (!metadata) {
                qCWarning(kalibreboxCore).noquote()
                    << QStringLiteral("Library sync failed: %1").arg(error);
                setStatus(QStringLiteral("Library sync failed: %1").arg(error));
                return;
            }
            // Remember what was fetched so the next sync can skip the
            // download when nothing changed on the remote side.
            QSettings settings;
            const QString prefix = QStringLiteral("metadata_%1_").arg(current->id);
            settings.setValue(prefix + QStringLiteral("size"), metadata->size);
            settings.setValue(prefix + QStringLiteral("modified"),
                              metadata->serverModifiedEpoch);
            DatabaseHelper::openLibrary(current->id);
            qCInfo(kalibreboxCore).noquote()
                << QStringLiteral("Library synced: %1 bytes -> %2")
                       .arg(QString::number(metadata->size), target);
            setTransientStatus(QStringLiteral("Library synced."));
            refresh();
        });
}

void LibraryController::addLibrary(const QString &nameOrPath, const QString &name)
{
    const QString path = DropboxUrls::extractPath(nameOrPath);
    if (path.isEmpty()) {
        setStatus(QStringLiteral("Could not parse '%1' as a path or URL.").arg(nameOrPath));
        return;
    }

    Library library;
    library.id = LibraryRepository::generateLibraryId();
    library.dropboxPath = path;
    library.name = name.trimmed().isEmpty() ? Library::displayNameFromPath(path) : name.trimmed();
    if (DropboxUrls::isSharedLink(nameOrPath))
        library.sharedLinkUrl = nameOrPath.trimmed();

    if (m_repository->addLibrary(library))
        setTransientStatus(QStringLiteral("Added library '%1'.").arg(library.name));
    else
        setStatus(QStringLiteral("A library with path %1 already exists.").arg(path));
    emit libraryChanged();
    syncLibrary();
}

void LibraryController::removeCurrentLibrary()
{
    const QString currentId = m_repository->getCurrentLibraryId();
    if (currentId.isEmpty()) {
        setStatus(QStringLiteral("No current library to remove."));
        return;
    }
    if (m_repository->removeLibrary(currentId)) {
        DatabaseHelper::clearDatabase(currentId);
        setTransientStatus(QStringLiteral("Removed library."));
    }
    emit libraryChanged();
    refresh();
}

void LibraryController::signIn()
{
    setStatus(QStringLiteral("Opening Dropbox in your browser …"));
    m_client->authProvider()->startLogin();
}

void LibraryController::signOut()
{
    m_client->logout();
    setTransientStatus(QStringLiteral("Signed out."));
    emit signedInChanged();
}

void LibraryController::submitAuthCode(const QString &code)
{
    m_client->authProvider()->completeDirectSignIn(code);
}

bool LibraryController::busy() const
{
    return m_busy;
}

QString LibraryController::downloadsFolder() const
{
    const QString folder =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    qCInfo(kalibreboxCore).noquote()
        << QStringLiteral("Downloads folder: %1").arg(folder);
    return folder;
}

void LibraryController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

QString LibraryController::remotePathFor(const QString &bookPath,
                                         const QString &fileName,
                                         const QString &format) const
{
    const QString base =
        QStringLiteral("%1.%2").arg(fileName, format.toLower());
    const std::optional<Library> current = m_repository->getCurrentLibrary();
    if (current && current->sharedLinkUrl)
        // Shared links address files relative to the link root.
        return QStringLiteral("/%1/%2").arg(bookPath, base);
    return QStringLiteral("%1/%2/%3")
        .arg(current ? current->dropboxPath : QString(), bookPath, base);
}

void LibraryController::openFormat(qlonglong bookId, const QString &bookPath,
                                   const QString &fileName, const QString &format)
{
    const std::optional<Library> current = m_repository->getCurrentLibrary();
    if (!current || m_busy)
        return;
    setBusy(true);

    const QString cleanName = fileName.section(QLatin1Char('/'), -1);
    const QDir cacheDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    cacheDir.mkpath(QStringLiteral("formats"));
    const QString target = cacheDir.filePath(
        QStringLiteral("formats/%1_%2_%3.%4")
            .arg(current->id, QString::number(bookId), cleanName, format.toLower()));

    setStatus(QStringLiteral("Downloading %1 …").arg(cleanName));
    qCInfo(kalibreboxCore).noquote()
        << QStringLiteral("openFormat book %1: %2 -> %3")
               .arg(bookId)
               .arg(remotePathFor(bookPath, fileName, format), target);

    m_client->downloadToFile(
        remotePathFor(bookPath, fileName, format), current->sharedLinkUrl, target,
        [this, target, cleanName](std::optional<DropboxFileMetadata> metadata,
                                  const QString &error) {
            setBusy(false);
            if (!metadata) {
                setStatus(QStringLiteral("Could not download %1: %2").arg(cleanName, error));
                return;
            }
            QDesktopServices::openUrl(QUrl::fromLocalFile(target));
            setStatus(QString());
        });
}

void LibraryController::saveFormat(qlonglong bookId, const QString &bookPath,
                                   const QString &fileName, const QString &format,
                                   const QUrl &destinationFolder)
{
    const std::optional<Library> current = m_repository->getCurrentLibrary();
    if (!current || m_busy)
        return;

    const QString cleanName = fileName.section(QLatin1Char('/'), -1);
    const QString target = QDir(destinationFolder.toLocalFile()).filePath(
        QStringLiteral("%1.%2").arg(cleanName, format.toLower()));

    setBusy(true);
    setStatus(QStringLiteral("Downloading %1 …").arg(cleanName));
    qCInfo(kalibreboxCore).noquote()
        << QStringLiteral("saveFormat book %1: %2 -> %3")
               .arg(bookId)
               .arg(remotePathFor(bookPath, fileName, format), target);

    m_client->downloadToFile(
        remotePathFor(bookPath, fileName, format), current->sharedLinkUrl, target,
        [this, target, cleanName](std::optional<DropboxFileMetadata> metadata,
                                  const QString &error) {
            setBusy(false);
            if (!metadata) {
                setStatus(QStringLiteral("Could not download %1: %2").arg(cleanName, error));
                return;
            }
            setTransientStatus(QStringLiteral("Saved: %1").arg(target));
        });
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
            if (!metadata) {
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
