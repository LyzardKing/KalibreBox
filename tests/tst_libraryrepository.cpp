#include "core/data/LibraryRepository.h"
#include "core/model/Library.h"

#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace KalibreBox;

namespace
{

QTemporaryDir tempDir;

namespace prefs
{

QString uniquePrefPath()
{
    static int counter = 0;
    return tempDir.filePath(QStringLiteral("prefs_%1").arg(++counter));
}

LibraryRepository makeRepository(const QString &prefPath)
{
    return LibraryRepository(new QSettings(prefPath, QSettings::IniFormat));
}

} // namespace prefs

Library makeLibrary(const QString &id, const QString &name, const QString &path, bool isDefault = false)
{
    Library library;
    library.id = id;
    library.name = name;
    library.dropboxPath = path;
    library.isDefault = isDefault;
    return library;
}

} // namespace

class tst_libraryrepository : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void startsEmpty();
    void addLibraryPersistsAcrossInstances();
    void addLibraryRejectsDuplicatePath();
    void firstLibraryBecomesDefaultAndCurrent();
    void removeLibraryReassignsDefaultAndCurrent();
    void updateLibraryModifiesInPlace();
    void getCurrentLibraryFallsBackToDefault();
    void displayNameFromPath();
    void opdsLibraryRoundTrips();
    void opdsLibrariesAreDistinguishedByUrl();
    void legacyJsonLoadsAsDropbox();
    void displayNameFromUrl();
};

void tst_libraryrepository::startsEmpty()
{
    LibraryRepository const repository = prefs::makeRepository(prefs::uniquePrefPath());
    QCOMPARE(repository.getLibraries().size(), 0);
    QVERIFY(!repository.getCurrentLibrary().has_value());
    QVERIFY(!repository.getDefaultLibrary().has_value());
}

void tst_libraryrepository::addLibraryPersistsAcrossInstances()
{
    const QString prefsPath = prefs::uniquePrefPath();
    {
        LibraryRepository repository = prefs::makeRepository(prefsPath);
        QVERIFY(repository.addLibrary(makeLibrary("aaa", "My Books", "/Books/Calibre")));
        QCOMPARE(repository.getLibraries().size(), 1);
    }
    {
        LibraryRepository const repository = prefs::makeRepository(prefsPath);
        QCOMPARE(repository.getLibraries().size(), 1);
        QCOMPARE(repository.getLibraries().first().dropboxPath, QStringLiteral("/Books/Calibre"));
    }
}

void tst_libraryrepository::addLibraryRejectsDuplicatePath()
{
    LibraryRepository repository = prefs::makeRepository(prefs::uniquePrefPath());
    const Library library = makeLibrary("bbb", "Calibre", "/Calibre Library");
    QVERIFY(repository.addLibrary(library));

    const Library duplicate = makeLibrary("ccc", "Calibre again", "/Calibre Library");
    QVERIFY(!repository.addLibrary(duplicate));
    QCOMPARE(repository.getLibraries().size(), 1);
}

void tst_libraryrepository::firstLibraryBecomesDefaultAndCurrent()
{
    LibraryRepository repository = prefs::makeRepository(prefs::uniquePrefPath());
    const Library library = makeLibrary("ddd", "First", "/First");
    QVERIFY(repository.addLibrary(library));

    QVERIFY(repository.getLibraries().first().isDefault);
    QCOMPARE(repository.getCurrentLibraryId(), QStringLiteral("ddd"));
    QCOMPARE(repository.getCurrentLibrary()->id, QStringLiteral("ddd"));
}

void tst_libraryrepository::removeLibraryReassignsDefaultAndCurrent()
{
    LibraryRepository repository = prefs::makeRepository(prefs::uniquePrefPath());
    QVERIFY(repository.addLibrary(makeLibrary("ddd", "First", "/First")));
    QVERIFY(repository.addLibrary(makeLibrary("eee", "Second", "/Second")));

    // The first library owns default/current; adding must not change them.
    QCOMPARE(repository.getCurrentLibraryId(), QStringLiteral("ddd"));

    QVERIFY(repository.removeLibrary(QStringLiteral("ddd")));
    QCOMPARE(repository.getLibraries().size(), 1);
    QCOMPARE(repository.getLibraries().first().isDefault, true);
    QCOMPARE(repository.getCurrentLibraryId(), QStringLiteral("eee"));

    QVERIFY(repository.removeLibrary(QStringLiteral("eee")));
    QVERIFY(repository.getLibraries().isEmpty());
    QCOMPARE(repository.getCurrentLibraryId(), QString());
}

void tst_libraryrepository::updateLibraryModifiesInPlace()
{
    LibraryRepository repository = prefs::makeRepository(prefs::uniquePrefPath());
    QVERIFY(repository.addLibrary(makeLibrary("fff", "Old name", "/Library path")));

    const Library updated = makeLibrary("fff", "New name", "/Library path", true);
    QVERIFY(repository.updateLibrary(updated));

    const std::optional<Library> persisted = repository.getLibraryById(QStringLiteral("fff"));
    QVERIFY(persisted.has_value());
    QCOMPARE(persisted->name, QStringLiteral("New name"));
    QCOMPARE(persisted->dropboxPath, QStringLiteral("/Library path"));

    const Library missing = makeLibrary("not-there", "Nobody", "/Nowhere");
    QVERIFY(!repository.updateLibrary(missing));
}

void tst_libraryrepository::opdsLibraryRoundTrips()
{
    const QString prefsPath = prefs::uniquePrefPath();
    Library library;
    library.id = QStringLiteral("opds-1");
    library.name = QStringLiteral("Gutenberg");
    library.sourceType = LibrarySourceType::Opds;
    library.opdsUrl = QStringLiteral("https://www.gutenberg.org/ebooks.opds/");
    library.opdsUsername = QStringLiteral("reader");
    {
        LibraryRepository repository = prefs::makeRepository(prefsPath);
        QVERIFY(repository.addLibrary(library));
    }
    LibraryRepository const repository = prefs::makeRepository(prefsPath);
    const std::optional<Library> loaded = repository.getLibraryById(QStringLiteral("opds-1"));
    QVERIFY(loaded.has_value());
    QVERIFY(loaded->isOpds());
    QCOMPARE(loaded->opdsUrl, library.opdsUrl);
    QCOMPARE(loaded->opdsUsername, QStringLiteral("reader"));
    QVERIFY(loaded->dropboxPath.isEmpty());
}

void tst_libraryrepository::opdsLibrariesAreDistinguishedByUrl()
{
    LibraryRepository repository = prefs::makeRepository(prefs::uniquePrefPath());
    const auto makeOpds = [](const QString &id, const QString &url) {
        Library library;
        library.id = id;
        library.name = id;
        library.sourceType = LibrarySourceType::Opds;
        library.opdsUrl = url;
        return library;
    };

    // Both have an empty dropboxPath, which must not count as a duplicate.
    QVERIFY(repository.addLibrary(makeOpds("a", QStringLiteral("https://one.example/opds"))));
    QVERIFY(repository.addLibrary(makeOpds("b", QStringLiteral("https://two.example/opds"))));
    QVERIFY(!repository.addLibrary(makeOpds("c", QStringLiteral("https://one.example/opds"))));
    QVERIFY(repository.addLibrary(makeLibrary("d", "Dropbox", "/One")));
    QCOMPARE(repository.getLibraries().size(), 3);
}

void tst_libraryrepository::legacyJsonLoadsAsDropbox()
{
    const QString prefsPath = prefs::uniquePrefPath();
    {
        QSettings settings(prefsPath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("libraries"),
                          QStringLiteral("[{\"id\":\"old\",\"name\":\"Old\",\"dropboxPath\":\"/Old\","
                                         "\"isDefault\":true}]"));
    }
    LibraryRepository const repository = prefs::makeRepository(prefsPath);
    const QList<Library> libraries = repository.getLibraries();
    QCOMPARE(libraries.size(), 1);
    QVERIFY(!libraries.first().isOpds());
    QCOMPARE(libraries.first().dropboxPath, QStringLiteral("/Old"));
}

void tst_libraryrepository::displayNameFromUrl()
{
    QCOMPARE(Library::displayNameFromUrl(QStringLiteral(" https://www.gutenberg.org/ebooks.opds/ ")), QStringLiteral("www.gutenberg.org"));
    QCOMPARE(Library::displayNameFromUrl(QStringLiteral("not a url")), QStringLiteral("Library"));
}

void tst_libraryrepository::getCurrentLibraryFallsBackToDefault()
{
    LibraryRepository repository = prefs::makeRepository(prefs::uniquePrefPath());
    QVERIFY(repository.addLibrary(makeLibrary("aaa", "One", "/One")));
    QVERIFY(repository.addLibrary(makeLibrary("bbb", "Two", "/Two")));

    repository.setCurrentLibraryId(QStringLiteral("zzz-not-here"));
    const std::optional<Library> fallback = repository.getCurrentLibrary();
    QVERIFY(fallback.has_value());
    QCOMPARE(fallback->id, QStringLiteral("aaa"));

    repository.setCurrentLibraryId(QStringLiteral("bbb"));
    QCOMPARE(repository.getCurrentLibrary()->id, QStringLiteral("bbb"));
}

void tst_libraryrepository::displayNameFromPath()
{
    QCOMPARE(Library::displayNameFromPath(QStringLiteral("/My Books/Calibre")), QStringLiteral("Calibre"));
    QCOMPARE(Library::displayNameFromPath(QStringLiteral("/Calibre Library/")), QStringLiteral("Calibre Library"));
    QCOMPARE(Library::displayNameFromPath(QStringLiteral("/")), QStringLiteral("Library"));
    QCOMPARE(Library::displayNameFromPath(QStringLiteral("")), QStringLiteral("Library"));
}

QTEST_MAIN(tst_libraryrepository)
#include "tst_libraryrepository.moc"
