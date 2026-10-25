#include "core/data/LibraryRepository.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUuid>

#include <algorithm>

namespace KalibreBox {

namespace {

const char *const keyLibraries = "libraries";
const char *const keyCurrentLibraryId = "current_library_id";

Library libraryFromJson(const QJsonObject &object)
{
    Library library;
    library.id = object.value(QStringLiteral("id")).toString();
    library.name = object.value(QStringLiteral("name")).toString();
    library.dropboxPath = object.value(QStringLiteral("dropboxPath")).toString();
    library.isDefault = object.value(QStringLiteral("isDefault")).toBool();
    if (!object.value(QStringLiteral("sharedLinkUrl")).isNull()
        && object.contains(QStringLiteral("sharedLinkUrl")))
        library.sharedLinkUrl = object.value(QStringLiteral("sharedLinkUrl")).toString();
    return library;
}

QJsonObject libraryToJson(const Library &library)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), library.id);
    object.insert(QStringLiteral("name"), library.name);
    object.insert(QStringLiteral("dropboxPath"), library.dropboxPath);
    object.insert(QStringLiteral("isDefault"), library.isDefault);
    if (library.sharedLinkUrl)
        object.insert(QStringLiteral("sharedLinkUrl"), *library.sharedLinkUrl);
    return object;
}

}

LibraryRepository::LibraryRepository(QSettings *settings)
    : m_settings(settings)
{
}

LibraryRepository::LibraryRepository()
    : m_settings(new QSettings()),
      m_ownsSettings(true)
{
}

QList<Library> LibraryRepository::getLibraries() const
{
    const QString json = m_settings->value(QLatin1String(keyLibraries)).toString();
    if (json.isEmpty())
        return {};

    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    if (!document.isArray())
        return {};

    QList<Library> libraries;
    for (const QJsonValue &value : document.array())
        libraries.append(libraryFromJson(value.toObject()));
    return libraries;
}

void LibraryRepository::saveLibraries(const QList<Library> &libraries)
{
    QJsonArray array;
    for (const Library &library : libraries)
        array.append(libraryToJson(library));

    m_settings->setValue(QLatin1String(keyLibraries),
                         QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

bool LibraryRepository::addLibrary(const Library &library)
{
    QList<Library> libraries = getLibraries();

    for (const Library &existing : libraries) {
        if (existing.dropboxPath == library.dropboxPath)
            return false;
    }

    Library libraryToAdd = library;
    if (libraries.isEmpty())
        libraryToAdd.isDefault = true;

    libraries.append(libraryToAdd);
    saveLibraries(libraries);

    if (libraries.size() == 1)
        setCurrentLibraryId(libraryToAdd.id);

    return true;
}

bool LibraryRepository::removeLibrary(const QString &libraryId)
{
    QList<Library> libraries = getLibraries();

    bool removed = false;
    for (int i = 0; i < libraries.size(); ++i) {
        if (libraries.at(i).id == libraryId) {
            libraries.removeAt(i);
            removed = true;
            --i;
        }
    }
    if (!removed)
        return false;

    if (!libraries.isEmpty() && std::none_of(libraries.cbegin(), libraries.cend(),
            [](const Library &library) { return library.isDefault; }))
        libraries[0].isDefault = true;

    saveLibraries(libraries);

    if (getCurrentLibraryId() == libraryId) {
        const std::optional<Library> defaultLibrary = getDefaultLibrary();
        // No replacement available: clear the selection rather than leave a
        // stale id pointing at a removed library.
        setCurrentLibraryId(defaultLibrary ? defaultLibrary->id : QString());
    }
    return true;
}

bool LibraryRepository::updateLibrary(const Library &library)
{
    QList<Library> libraries = getLibraries();

    for (Library &existing : libraries) {
        if (existing.id == library.id) {
            existing = library;
            saveLibraries(libraries);
            return true;
        }
    }
    return false;
}

std::optional<Library> LibraryRepository::getDefaultLibrary() const
{
    const QList<Library> libraries = getLibraries();
    for (const Library &library : libraries) {
        if (library.isDefault)
            return library;
    }
    if (!libraries.isEmpty())
        return libraries.first();
    return std::nullopt;
}

std::optional<Library> LibraryRepository::getLibraryById(const QString &id) const
{
    for (const Library &library : getLibraries()) {
        if (library.id == id)
            return library;
    }
    return std::nullopt;
}

void LibraryRepository::setCurrentLibraryId(const QString &libraryId)
{
    if (libraryId.isEmpty())
        m_settings->remove(QLatin1String(keyCurrentLibraryId));
    else
        m_settings->setValue(QLatin1String(keyCurrentLibraryId), libraryId);
}

QString LibraryRepository::getCurrentLibraryId() const
{
    return m_settings->value(QLatin1String(keyCurrentLibraryId)).toString();
}

std::optional<Library> LibraryRepository::getCurrentLibrary() const
{
    const QString currentId = getCurrentLibraryId();
    if (!currentId.isEmpty()) {
        if (const std::optional<Library> byId = getLibraryById(currentId))
            return byId;
    }
    return getDefaultLibrary();
}

QString LibraryRepository::generateLibraryId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

}
