#include "core/data/LibraryRepository.h"
#include "core/model/Library.h"

#include <QStandardPaths>
#include <QSettings>
#include <QTest>
#include <QTemporaryDir>

using namespace KalibreBox;

namespace {

QTemporaryDir tempDir;

namespace prefs {

QString uniquePrefPath()
{
    static int counter = 0;
    return tempDir.filePath(
        QStringLiteral("prefs_%1").arg(++counter));
}

LibraryRepository makeRepository(const QString &prefPath)
{
    return LibraryRepository(new QSettings(prefPath, QSettings::IniFormat));
}

} // namespace prefs

Library makeLibrary(const QString &id, const QString &name, const QString &path,
                    bool isDefault = false)
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
};

void tst_libraryrepository::startsEmpty()
{
    LibraryRepository repository = prefs::makeRepository(prefs::uniquePrefPath());
    QCOMPARE(repository.getLibraries().size(), 0);
    QVERIFY(!repository.getCurrentLibrary().has_value());
    QVERIFY(!repository.getDefaultLibrary().has_value());
}

void tst_libraryrepository::addLibraryPersistsAcrossInstances()
{
    const QString prefsPath = prefs::uniquePrefPath();
    {
        LibraryRepository repository = prefs::makeRepository(prefsPath);
        QVERIFY(repository.addLibrary(
            makeLibrary("aaa", "My Books", "/Books/Calibre")));
        QCOMPARE(repository.getLibraries().size(), 1);
    }
    {
        LibraryRepository repository = prefs::makeRepository(prefsPath);
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
    QCOMPARE(Library::displayNameFromPath(QStringLiteral("/My Books/Calibre")),
             QStringLiteral("Calibre"));
    QCOMPARE(Library::displayNameFromPath(QStringLiteral("/Calibre Library/")),
             QStringLiteral("Calibre Library"));
    QCOMPARE(Library::displayNameFromPath(QStringLiteral("/")),
             QStringLiteral("Library"));
    QCOMPARE(Library::displayNameFromPath(QStringLiteral("")),
             QStringLiteral("Library"));
}

QTEST_MAIN(tst_libraryrepository)
#include "tst_libraryrepository.moc"
