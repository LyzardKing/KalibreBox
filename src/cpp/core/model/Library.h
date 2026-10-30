#pragma once

#include <QMetaType>
#include <QString>
#include <optional>

namespace KalibreBox
{

enum class LibrarySourceType : std::uint8_t {
    Dropbox,
    Opds,
};

struct Library {
    QString id;
    QString name;
    QString dropboxPath;
    bool isDefault = false;
    std::optional<QString> sharedLinkUrl;
    LibrarySourceType sourceType = LibrarySourceType::Dropbox;
    /// Catalogue root of an OPDS library (empty for Dropbox libraries).
    QString opdsUrl;
    /// The password is kept outside the library JSON.
    QString opdsUsername;

    bool operator==(const Library &other) const = default;

    [[nodiscard]] bool isOpds() const
    {
        return sourceType == LibrarySourceType::Opds;
    }
    /// What identifies the library within its source, used to reject duplicates.
    [[nodiscard]] QString sourceKey() const
    {
        return isOpds() ? opdsUrl : dropboxPath;
    }

    static QString displayNameFromPath(const QString &path);
    static QString displayNameFromUrl(const QString &url);
};

}

Q_DECLARE_METATYPE(KalibreBox::Library)
