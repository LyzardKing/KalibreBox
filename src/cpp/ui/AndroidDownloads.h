#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

namespace KalibreBox::AndroidDownloads
{

/**
 * Saves downloaded bytes into the device's public Downloads folder, like
 * the Kotlin app does: MediaStore.Downloads on Android 10+, the public
 * Downloads directory before that (which needs the storage permission).
 * Returns false and fills `error` on failure. Always fails off Android.
 */
bool saveToPublicDownloads(const QByteArray &data, const QString &displayName, QString *error);
bool retainFolderPermission(const QUrl &folder, QString *error);
bool saveToFolder(const QByteArray &data, const QUrl &folder, const QString &displayName, QString *error);

}
