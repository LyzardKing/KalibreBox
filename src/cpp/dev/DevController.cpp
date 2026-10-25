#include "dev/DevController.h"

#include "core/data/LibraryRepository.h"
#include "core/db/DatabaseHelper.h"
#include "core/dropbox/DropboxAuthProvider.h"
#include "core/dropbox/DropboxUrls.h"

#include <QFile>
#include <QSettings>

namespace KalibreBox {

DevController::DevController(QSettings *settings, QObject *parent)
    : QObject(parent)
{
    m_repository = new LibraryRepository(settings);
    m_client = new DropboxClient(settings, this);

    if (const std::optional<DropboxCredential> saved = DropboxCredential::load(settings))
        m_client->setCredential(*saved);

    DropboxAuthProvider *auth = m_client->authProvider();
    connect(auth, &DropboxAuthProvider::authSucceeded, this, [this](const DropboxCredential &) {
        m_status = QStringLiteral("Signed in to Dropbox.");
        emit statusChanged();
        emit signedInChanged();
    });
    connect(auth, &DropboxAuthProvider::authFailed, this, [this](const QString &message) {
        m_status = QStringLiteral("Sign-in failed: %1").arg(message);
        emit statusChanged();
        emit signedInChanged();
    });
}

bool DevController::signedIn() const
{
    const std::optional<DropboxCredential> credential = m_client->credential();
    return credential && credential->hasAccessToken();
}

QString DevController::status() const
{
    return m_status;
}

void DevController::startSignIn()
{
    m_status = QStringLiteral("Waiting for browser sign-in …");
    emit statusChanged();
    m_client->authProvider()->startLogin();
}

void DevController::signOut()
{
    m_client->logout();
    m_status = QStringLiteral("Signed out.");
    emit statusChanged();
    emit signedInChanged();
}

QStringList DevController::librarySummaries() const
{
    QStringList summaries;
    const QList<Library> libraries = m_repository->getLibraries();
    const QString currentId = m_repository->getCurrentLibraryId();
    for (const Library &library : libraries) {
        summaries.append(QStringLiteral("%1%2 — %3")
                             .arg(library.isDefault ? QStringLiteral("* ") : QStringLiteral("  "),
                                  library.name,
                                  (library.id == currentId ?
                                       QStringLiteral("(current) %1").arg(library.dropboxPath)
                                     : library.dropboxPath)));
    }
    return summaries;
}

void DevController::addLibrary(const QString &nameOrPath, const QString &name)
{
    const QString path = DropboxUrls::extractPath(nameOrPath);
    if (path.isEmpty()) {
        m_status = QStringLiteral("Could not parse '%1' as a path or URL.").arg(nameOrPath);
        emit statusChanged();
        return;
    }

    Library library;
    library.id = LibraryRepository::generateLibraryId();
    library.dropboxPath = path;
    library.name = name.trimmed().isEmpty() ? Library::displayNameFromPath(path) : name.trimmed();
    if (DropboxUrls::isSharedLink(nameOrPath))
        library.sharedLinkUrl = nameOrPath.trimmed();

    if (m_repository->addLibrary(library)) {
        m_status = QStringLiteral("Added library '%1' at %2.").arg(library.name, path);
        emit librariesChanged();
    } else {
        m_status = QStringLiteral("A library with path %1 already exists.").arg(path);
    }
    emit statusChanged();
}

void DevController::removeCurrentLibrary()
{
    const QString currentId = m_repository->getCurrentLibraryId();
    if (currentId.isEmpty()) {
        m_status = QStringLiteral("No current library to remove.");
        emit statusChanged();
        return;
    }
    if (m_repository->removeLibrary(currentId)) {
        m_status = QStringLiteral("Removed library %1.").arg(currentId);
        emit librariesChanged();
    }
    emit statusChanged();
}

QStringList DevController::bookTitles() const
{
    const QString currentId = m_repository->getCurrentLibraryId();
    if (currentId.isEmpty())
        return {};

    QStringList titles;
    for (const Book &book : DatabaseHelper::getBooks(currentId))
        titles.append(QStringLiteral("%1 — %2").arg(book.title, book.authors));
    return titles;
}

bool DevController::importLocalMetadataDb(const QString &sourcePath)
{
    const QString currentId = m_repository->getCurrentLibraryId();
    if (currentId.isEmpty()) {
        m_status = QStringLiteral("Add a library first.");
        emit statusChanged();
        return false;
    }

    QFile source(sourcePath);
    if (!source.exists()) {
        m_status = QStringLiteral("No such file: %1").arg(sourcePath);
        emit statusChanged();
        return false;
    }
    DatabaseHelper::clearDatabase(currentId);
    const bool copied =
        QFile::copy(sourcePath, DatabaseHelper::databaseFilePath(currentId));
    m_status = copied
        ? QStringLiteral("Imported metadata.db for the current library.")
        : QStringLiteral("Could not copy %1").arg(sourcePath);
    emit statusChanged();
    emit booksChanged();
    return copied;
}

void DevController::checkRemoteMetadata()
{
    const std::optional<Library> current = m_repository->getCurrentLibrary();
    if (!current) {
        m_status = QStringLiteral("Add a library first.");
        emit statusChanged();
        return;
    }
    m_status = QStringLiteral("Checking %1/metadata.db …").arg(current->dropboxPath);
    emit statusChanged();

    m_client->getMetadata(
        current->dropboxPath + QStringLiteral("/metadata.db"),
        [this](std::optional<DropboxFileMetadata> metadata, const QString &error) {
            if (!metadata) {
                m_status = QStringLiteral("get_metadata failed: %1").arg(error);
            } else {
                m_status = QStringLiteral("Remote metadata.db: %1 bytes, modified epoch %2")
                               .arg(QString::number(metadata->size),
                                    QString::number(metadata->serverModifiedEpoch));
            }
            emit statusChanged();
        });
}

void DevController::downloadMetadataDb()
{
    const std::optional<Library> current = m_repository->getCurrentLibrary();
    if (!current) {
        m_status = QStringLiteral("Add a library first.");
        emit statusChanged();
        return;
    }
    const QString target = DatabaseHelper::databaseFilePath(current->id);
    m_status = QStringLiteral("Downloading metadata.db …");
    emit statusChanged();

    m_client->downloadToFile(
        current->dropboxPath + QStringLiteral("/metadata.db"),
        current->sharedLinkUrl, target,
        [this, current, target](std::optional<DropboxFileMetadata> metadata,
                                const QString &error) {
            if (!metadata) {
                m_status = QStringLiteral("Download failed: %1").arg(error);
            } else {
                DatabaseHelper::clearDatabase(current->id);
                DatabaseHelper::openLibrary(current->id);
                m_status = QStringLiteral("Downloaded metadata.db (%1 bytes) -> %2")
                               .arg(QString::number(metadata->size), target);
                emit booksChanged();
            }
            emit statusChanged();
        });
}

void DevController::browseLibraryFolder()
{
    const std::optional<Library> current = m_repository->getCurrentLibrary();
    if (!current) {
        m_status = QStringLiteral("Add a library first.");
        emit statusChanged();
        return;
    }
    m_status = QStringLiteral("Listing %1 …").arg(current->dropboxPath);
    emit statusChanged();

    m_client->listFolder(
        current->dropboxPath,
        [this](DropboxListFolderResult result, const QString &error) {
            if (!error.isEmpty()) {
                m_status = QStringLiteral("list_folder failed: %1").arg(error);
            } else {
                QStringList preview;
                for (const DropboxEntry &entry : result.entries.mid(0, 5)) {
                    preview.append(QStringLiteral("%1%2").arg(
                        entry.isFolder ? QStringLiteral("[D] ") : QStringLiteral("[F] "),
                        entry.name));
                }
                m_status = QStringLiteral(
                    "%1 entries (%2 more pages): %3 …")
                               .arg(result.entries.size())
                               .arg(result.hasMore ? QStringLiteral("yes") : QStringLiteral("no"))
                               .arg(preview.join(QStringLiteral(" | ")));
            }
            emit statusChanged();
        });
}

void DevController::sendCurrentTestToKobo()
{
    const std::optional<Library> current = m_repository->getCurrentLibrary();
    if (!current) {
        m_status = QStringLiteral("Add a library first.");
        emit statusChanged();
        return;
    }
    const QList<Book> books = DatabaseHelper::getBooks(current->id);
    if (books.isEmpty() || books.first().formats.isEmpty()) {
        m_status = QStringLiteral("No book with formats in the local database.");
        emit statusChanged();
        return;
    }
    const Book book = books.first();
    const BookFormat format = book.formats.first();
    const QString sourcePath = QStringLiteral("%1/%2/%3.%4")
                                   .arg(current->dropboxPath, book.path, format.fileName,
                                        format.format.toLower());
    m_status = QStringLiteral("Copying %1 to Kobo folder …").arg(sourcePath);
    emit statusChanged();

    m_client->copyFile(sourcePath,
                       QStringLiteral("/Apps/Rakuten Kobo/%1.%2")
                           .arg(format.fileName, format.format.toLower()),
                       [this](bool ok, const QString &error) {
                           m_status = ok ? QStringLiteral("Copied to /Apps/Rakuten Kobo.")
                                         : QStringLiteral("copy_v2 failed: %1").arg(error);
                           emit statusChanged();
                       });
}

}
