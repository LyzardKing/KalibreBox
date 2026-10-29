#pragma once

#include <QList>
#include <QString>

namespace KalibreBox
{

/// A file or folder entry from files/list_folder.
struct DropboxEntry {
    QString name;
    QString pathLower;
    bool isFolder = false;
    qint64 size = 0;
    QString id;

    [[nodiscard]] bool isFile() const
    {
        return !isFolder;
    }
    bool operator==(const DropboxEntry &other) const = default;
};

/// Result of files/list_folder (single page, parity with the Android client).
struct DropboxListFolderResult {
    QList<DropboxEntry> entries;
    QString cursor;
    bool hasMore = false;
};

/// File metadata (files/get_metadata, Dropbox-API-Result of downloads, ...).
struct DropboxFileMetadata {
    QString name;
    QString pathLower;
    QString id;
    QString contentHash;
    qint64 size = 0;
    qint64 serverModifiedEpoch = 0; ///< Unix epoch seconds (0 when unavailable).
    [[nodiscard]] bool isFile() const
    {
        return size >= 0;
    }
    bool operator==(const DropboxFileMetadata &other) const = default;
};

}
