#include "core/model/Library.h"

#include <QString>

namespace KalibreBox {

QString Library::displayNameFromPath(const QString &path)
{
    const QString trimmed = path;
    QString name = trimmed;
    while (name.endsWith(u'/'))
        name.chop(1);
    const int slash = name.lastIndexOf(u'/');
    if (slash >= 0)
        name = name.mid(slash + 1);
    if (name.isEmpty())
        name = QStringLiteral("Library");
    return name;
}

}
