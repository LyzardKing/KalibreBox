#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace KalibreBox::Opds
{

/// One link of a feed or entry. `href` is absolute (resolved against the feed URL).
struct Link {
    QString rel;
    QString href;
    QString type;
    QString title;
    /// OPDS 2.0 `templated` links and OpenSearch templates contain `{...}` variables.
    bool templated = false;
    qint64 length = -1;
    /// Pixel width for OPDS 2.0 images, 0 when unknown.
    int width = 0;

    bool operator==(const Link &other) const = default;
};

/// A publication, or a navigation item pointing at another feed.
struct Entry {
    QString id;
    QString title;
    QString summary;
    QStringList authors;
    QStringList categories;
    QString language;
    QString updated;
    QString series;
    QList<Link> links;

    /// Links a client can download directly (plain and open-access acquisition).
    [[nodiscard]] QList<Link> downloadLinks() const;
    /// True when the entry carries any acquisition relation, including borrow/buy.
    [[nodiscard]] bool hasAcquisition() const;
    [[nodiscard]] std::optional<Link> coverLink() const;
    /// Falls back to the cover when no thumbnail is advertised.
    [[nodiscard]] std::optional<Link> thumbnailLink() const;
    /// The catalogue a navigation entry leads to, if any.
    [[nodiscard]] std::optional<Link> navigationLink() const;
    /// True for an entry that links to a catalogue and offers nothing to download.
    [[nodiscard]] bool isNavigation() const
    {
        return !hasAcquisition() && navigationLink().has_value();
    }
};

/// A titled shelf of an OPDS 2.0 feed.
struct Group {
    QString title;
    std::optional<Link> moreLink;
    QList<Entry> entries;
};

enum class SearchKind : std::uint8_t {
    None,
    /// The link is already a template to expand with the search terms.
    Template,
    /// The link points at an OpenSearch description document to fetch first.
    Description,
};

struct SearchTarget {
    SearchKind kind = SearchKind::None;
    QString href;
};

struct Feed {
    QString id;
    QString title;
    QString subtitle;
    QList<Link> links;
    QList<Entry> entries;
    QList<Group> groups;
    int totalResults = -1;
    int itemsPerPage = -1;
    /// 1-based index of the first entry on this page, -1 when unknown.
    int startIndex = -1;

    [[nodiscard]] std::optional<Link> findLink(const QString &rel) const;
    [[nodiscard]] std::optional<Link> nextLink() const
    {
        return findLink(QStringLiteral("next"));
    }
    [[nodiscard]] std::optional<Link> selfLink() const
    {
        return findLink(QStringLiteral("self"));
    }
    [[nodiscard]] std::optional<Link> startLink() const
    {
        return findLink(QStringLiteral("start"));
    }
    /// The feed listing the newest publications: the OPDS sort relation, else an entry
    /// titled like "Latest"/"Newest".
    [[nodiscard]] std::optional<Link> newestLink() const;
    [[nodiscard]] SearchTarget searchTarget() const;
};

}
