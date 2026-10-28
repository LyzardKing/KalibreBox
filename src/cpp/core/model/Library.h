#pragma once

#include <QMetaType>
#include <optional>

namespace KalibreBox
{

struct Library {
    QString id;
    QString name;
    QString dropboxPath;
    bool isDefault = false;
    std::optional<QString> sharedLinkUrl;

    bool operator==(const Library &other) const = default;

    static QString displayNameFromPath(const QString &path);
};

}

Q_DECLARE_METATYPE(KalibreBox::Library)
