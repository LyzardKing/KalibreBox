#include "core/dropbox/DropboxUrls.h"

#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace KalibreBox {

namespace DropboxUrls {

QString extractPath(const QString &input)
{
    const QString trimmed = input.trimmed();

    // Already an absolute path.
    if (trimmed.startsWith(u'/'))
        return trimmed;

    // Not a URL: treat as a relative path.
    if (!isUrl(trimmed))
        return u'/' + trimmed;

    const QUrl url = QUrl(trimmed);
    if (!url.isValid() || url.host().isEmpty())
        return {};

    if (!url.host().endsWith(QStringLiteral("dropbox.com")))
        return {};

    const QString path = url.path();
    if (path.startsWith(QStringLiteral("/home/"))) {
        return path.mid(QStringLiteral("/home").size());
    }

    // Shared links need API resolution; the full URL is the marker.
    if (path.startsWith(QStringLiteral("/scl/")) || path.startsWith(QStringLiteral("/sh/")))
        return trimmed;

    // Any other Dropbox URL: use the path as-is when it points somewhere.
    if (!path.isEmpty() && path != QStringLiteral("/"))
        return path;
    return {};
}

bool isSharedLink(const QString &input)
{
    const QString trimmed = input.trimmed();
    if (!isUrl(trimmed))
        return false;

    const QUrl url = QUrl(trimmed);
    if (!url.isValid() || url.host().isEmpty() || !url.host().endsWith(QStringLiteral("dropbox.com")))
        return false;

    const QString path = url.path();
    return path.startsWith(QStringLiteral("/scl/")) || path.startsWith(QStringLiteral("/sh/"));
}

bool isUrl(const QString &input)
{
    const QString trimmed = input.trimmed();
    return trimmed.startsWith(QLatin1String("http://"))
        || trimmed.startsWith(QLatin1String("https://"));
}

bool isValidPath(const QString &path)
{
    const QString trimmed = path.trimmed();
    return trimmed.startsWith(u'/') && trimmed.size() > 1;
}

}
}
