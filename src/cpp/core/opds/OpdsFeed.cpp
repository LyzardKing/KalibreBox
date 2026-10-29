#include "core/opds/OpdsFeed.h"

namespace KalibreBox::Opds
{

namespace
{

const QString relAcquisition = QStringLiteral("http://opds-spec.org/acquisition");
const QString relOpenAccess = QStringLiteral("http://opds-spec.org/acquisition/open-access");
const QString relSortNew = QStringLiteral("http://opds-spec.org/sort/new");

bool isCoverRel(const QString &rel)
{
    // The 0.9 draft used "cover"/"thumbnail"; Calibre still emits those.
    return rel == QLatin1String("http://opds-spec.org/image") || rel == QLatin1String("http://opds-spec.org/cover");
}

bool isThumbnailRel(const QString &rel)
{
    return rel == QLatin1String("http://opds-spec.org/image/thumbnail") || rel == QLatin1String("http://opds-spec.org/thumbnail");
}

bool isCatalogType(const QString &type)
{
    return type.contains(QLatin1String("opds-catalog"), Qt::CaseInsensitive) || type.contains(QLatin1String("opds+json"), Qt::CaseInsensitive)
        || type.contains(QLatin1String("atom+xml"), Qt::CaseInsensitive);
}

bool isNonNavigationRel(const QString &rel)
{
    static const QStringList excluded = {
        QStringLiteral("related"),
        QStringLiteral("self"),
        QStringLiteral("alternate"),
        QStringLiteral("start"),
        QStringLiteral("up"),
        QStringLiteral("search"),
        QStringLiteral("next"),
        QStringLiteral("previous"),
        QStringLiteral("first"),
        QStringLiteral("last"),
    };
    return excluded.contains(rel);
}

std::optional<Link> pickImage(const QList<Link> &links, bool (*matches)(const QString &))
{
    std::optional<Link> best;
    for (const Link &link : links) {
        if (!matches(link.rel))
            continue;
        // OPDS 2.0 may list several sizes; prefer the widest.
        if (!best || link.width > best->width)
            best = link;
    }
    return best;
}

}

QList<Link> Entry::downloadLinks() const
{
    QList<Link> result;
    for (const Link &link : links) {
        if (link.rel == relAcquisition || link.rel == relOpenAccess)
            result.append(link);
    }
    return result;
}

bool Entry::hasAcquisition() const
{
    for (const Link &link : links) {
        if (link.rel.startsWith(relAcquisition))
            return true;
    }
    return false;
}

std::optional<Link> Entry::coverLink() const
{
    return pickImage(links, isCoverRel);
}

std::optional<Link> Entry::thumbnailLink() const
{
    // For several sizes the smallest is the best thumbnail.
    std::optional<Link> best;
    for (const Link &link : links) {
        if (!isThumbnailRel(link.rel))
            continue;
        if (!best || (link.width > 0 && (best->width == 0 || link.width < best->width)))
            best = link;
    }
    return best ? best : coverLink();
}

std::optional<Link> Entry::navigationLink() const
{
    for (const Link &link : links) {
        if (link.rel.startsWith(relAcquisition) || isCoverRel(link.rel) || isThumbnailRel(link.rel) || isNonNavigationRel(link.rel))
            continue;
        if (isCatalogType(link.type))
            return link;
    }
    return std::nullopt;
}

std::optional<Link> Feed::findLink(const QString &rel) const
{
    for (const Link &link : links) {
        if (link.rel == rel)
            return link;
    }
    return std::nullopt;
}

std::optional<Link> Feed::newestLink() const
{
    if (const std::optional<Link> direct = findLink(relSortNew))
        return direct;

    QList<const Entry *> candidates;
    for (const Entry &entry : entries)
        candidates.append(&entry);
    for (const Group &group : groups) {
        for (const Entry &entry : group.entries)
            candidates.append(&entry);
    }

    // The spec relation normally sits on a navigation entry.
    for (const Entry *entry : std::as_const(candidates)) {
        for (const Link &link : entry->links) {
            if (link.rel == relSortNew)
                return link;
        }
    }

    // Many catalogues (Project Gutenberg among them) skip the relation and just
    // title the entry, so fall back to a few English names.
    static const QStringList newestTitles = {
        QStringLiteral("newest"),
        QStringLiteral("latest"),
        QStringLiteral("new"),
        QStringLiteral("recent"),
        QStringLiteral("recently added"),
        QStringLiteral("new releases"),
        QStringLiteral("latest releases"),
        QStringLiteral("new books"),
    };
    for (const Entry *entry : std::as_const(candidates)) {
        if (entry->hasAcquisition() || !newestTitles.contains(entry->title.trimmed().toLower()))
            continue;
        if (const std::optional<Link> link = entry->navigationLink())
            return link;
    }
    return std::nullopt;
}

SearchTarget Feed::searchTarget() const
{
    SearchTarget description;
    for (const Link &link : links) {
        if (link.rel != QLatin1String("search"))
            continue;
        if (link.type.contains(QLatin1String("opensearchdescription"), Qt::CaseInsensitive)) {
            if (description.kind == SearchKind::None)
                description = {SearchKind::Description, link.href};
            continue;
        }
        if (link.templated || link.href.contains(u'{'))
            return {SearchKind::Template, link.href};
    }
    return description;
}

}
