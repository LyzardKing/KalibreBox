#include "ui/LibraryController.h"

#include "core/data/LibraryRepository.h"
#include "core/db/DatabaseHelper.h"
#include "core/dropbox/DropboxAuthProvider.h"
#include "core/dropbox/DropboxClient.h"
#include "core/dropbox/DropboxUrls.h"
#include "core/opds/OpdsClient.h"
#include "core/opds/OpdsSource.h"
#include "ui/AndroidDownloads.h"
#include "ui/AndroidFiles.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleHints>
#include <QUrl>

#include <algorithm>

Q_LOGGING_CATEGORY(kalibreboxCore, "kalibrebox.core")

namespace KalibreBox
{

namespace
{
// Kept out of the library JSON; stored unencrypted like the Dropbox tokens.
QString opdsPasswordKey(const QString &libraryId)
{
    return QStringLiteral("opds_password_%1").arg(libraryId);
}

constexpr int kDefaultOpdsBatchSize = 20;
constexpr int kMaxOpdsBatchSize = 500;
constexpr int kMaxParallelOpdsCovers = 4;
constexpr int kOpdsSearchDebounceMs = 400;
constexpr int kSystemTheme = 0;
constexpr int kLightTheme = 1;
constexpr int kDarkTheme = 2;

void applyTheme(int theme)
{
    const Qt::ColorScheme colorScheme = theme == kLightTheme ? Qt::ColorScheme::Light
                                        : theme == kDarkTheme ? Qt::ColorScheme::Dark
                                                              : Qt::ColorScheme::Unknown;
    QGuiApplication::styleHints()->setColorScheme(colorScheme);
}

QString fileSafe(const QString &text)
{
    static const QRegularExpression unsafe(QStringLiteral("[/\\\\:*?\"<>|]"));
    QString safe = text;
    return safe.replace(unsafe, QStringLiteral("_")).simplified();
}
}

LibraryController::LibraryController(DropboxClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_repository(new LibraryRepository())
    , m_model(new BookListModel(this))
    , m_opdsClient(new Opds::OpdsClient(this))
    , m_opds(new Opds::OpdsSource(m_opdsClient, this))
{
    applyTheme(theme());
    m_searchTimer.setSingleShot(true);
    m_searchTimer.setInterval(kOpdsSearchDebounceMs);
    connect(&m_searchTimer, &QTimer::timeout, this, &LibraryController::refresh);
    setupOpdsConnections();
    m_statusTimer.setSingleShot(true);
    m_statusTimer.setInterval(4000);
    connect(&m_statusTimer, &QTimer::timeout, this, [this] {
        setStatus(QString());
    });
    setupAuthConnections();

    // Show the local copy right away; the sync runs afterwards and only
    // refreshes the list if the remote metadata.db changed.
    qCInfo(kalibreboxCore) << "KalibreBox: startup -"
                           << "signedIn:" << signedIn() << "hasLibrary:" << hasLibrary();
    refresh();
    if (hasLibrary() && signedIn() && !isOpds())
        QTimer::singleShot(0, this, &LibraryController::syncLibrary);
}

void LibraryController::setupOpdsConnections()
{
    connect(m_opds, &Opds::OpdsSource::reset, this, [this] {
        m_opdsBooks.clear();
        m_opdsCoverQueue.clear();
        m_model->setBooks({}, {});
    });
    connect(m_opds, &Opds::OpdsSource::booksAdded, this, &LibraryController::onOpdsBooksAdded);
    connect(m_opds, &Opds::OpdsSource::loadingChanged, this, [this] {
        m_syncing = m_opds->loading();
        emit syncingChanged();
        emit canLoadMoreChanged();
    });
    connect(m_opds, &Opds::OpdsSource::failed, this, [this](const QString &message, bool offline) {
        // Nothing is kept offline: the list stays empty and says why.
        setStatus(offline ? QStringLiteral("Can't reach the catalogue: %1").arg(message) : message);
    });
    connect(m_opds, &Opds::OpdsSource::detailsReady, this, [this](const Book &book) {
        if (!m_opdsBooks.contains(book.id))
            return;
        m_opdsBooks.insert(book.id, book);
        m_model->updateBook(book);
        m_detailsLoading = false;
        emit detailsLoadingChanged();
        emit bookDetailsLoaded(book.id);
    });
    connect(m_opds, &Opds::OpdsSource::detailsFailed, this, [this](qlonglong, const QString &message) {
        m_detailsLoading = false;
        emit detailsLoadingChanged();
        setStatus(QStringLiteral("Could not load the book: %1").arg(message));
    });
}

void LibraryController::setupAuthConnections()
{
    DropboxAuthProvider const *auth = m_client->authProvider();
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
    connect(auth, &DropboxAuthProvider::codeRequested, this, [this] {
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

QVariantList LibraryController::libraries() const
{
    const QString currentId = m_repository->getCurrentLibrary().value_or(Library{}).id;
    QVariantList result;
    const QList<Library> all = m_repository->getLibraries();
    for (const Library &library : all) {
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), library.id);
        entry.insert(QStringLiteral("name"), library.name);
        entry.insert(QStringLiteral("isOpds"), library.isOpds());
        entry.insert(QStringLiteral("location"), library.sourceKey());
        entry.insert(QStringLiteral("current"), library.id == currentId);
        result.append(entry);
    }
    return result;
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

bool LibraryController::isOpds() const
{
    const std::optional<Library> current = m_repository->getCurrentLibrary();
    return current && current->isOpds();
}

bool LibraryController::canLoadMore() const
{
    return m_opds->canLoadMore() && isOpds();
}

bool LibraryController::detailsLoading() const
{
    return m_detailsLoading;
}

int LibraryController::opdsBatchSize() const
{
    return std::clamp(QSettings().value(QStringLiteral("opds/batchSize"), kDefaultOpdsBatchSize).toInt(), 1, kMaxOpdsBatchSize);
}

void LibraryController::setOpdsBatchSize(int count)
{
    const int clamped = std::clamp(count, 1, kMaxOpdsBatchSize);
    if (clamped == opdsBatchSize())
        return;
    QSettings().setValue(QStringLiteral("opds/batchSize"), clamped);
    m_opds->setBatchSize(clamped);
    emit opdsBatchSizeChanged();
}

QUrl LibraryController::downloadFolder() const
{
    const QString stored = QSettings().value(QStringLiteral("downloads/folder")).toString();
    if (stored.isEmpty())
        return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));

    const QUrl folder(stored);
    return folder.scheme().isEmpty() ? QUrl::fromLocalFile(stored) : folder;
}

QString LibraryController::downloadFolderPath() const
{
    const QUrl folder = downloadFolder();
    return folder.isLocalFile() ? folder.toLocalFile() : QString();
}

void LibraryController::setDownloadFolder(const QUrl &folder)
{
#ifdef Q_OS_ANDROID
    if (folder.scheme() == QLatin1String("content")) {
        QString error;
        if (!AndroidDownloads::retainFolderPermission(folder, &error)) {
            qCWarning(kalibreboxCore).noquote() << "Could not retain access to the selected download folder:" << error;
            setStatus(QStringLiteral("Could not use that folder: %1").arg(error));
            return;
        }

        const QString value = folder.toString(QUrl::FullyEncoded);
        if (value != QSettings().value(QStringLiteral("downloads/folder")).toString()) {
            QSettings().setValue(QStringLiteral("downloads/folder"), value);
            emit downloadFolderChanged();
        }
        setUsePublicDownloads(false);
        return;
    }
#endif

    if (!folder.isLocalFile()) {
        qCWarning(kalibreboxCore) << "Ignoring a non-local download folder URL:" << folder;
        return;
    }

    const QString path = QDir::cleanPath(folder.toLocalFile());
    if (path.isEmpty() || !QDir(path).exists()) {
        qCWarning(kalibreboxCore).noquote() << "Ignoring a download folder that does not exist:" << path;
        return;
    }
    if (path == downloadFolder().toLocalFile()) {
#ifdef Q_OS_ANDROID
        setUsePublicDownloads(false);
#endif
        return;
    }

    QSettings().setValue(QStringLiteral("downloads/folder"), path);
    emit downloadFolderChanged();
#ifdef Q_OS_ANDROID
    setUsePublicDownloads(false);
#endif
}

bool LibraryController::usePublicDownloads() const
{
#ifdef Q_OS_ANDROID
    constexpr bool defaultValue = true;
#else
    constexpr bool defaultValue = false;
#endif
    return QSettings().value(QStringLiteral("downloads/usePublic"), defaultValue).toBool();
}

void LibraryController::setUsePublicDownloads(bool usePublicDownloads)
{
#ifdef Q_OS_ANDROID
    if (!usePublicDownloads && !QSettings().contains(QStringLiteral("downloads/folder"))) {
        setStatus(QStringLiteral("Choose a custom folder first."));
        return;
    }
#endif
    if (usePublicDownloads == this->usePublicDownloads())
        return;

    QSettings().setValue(QStringLiteral("downloads/usePublic"), usePublicDownloads);
    emit usePublicDownloadsChanged();
}

int LibraryController::theme() const
{
    return std::clamp(QSettings().value(QStringLiteral("appearance/theme"), kSystemTheme).toInt(), kSystemTheme, kDarkTheme);
}

void LibraryController::setTheme(int theme)
{
    const int selectedTheme = std::clamp(theme, kSystemTheme, kDarkTheme);
    if (selectedTheme == this->theme())
        return;

    QSettings().setValue(QStringLiteral("appearance/theme"), selectedTheme);
    applyTheme(selectedTheme);
    emit themeChanged();
}

void LibraryController::setSearchQuery(const QString &query)
{
    if (query == m_searchQuery)
        return;
    m_searchQuery = query;
    emit searchQueryChanged();
    // Catalogue searches are server requests, so typing is debounced.
    if (isOpds())
        m_searchTimer.start();
    else
        refresh();
}

void LibraryController::setSortOrder(int order)
{
    if (order == static_cast<int>(m_sortOrder))
        return;
    m_sortOrder = static_cast<SortOrder>(order);
    emit sortOrderChanged();
    // Catalogues list in the server's order.
    if (!isOpds())
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
        m_opds->cancel();
        m_model->setBooks({}, {});
        return;
    }
    if (current->isOpds()) {
        loadOpdsLibrary(*current);
        return;
    }
    // Leaving a catalogue stops whatever it was still loading.
    m_opds->cancel();

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
    qCInfo(kalibreboxCore) << "Loaded" << loaded.size() << "books"
                           << (m_searchQuery.trimmed().isEmpty() ? QString() : QStringLiteral("(filter: %1)").arg(m_searchQuery));
}

QStringList LibraryController::authors() const
{
    const std::optional<Library> current = m_repository->getCurrentLibrary();
    return current && !current->isOpds() ? DatabaseHelper::getAuthors(current->id) : QStringList();
}

void LibraryController::syncLibrary()
{
    const std::optional<Library> current = m_repository->getCurrentLibrary();
    if (!current) {
        setStatus(QStringLiteral("No library configured."));
        return;
    }
    if (current->isOpds()) {
        refresh();
        return;
    }
    if (m_syncing)
        return;
    m_syncing = true;
    emit syncingChanged();

    // Skip the download when the remote metadata.db still matches the copy
    // the last sync saw (size + server_modified persisted in QSettings).
    QSettings const settings;
    const QString prefix = QStringLiteral("metadata_%1_").arg(current->id);
    // A missing or empty local copy means the stored state is stale.
    const bool hasState = settings.contains(prefix + QStringLiteral("size")) && QFileInfo(DatabaseHelper::databaseFilePath(current->id)).size() > 0;
    const qint64 storedSize = settings.value(prefix + QStringLiteral("size")).toLongLong();
    const qint64 storedModified = settings.value(prefix + QStringLiteral("modified")).toLongLong();

    if (!hasState) {
        downloadMetadata(current);
        return;
    }

    qCInfo(kalibreboxCore).noquote() << QStringLiteral("Checking remote metadata.db state for library %1 …").arg(current->id);
    m_client->getMetadata(current->dropboxPath + QStringLiteral("/metadata.db"),
                          [this, current, storedSize, storedModified](std::optional<DropboxFileMetadata> metadata, const QString &error) {
                              if (!metadata) {
                                  // Cannot prove it is unchanged; fall through to a full sync.
                                  qCInfo(kalibreboxCore).noquote() << QStringLiteral("get_metadata failed (%1); syncing anyway.").arg(error);
                                  downloadMetadata(current);
                                  return;
                              }
                              if (metadata->size == storedSize && metadata->serverModifiedEpoch == storedModified) {
                                  qCInfo(kalibreboxCore).noquote() << QStringLiteral("metadata.db unchanged; skipping download.");
                                  DatabaseHelper::openLibrary(current->id);
                                  m_syncing = false;
                                  emit syncingChanged();
                                  cacheCovers();
                                  return;
                              }
                              qCInfo(kalibreboxCore).noquote() << QStringLiteral("metadata.db changed (remote %1/%2, local %3/%4); syncing.")
                                                                      .arg(metadata->size)
                                                                      .arg(metadata->serverModifiedEpoch)
                                                                      .arg(storedSize)
                                                                      .arg(storedModified);
                              downloadMetadata(current);
                          });
}

void LibraryController::downloadMetadata(const std::optional<Library> &library)
{
    const std::optional<Library> &current = library;
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
    QSettings staleState;
    staleState.remove(QStringLiteral("metadata_%1_size").arg(current->id));
    staleState.remove(QStringLiteral("metadata_%1_modified").arg(current->id));
    qCInfo(kalibreboxCore).noquote()
        << QStringLiteral("Syncing library '%1': downloading %2/metadata.db -> %3").arg(current->name, current->dropboxPath, target);

    m_client->downloadToFile(current->dropboxPath + QStringLiteral("/metadata.db"),
                             current->sharedLinkUrl,
                             target,
                             [this, current, target](std::optional<DropboxFileMetadata> metadata, const QString &error) {
                                 m_syncing = false;
                                 emit syncingChanged();
                                 if (!metadata) {
                                     qCWarning(kalibreboxCore).noquote() << QStringLiteral("Library sync failed: %1").arg(error);
                                     setStatus(QStringLiteral("Library sync failed: %1").arg(error));
                                     return;
                                 }
                                 // Remember what was fetched so the next sync can skip the
                                 // download when nothing changed on the remote side.
                                 QSettings settings;
                                 const QString prefix = QStringLiteral("metadata_%1_").arg(current->id);
                                 settings.setValue(prefix + QStringLiteral("size"), metadata->size);
                                 settings.setValue(prefix + QStringLiteral("modified"), metadata->serverModifiedEpoch);
                                 DatabaseHelper::openLibrary(current->id);
                                 qCInfo(kalibreboxCore).noquote()
                                     << QStringLiteral("Library synced: %1 bytes -> %2").arg(QString::number(metadata->size), target);
                                 setTransientStatus(QStringLiteral("Library synced."));
                                 refresh();
                                 cacheCovers();
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

    if (!m_repository->addLibrary(library)) {
        setStatus(QStringLiteral("A library with path %1 already exists.").arg(path));
        return;
    }
    setTransientStatus(QStringLiteral("Added library '%1'.").arg(library.name));
    // The new library is what the user wants to see next.
    m_repository->setCurrentLibraryId(library.id);
    m_coverQueue.clear();
    emit librariesChanged();
    refresh();
    syncLibrary();
}

void LibraryController::addOpdsLibrary(const QString &url, const QString &name, const QString &username, const QString &password)
{
    const QUrl parsed(url.trimmed(), QUrl::StrictMode);
    const bool schemeOk = parsed.scheme() == QLatin1String("http") || parsed.scheme() == QLatin1String("https");
    if (!parsed.isValid() || !schemeOk || parsed.host().isEmpty()) {
        setStatus(QStringLiteral("'%1' is not a valid http(s) catalogue URL.").arg(url.trimmed()));
        return;
    }

    Library library;
    library.id = LibraryRepository::generateLibraryId();
    library.sourceType = LibrarySourceType::Opds;
    library.opdsUrl = parsed.toString();
    library.opdsUsername = username.trimmed();
    library.name = name.trimmed().isEmpty() ? Library::displayNameFromUrl(library.opdsUrl) : name.trimmed();

    if (!m_repository->addLibrary(library)) {
        setStatus(QStringLiteral("A library for %1 already exists.").arg(library.opdsUrl));
        return;
    }
    if (!library.opdsUsername.isEmpty())
        QSettings().setValue(opdsPasswordKey(library.id), password);

    setTransientStatus(QStringLiteral("Added library '%1'.").arg(library.name));
    m_repository->setCurrentLibraryId(library.id);
    m_coverQueue.clear();
    emit librariesChanged();
    refresh();
}

void LibraryController::selectLibrary(const QString &libraryId)
{
    if (!m_repository->getLibraryById(libraryId))
        return;
    const bool changed = m_repository->getCurrentLibraryId() != libraryId;
    if (changed) {
        m_repository->setCurrentLibraryId(libraryId);
        m_coverQueue.clear();
    }
    emit librariesChanged();
    refresh();
    // Re-selecting the current library must not trigger another sync.
    if (changed && signedIn() && !isOpds())
        syncLibrary();
}

void LibraryController::removeCurrentLibrary()
{
    removeLibrary(m_repository->getCurrentLibraryId());
}

void LibraryController::removeLibrary(const QString &libraryId)
{
    const std::optional<Library> library = m_repository->getLibraryById(libraryId);
    if (!library) {
        setStatus(QStringLiteral("No such library to remove."));
        return;
    }
    const bool wasCurrent = m_repository->getCurrentLibraryId() == libraryId || m_repository->getCurrentLibrary().value_or(Library{}).id == libraryId;
    if (!m_repository->removeLibrary(libraryId))
        return;

    DatabaseHelper::clearDatabase(libraryId);
    QSettings settings;
    settings.remove(opdsPasswordKey(libraryId));
    // Without these a re-added library would skip its first download.
    settings.remove(QStringLiteral("metadata_%1_size").arg(libraryId));
    settings.remove(QStringLiteral("metadata_%1_modified").arg(libraryId));
    setTransientStatus(QStringLiteral("Removed library '%1'.").arg(library->name));

    emit librariesChanged();
    if (wasCurrent) {
        m_coverQueue.clear();
        refresh();
    }
}

void LibraryController::renameLibrary(const QString &libraryId, const QString &name)
{
    std::optional<Library> library = m_repository->getLibraryById(libraryId);
    const QString trimmed = name.trimmed();
    if (!library || trimmed.isEmpty())
        return;
    if (library->name == trimmed)
        return;
    library->name = trimmed;
    if (!m_repository->updateLibrary(*library))
        return;
    emit librariesChanged();
    // The page title shows the current library's name.
    emit libraryChanged();
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
    const QString folder = downloadFolder().toLocalFile();
    qCInfo(kalibreboxCore).noquote() << QStringLiteral("Downloads folder: %1").arg(folder);
    return folder;
}

void LibraryController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

QString LibraryController::remotePathFor(const QString &bookPath, const QString &fileName, const QString &format) const
{
    const QString base = QStringLiteral("%1.%2").arg(fileName, format.toLower());
    const std::optional<Library> current = m_repository->getCurrentLibrary();
    if (current && current->sharedLinkUrl)
        // Shared links address files relative to the link root.
        return QStringLiteral("/%1/%2").arg(bookPath, base);
    return QStringLiteral("%1/%2/%3").arg(current ? current->dropboxPath : QString(), bookPath, base);
}

void LibraryController::fetchFormatToCache(qlonglong bookId,
                                           const QString &bookPath,
                                           const QString &fileName,
                                           const QString &format,
                                           const std::function<void(const QString &)> &onReady)
{
    const std::optional<Library> current = m_repository->getCurrentLibrary();
    if (!current || m_busy)
        return;

    if (current->isOpds()) {
        const std::optional<BookFormat> remote = opdsFormat(bookId, format);
        if (!remote) {
            setStatus(QStringLiteral("This format is no longer available."));
            return;
        }
        setBusy(true);
        const QDir cacheDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
        cacheDir.mkpath(QStringLiteral("formats"));
        // The address is part of the name: editions of one book can share an extension.
        const QString unique = QString::fromLatin1(QCryptographicHash::hash(remote->url.toUtf8(), QCryptographicHash::Sha1).toHex().left(8));
        const QString target =
            cacheDir.filePath(QStringLiteral("formats/%1_%2_%3_%4.%5").arg(current->id, QString::number(bookId), unique, remote->fileName, remote->extension));
        setStatus(QStringLiteral("Downloading %1 …").arg(remote->fileName));
        m_opdsClient->downloadFile(QUrl(remote->url), target, [this, target, name = remote->fileName, onReady](const Opds::FileResult &result) {
            setBusy(false);
            if (!result.ok) {
                setStatus(QStringLiteral("Could not download %1: %2").arg(name, result.error));
                return;
            }
            setStatus(QString());
            onReady(target);
        });
        return;
    }

    setBusy(true);

    const QString cleanName = fileName.section(QLatin1Char('/'), -1);
    const QDir cacheDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    cacheDir.mkpath(QStringLiteral("formats"));
    const QString target = cacheDir.filePath(QStringLiteral("formats/%1_%2_%3.%4").arg(current->id, QString::number(bookId), cleanName, format.toLower()));

    setStatus(QStringLiteral("Downloading %1 …").arg(cleanName));
    qCInfo(kalibreboxCore).noquote() << QStringLiteral("fetchFormat book %1: %2 -> %3").arg(bookId).arg(remotePathFor(bookPath, fileName, format), target);

    m_client->downloadToFile(remotePathFor(bookPath, fileName, format),
                             current->sharedLinkUrl,
                             target,
                             [this, target, cleanName, onReady](const std::optional<DropboxFileMetadata> &metadata, const QString &error) {
                                 setBusy(false);
                                 if (!metadata) {
                                     setStatus(QStringLiteral("Could not download %1: %2").arg(cleanName, error));
                                     return;
                                 }
                                 setStatus(QString());
                                 onReady(target);
                             });
}

void LibraryController::openFormat(qlonglong bookId, const QString &bookPath, const QString &fileName, const QString &format)
{
    fetchFormatToCache(bookId, bookPath, fileName, format, [this](const QString &target) {
#ifdef Q_OS_ANDROID
        QString error;
        if (!AndroidFiles::openFile(target, &error))
            setTransientStatus(QStringLiteral("Could not open the file: %1").arg(error));
#else
        QDesktopServices::openUrl(QUrl::fromLocalFile(target));
#endif
    });
}

void LibraryController::shareFormat(qlonglong bookId, const QString &bookPath, const QString &fileName, const QString &format)
{
    fetchFormatToCache(bookId, bookPath, fileName, format, [this](const QString &target) {
        QString error;
        if (!AndroidFiles::shareFile(target, &error))
            setTransientStatus(QStringLiteral("Could not share the file: %1").arg(error));
    });
}

void LibraryController::saveFormat(qlonglong bookId, const QString &bookPath, const QString &fileName, const QString &format, const QUrl &destinationFolder)
{
    const std::optional<Library> current = m_repository->getCurrentLibrary();
    if (!current || m_busy)
        return;

    if (current->isOpds()) {
        const std::optional<BookFormat> remote = opdsFormat(bookId, format);
        if (!remote) {
            setStatus(QStringLiteral("This format is no longer available."));
            return;
        }
        const QString displayName = opdsDisplayName(bookId, *remote);
        setBusy(true);
        setStatus(QStringLiteral("Downloading %1 …").arg(displayName));
        const auto onDone = [this, displayName](bool ok, const QString &error) {
            setBusy(false);
            if (!ok) {
                setStatus(QStringLiteral("Could not download %1: %2").arg(displayName, error));
                return;
            }
            setTransientStatus(QStringLiteral("Saved: %1").arg(displayName));
        };
#ifdef Q_OS_ANDROID
        const bool usePublic = usePublicDownloads();
        const QUrl outputFolder = destinationFolder;
        const QDir cacheDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
        cacheDir.mkpath(QStringLiteral("formats"));
        const QString staging = cacheDir.filePath(QStringLiteral("formats/save_%1").arg(displayName));
        m_opdsClient->downloadFile(QUrl(remote->url), staging, [staging, displayName, outputFolder, usePublic, onDone](const Opds::FileResult &result) {
            if (!result.ok) {
                onDone(false, result.error);
                return;
            }
            QFile file(staging);
            QString error;
            bool saved = false;
            if (file.open(QIODevice::ReadOnly)) {
                const QByteArray data = file.readAll();
                saved = usePublic ? AndroidDownloads::saveToPublicDownloads(data, displayName, &error)
                                  : AndroidDownloads::saveToFolder(data, outputFolder, displayName, &error);
            } else {
                error = file.errorString();
            }
            file.close();
            file.remove();
            onDone(saved, error.isEmpty() ? QStringLiteral("could not save the file") : error);
        });
#else
        m_opdsClient->downloadFile(QUrl(remote->url), QDir(destinationFolder.toLocalFile()).filePath(displayName), [onDone](const Opds::FileResult &result) {
            onDone(result.ok, result.error);
        });
#endif
        return;
    }

    const QString cleanName = fileName.section(QLatin1Char('/'), -1);
    const QString displayName = QStringLiteral("%1.%2").arg(cleanName, format.toLower());

    setBusy(true);
    setStatus(QStringLiteral("Downloading %1 …").arg(cleanName));
    qCInfo(kalibreboxCore).noquote() << QStringLiteral("saveFormat book %1: %2 -> %3").arg(bookId).arg(remotePathFor(bookPath, fileName, format), displayName);

    const auto onDone = [this, cleanName, displayName](const std::optional<DropboxFileMetadata> &metadata, const QString &error) {
        setBusy(false);
        if (!metadata) {
            setStatus(QStringLiteral("Could not download %1: %2").arg(cleanName, error));
            return;
        }
        setTransientStatus(QStringLiteral("Saved: %1").arg(displayName));
    };

#ifdef Q_OS_ANDROID
    const bool usePublic = usePublicDownloads();
    m_client->downloadToSink(
        remotePathFor(bookPath, fileName, format),
        current->sharedLinkUrl,
        [displayName, destinationFolder, usePublic](const QByteArray &data, QString *error) {
            return usePublic ? AndroidDownloads::saveToPublicDownloads(data, displayName, error)
                             : AndroidDownloads::saveToFolder(data, destinationFolder, displayName, error);
        },
        onDone);
#else
    m_client->downloadToFile(remotePathFor(bookPath, fileName, format),
                             current->sharedLinkUrl,
                             QDir(destinationFolder.toLocalFile()).filePath(displayName),
                             onDone);
#endif
}

QString LibraryController::coverPath(const QString &libraryId, qlonglong bookId) const
{
    const QDir cacheDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    return cacheDir.filePath(QStringLiteral("covers/%1_%2.jpg").arg(libraryId, QString::number(bookId)));
}

namespace
{
constexpr int kMaxParallelCoverDownloads = 4;
}

void LibraryController::cacheCovers()
{
    const std::optional<Library> current = m_repository->getCurrentLibrary();
    m_coverQueue.clear();
    if (!current || current->isOpds())
        return;
    m_coverQueueLibraryId = current->id;
    for (const Book &book : DatabaseHelper::getBooks(current->id)) {
        if (book.hasCover && !QFileInfo::exists(coverPath(current->id, book.id)))
            m_coverQueue.append(book);
    }
    qCInfo(kalibreboxCore) << "Caching" << m_coverQueue.size() << "covers";
    emit coversQueued();
    pumpCoverQueue();
}

void LibraryController::prioritizeCovers(int first, int last)
{
    if (isOpds()) {
        if (m_opdsCoverQueue.isEmpty())
            return;
        const QList<qlonglong> visible = m_model->bookIdsInRange(first, last);
        const QSet<qlonglong> wanted(visible.begin(), visible.end());
        std::ranges::stable_partition(m_opdsCoverQueue, [&wanted](qlonglong id) {
            return wanted.contains(id);
        });
        pumpOpdsCovers();
        return;
    }
    if (m_coverQueue.isEmpty())
        return;
    const QList<qlonglong> visible = m_model->bookIdsInRange(first, last);
    const QSet<qlonglong> wanted(visible.begin(), visible.end());
    auto const split = std::ranges::stable_partition(m_coverQueue, [&wanted](const Book &b) {
                           return wanted.contains(b.id);
                       }).begin();
    Q_UNUSED(split);
    pumpCoverQueue();
}

void LibraryController::pumpCoverQueue()
{
    while (!m_coverQueue.isEmpty() && m_pendingCovers.size() < kMaxParallelCoverDownloads) {
        const Book book = m_coverQueue.takeFirst();
        requestCover(book.id, book);
    }
}

void LibraryController::requestCover(qlonglong bookId, const Book &book)
{
    if (m_pendingCovers.contains(bookId))
        return;
    const std::optional<Library> current = m_repository->getCurrentLibrary();
    if (!current || !book.hasCover)
        return;

    const QString target = coverPath(current->id, bookId);
    QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).mkpath(QStringLiteral("covers"));

    m_pendingCovers.insert(bookId);
    const auto onDone = [this, bookId, target, libraryId = current->id](const std::optional<DropboxFileMetadata> &metadata, const QString &error) {
        m_pendingCovers.remove(bookId);
        if (libraryId != m_coverQueueLibraryId)
            m_coverQueue.clear();
        pumpCoverQueue();
        if (!metadata) {
            // Missing covers are normal (not every book has one); log quietly.
            qCDebug(kalibreboxCore).noquote() << QStringLiteral("Cover download for book %1 failed: %2").arg(bookId).arg(error);
            return;
        }
        const std::optional<Library> now = m_repository->getCurrentLibrary();
        if (now && now->id == libraryId)
            m_model->setCover(bookId, QUrl::fromLocalFile(target).toString());
    };

    if (current->sharedLinkUrl) {
        // The thumbnail API cannot address files inside a shared folder link.
        m_client->downloadToFile(sharedLinkCoverPath(book), current->sharedLinkUrl, target, onDone);
        return;
    }

    // A small rendition is plenty for the grid tiles; fall back to the full
    // cover if the thumbnail request is refused.
    const QString fullPath = directCoverDropboxPath(current->dropboxPath, book);
    m_client->downloadThumbnailToFile(fullPath,
                                      QStringLiteral("w640h480"),
                                      target,
                                      [this, fullPath, target, onDone](const std::optional<DropboxFileMetadata> &metadata, const QString &error) {
                                          if (metadata) {
                                              onDone(metadata, error);
                                              return;
                                          }
                                          m_client->downloadToFile(fullPath, std::nullopt, target, onDone);
                                      });
}

QString LibraryController::directCoverDropboxPath(const QString &libraryPath, const Book &book) const
{
    return QStringLiteral("%1/%2/cover.jpg").arg(libraryPath, book.path);
}

QString LibraryController::sharedLinkCoverPath(const Book &book) const
{
    return QStringLiteral("/%1/cover.jpg").arg(book.path);
}

void LibraryController::loadOpdsLibrary(const Library &library)
{
    m_opdsClient->setCredentials(QUrl(library.opdsUrl), library.opdsUsername, QSettings().value(opdsPasswordKey(library.id)).toString());
    const QUrl root(library.opdsUrl);
    // The same server keeps what was learned about it between searches.
    if (m_opds->root() != root)
        m_opds->setRoot(root);
    m_opds->setBatchSize(opdsBatchSize());
    setStatus(QString());
    m_opds->load(m_searchQuery);
}

void LibraryController::loadMore()
{
    if (canLoadMore())
        m_opds->fetchMore();
}

void LibraryController::onOpdsBooksAdded(const QList<Book> &books)
{
    for (const Book &book : books)
        m_opdsBooks.insert(book.id, book);

    // Without server-side search the query can only narrow what has been loaded.
    const bool filterLocally = m_opds->searchIsLocalOnly() && !m_searchQuery.trimmed().isEmpty();
    const QList<Book> shown = filterLocally ? DatabaseHelper::filterBooks(books, m_searchQuery) : books;
    if (filterLocally)
        setStatus(QStringLiteral("This catalogue has no search; only the books loaded so far are filtered."));

    QHash<qlonglong, QString> covers;
    for (const Book &book : shown) {
        if (book.coverUrl.isEmpty())
            continue;
        const QString path = opdsCoverPath(book.coverUrl);
        if (QFileInfo::exists(path))
            covers.insert(book.id, QUrl::fromLocalFile(path).toString());
        else
            m_opdsCoverQueue.append(book.id);
    }
    m_model->appendBooks(shown, covers);
    pumpOpdsCovers();
}

QString LibraryController::opdsCoverPath(const QString &url) const
{
    const QDir cacheDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex());
    return cacheDir.filePath(QStringLiteral("covers/opds_%1.jpg").arg(hash));
}

void LibraryController::pumpOpdsCovers()
{
    while (!m_opdsCoverQueue.isEmpty() && m_opdsCoversPending < kMaxParallelOpdsCovers) {
        const qlonglong id = m_opdsCoverQueue.takeFirst();
        const auto it = m_opdsBooks.constFind(id);
        if (it == m_opdsBooks.constEnd())
            continue; // the list moved on
        const QString target = opdsCoverPath(it->coverUrl);
        ++m_opdsCoversPending;
        m_opdsClient->downloadFile(QUrl(it->coverUrl), target, [this, id, target](const Opds::FileResult &result) {
            --m_opdsCoversPending;
            // Missing covers are normal; the tile just keeps its placeholder.
            if (result.ok)
                m_model->setCover(id, QUrl::fromLocalFile(target).toString());
            pumpOpdsCovers();
        });
    }
}

void LibraryController::loadBookDetails(qlonglong bookId)
{
    const auto it = m_opdsBooks.constFind(bookId);
    if (it == m_opdsBooks.constEnd() || it->detailsUrl.isEmpty())
        return;
    m_detailsLoading = true;
    emit detailsLoadingChanged();
    m_opds->loadDetails(*it);
}

QVariantMap LibraryController::bookDetails(qlonglong bookId) const
{
    QVariantMap details;
    const auto it = m_opdsBooks.constFind(bookId);
    if (it == m_opdsBooks.constEnd())
        return details;
    details.insert(QStringLiteral("bookId"), it->id);
    details.insert(QStringLiteral("title"), it->title);
    details.insert(QStringLiteral("authors"), it->authors);
    details.insert(QStringLiteral("comment"), it->comment);
    details.insert(QStringLiteral("path"), it->path);
    details.insert(QStringLiteral("formats"), BookListModel::formatsToVariant(it->formats));
    return details;
}

std::optional<BookFormat> LibraryController::opdsFormat(qlonglong bookId, const QString &formatLabel) const
{
    const auto it = m_opdsBooks.constFind(bookId);
    if (it == m_opdsBooks.constEnd())
        return std::nullopt;
    for (const BookFormat &format : it->formats) {
        if (format.format == formatLabel)
            return format;
    }
    return std::nullopt;
}

QString LibraryController::opdsDisplayName(qlonglong bookId, const BookFormat &format) const
{
    int sameExtension = 0;
    const auto it = m_opdsBooks.constFind(bookId);
    if (it != m_opdsBooks.constEnd()) {
        for (const BookFormat &other : it->formats)
            sameExtension += other.extension == format.extension;
    }
    if (sameExtension > 1)
        return QStringLiteral("%1 (%2).%3").arg(format.fileName, fileSafe(format.format), format.extension);
    return QStringLiteral("%1.%2").arg(format.fileName, format.extension);
}

}
