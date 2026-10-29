#pragma once

#include "core/opds/OpdsFeed.h"

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <optional>

namespace KalibreBox::Opds
{

enum class FeedFormat : std::uint8_t {
    Unknown,
    /// OPDS 1.x, an Atom document.
    Atom,
    /// OPDS 2.0, a JSON document.
    Json,
};

/// Decides by Content-Type, sniffing the body when the header is missing or generic.
FeedFormat detectFormat(const QByteArray &body, const QString &contentType);

/// Parses either flavour. Relative links are resolved against `baseUrl`.
/// On failure returns nullopt and, if given, describes the problem in `error`.
std::optional<Feed> parseFeed(const QByteArray &body, const QUrl &baseUrl, const QString &contentType = {}, QString *error = nullptr);
std::optional<Feed> parseAtomFeed(const QByteArray &body, const QUrl &baseUrl, QString *error = nullptr);
std::optional<Feed> parseJsonFeed(const QByteArray &body, const QUrl &baseUrl, QString *error = nullptr);

/// Picks the OPDS/Atom (else OPDS 2.0 JSON) search template out of an OpenSearch
/// description document, ignoring its HTML and suggestion URLs.
std::optional<QString> parseOpenSearchDescription(const QByteArray &body, QString *error = nullptr);

/// Expands an OpenSearch (`{searchTerms}`) or RFC 6570 query (`{?query}`) template.
/// Variables other than the search terms are left out.
QUrl expandSearchTemplate(const QString &urlTemplate, const QString &searchTerms);

}
