#pragma once

class QString;

namespace KalibreBox::TextNormalization
{

/**
 * Normalizes a string for case- and accent-insensitive searching.
 * e.g. "José" becomes "jose".
 */
QString normalizeForSearch(const QString &input);

}
