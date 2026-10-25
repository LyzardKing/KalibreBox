#pragma once

#include "core/dropbox/DropboxClient.h"
#include "core/dropbox/DropboxTypes.h"

#include <QObject>
#include <QString>
#include <QStringList>

class QSettings;

namespace KalibreBox {

class DropboxClient;
class LibraryRepository;

/**
 * Small bridge exposing the core layer to QML for manual smoke testing.
 * The real UI controller will grow out of this in the UI step.
 */
class DevController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool signedIn READ signedIn NOTIFY signedInChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool awaitingAuthCode READ awaitingAuthCode NOTIFY awaitingAuthCodeChanged)

public:
    explicit DevController(DropboxClient *client, QObject *parent = nullptr);

    bool signedIn() const;
    QString status() const;
    bool awaitingAuthCode() const;

    Q_INVOKABLE void startSignIn();
    Q_INVOKABLE void signOut();

    /// Submits the authorization code the user copied from the browser page.
    Q_INVOKABLE void submitAuthCode(const QString &code);

    Q_INVOKABLE QStringList librarySummaries() const;
    Q_INVOKABLE void addLibrary(const QString &nameOrPath, const QString &name);
    Q_INVOKABLE void removeCurrentLibrary();
    Q_INVOKABLE QStringList bookTitles() const;

    /// Copies an existing local Calibre metadata.db into the current library's slot.
    Q_INVOKABLE bool importLocalMetadataDb(const QString &sourcePath);

    /// Dropbox calls against the current library (requires sign-in).
    Q_INVOKABLE void checkRemoteMetadata();
    Q_INVOKABLE void downloadMetadataDb();
    Q_INVOKABLE void browseLibraryFolder();
    Q_INVOKABLE void sendCurrentTestToKobo();

signals:
    void signedInChanged();
    void statusChanged();
    void awaitingAuthCodeChanged();
    void librariesChanged();
    void booksChanged();

private:
    LibraryRepository *m_repository = nullptr;
    DropboxClient *m_client = nullptr;
    QString m_status;
    bool m_awaitingAuthCode = false;};

}
