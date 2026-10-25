#pragma once

#include <QString>

namespace KalibreBox {

/**
 * Utility for parsing Dropbox shared URLs and extracting folder paths.
 * Port of the Android DropboxUrlParser.
 */
namespace DropboxUrls {

/**
 * Extracts the Dropbox folder path from a shared URL, or returns the input
 * if it is already a path.
 *
 * Supported inputs:
 * - Direct paths: "/Calibre Library", "Calibre Library"
 * - New shared links: https://www.dropbox.com/scl/fo/xyz?rlkey=abc&dl=0
 * - Old shared links: https://www.dropbox.com/sh/xyz/abc
 * - Direct links: https://www.dropbox.com/home/Calibre%20Library
 *
 * Shared links cannot be resolved locally; the full URL is returned as a
 * marker for API resolution (see isSharedLink).
 */
QString extractPath(const QString &input);

/// True when the input is a Dropbox shared link requiring API resolution.
bool isSharedLink(const QString &input);

/// True when the input is an http(s) URL rather than a plain path.
bool isUrl(const QString &input);

/// Validates that a string is a usable absolute Dropbox path ("/x...", not "/").
bool isValidPath(const QString &path);

}
}
