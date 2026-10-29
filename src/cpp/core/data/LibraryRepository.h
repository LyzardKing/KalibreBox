#pragma once

#include "core/model/Library.h"

#include <QList>
#include <QString>
#include <optional>

class QSettings;

namespace KalibreBox
{

/**
 * Repository for managing Calibre libraries, persisted as JSON in settings
 * (mirrors the Android SharedPreferences layout).
 */
class LibraryRepository
{
public:
    explicit LibraryRepository(QSettings *settings);
    /// Convenience: uses the app-default QSettings.
    LibraryRepository();

    [[nodiscard]] QList<Library> getLibraries() const;
    bool addLibrary(const Library &library);
    bool removeLibrary(const QString &libraryId);
    bool updateLibrary(const Library &library);

    [[nodiscard]] std::optional<Library> getDefaultLibrary() const;
    [[nodiscard]] std::optional<Library> getLibraryById(const QString &id) const;

    void setCurrentLibraryId(const QString &libraryId);
    [[nodiscard]] QString getCurrentLibraryId() const;
    [[nodiscard]] std::optional<Library> getCurrentLibrary() const;

    static QString generateLibraryId();

private:
    void saveLibraries(const QList<Library> &libraries);

    QSettings *m_settings;
    bool m_ownsSettings = false;
};

}
