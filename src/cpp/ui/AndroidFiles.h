#pragma once

#include <QString>

namespace KalibreBox::AndroidFiles
{

/**
 * Opens a local file in another app (ACTION_VIEW) through the app's
 * FileProvider, since Android refuses file:// URIs. Returns false and fills
 * `error` on failure. Always fails off Android.
 */
bool openFile(const QString &path, QString *error);

/// Offers a local file to the system share sheet (ACTION_SEND).
bool shareFile(const QString &path, QString *error);

}
