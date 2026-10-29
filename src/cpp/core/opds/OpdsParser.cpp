#include "core/opds/OpdsParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QXmlStreamReader>

namespace KalibreBox::Opds
{

namespace
{

const QString nsAtom = QStringLiteral("http://www.w3.org/2005/Atom");
const QString nsOpenSearchPrefix = QStringLiteral("http://a9.com/-/spec/opensearch/");
const QString nsDcPrefix = QStringLiteral("http://purl.org/dc/");

const QString relImage = QStringLiteral("http://opds-spec.org/image");
const QString relThumbnail = QStringLiteral("http://opds-spec.org/image/thumbnail");
const QString jsonCatalogType = QStringLiteral("application/opds+json");

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

QString resolveHref(const QUrl &base, const QString &href)
{
    QString trimmed = href.trimmed();
    if (trimmed.isEmpty())
        return {};

    // URI templates carry `{...}` that QUrl would percent-encode, so only the
    // part in front of the first variable is resolved.
    const qsizetype variable = trimmed.indexOf(u'{');
    if (variable >= 0) {
        const QString prefix = trimmed.left(variable);
        const QString resolved = prefix.isEmpty() ? base.toString(QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::FullyEncoded) : resolveHref(base, prefix);
        return resolved + trimmed.mid(variable);
    }

    const QUrl url(trimmed, QUrl::TolerantMode);
    // Absolute links (including inline data: URIs) are kept byte for byte.
    if (!url.isRelative() || base.isEmpty())
        return trimmed;
    return base.resolved(url).toString(QUrl::FullyEncoded);
}

QString normalizeText(const QString &text)
{
    QStringList lines;
    const QStringList rawLines = text.split(u'\n');
    for (const QString &line : rawLines) {
        const QString simplified = line.simplified();
        if (!simplified.isEmpty())
            lines.append(simplified);
    }
    return lines.join(u'\n');
}

bool isBlockElement(QStringView name)
{
    static const QStringList blocks = {
        QStringLiteral("div"),
        QStringLiteral("p"),
        QStringLiteral("br"),
        QStringLiteral("li"),
        QStringLiteral("ul"),
        QStringLiteral("ol"),
        QStringLiteral("h1"),
        QStringLiteral("h2"),
        QStringLiteral("h3"),
        QStringLiteral("h4"),
        QStringLiteral("tr"),
    };
    return blocks.contains(name);
}

QString elementText(QXmlStreamReader &reader)
{
    return reader.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
}

/// Reads a text/html/xhtml Atom text construct as plain text, one paragraph per line.
QString readAtomText(QXmlStreamReader &reader)
{
    const QString type = reader.attributes().value(QLatin1String("type")).toString();
    if (type != QLatin1String("xhtml")) {
        const QString raw = reader.readElementText(QXmlStreamReader::IncludeChildElements);
        if (type == QLatin1String("html"))
            return normalizeText(QTextDocumentFragment::fromHtml(raw).toPlainText());
        return normalizeText(raw);
    }

    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    QString text;
    int depth = 0;
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            ++depth;
            if (isBlockElement(reader.name()) && !text.endsWith(u'\n'))
                text += u'\n';
        } else if (token == QXmlStreamReader::Characters) {
            QString chunk = reader.text().toString();
            text += chunk.replace(whitespace, QStringLiteral(" "));
        } else if (token == QXmlStreamReader::EndElement) {
            if (depth == 0)
                break;
            --depth;
            if (isBlockElement(reader.name()) && !text.endsWith(u'\n'))
                text += u'\n';
        }
    }
    return normalizeText(text);
}

Link readAtomLink(QXmlStreamReader &reader, const QUrl &base)
{
    const QXmlStreamAttributes attributes = reader.attributes();
    Link link;
    link.rel = attributes.value(QLatin1String("rel")).toString();
    link.href = resolveHref(base, attributes.value(QLatin1String("href")).toString());
    link.type = attributes.value(QLatin1String("type")).toString();
    link.title = attributes.value(QLatin1String("title")).toString();
    link.templated = link.href.contains(u'{');
    bool ok = false;
    const qint64 length = attributes.value(QLatin1String("length")).toLongLong(&ok);
    if (ok)
        link.length = length;
    reader.skipCurrentElement();
    return link;
}

Entry readAtomEntry(QXmlStreamReader &reader, const QUrl &base)
{
    Entry entry;
    QString summary;
    QString content;
    QString published;
    while (reader.readNextStartElement()) {
        const QString ns = reader.namespaceUri().toString();
        const QString name = reader.name().toString();
        if (ns == nsAtom) {
            if (name == QLatin1String("id")) {
                entry.id = elementText(reader);
            } else if (name == QLatin1String("title")) {
                entry.title = readAtomText(reader);
            } else if (name == QLatin1String("summary")) {
                summary = readAtomText(reader);
            } else if (name == QLatin1String("content")) {
                content = readAtomText(reader);
            } else if (name == QLatin1String("updated")) {
                entry.updated = elementText(reader);
            } else if (name == QLatin1String("published")) {
                published = elementText(reader);
            } else if (name == QLatin1String("link")) {
                entry.links.append(readAtomLink(reader, base));
            } else if (name == QLatin1String("author")) {
                while (reader.readNextStartElement()) {
                    if (reader.name() == QLatin1String("name")) {
                        const QString author = elementText(reader);
                        if (!author.isEmpty())
                            entry.authors.append(author);
                    } else {
                        reader.skipCurrentElement();
                    }
                }
            } else if (name == QLatin1String("category")) {
                const QXmlStreamAttributes attributes = reader.attributes();
                QString category = attributes.value(QLatin1String("label")).toString();
                if (category.isEmpty())
                    category = attributes.value(QLatin1String("term")).toString();
                if (!category.isEmpty())
                    entry.categories.append(category);
                reader.skipCurrentElement();
            } else {
                reader.skipCurrentElement();
            }
        } else if (ns.startsWith(nsDcPrefix) && name == QLatin1String("language")) {
            entry.language = elementText(reader);
        } else {
            reader.skipCurrentElement();
        }
    }
    entry.summary = summary.isEmpty() ? content : summary;
    if (entry.updated.isEmpty())
        entry.updated = std::move(published);
    return entry;
}

QString xmlErrorText(const QXmlStreamReader &reader)
{
    return QStringLiteral("Invalid XML at line %1: %2").arg(reader.lineNumber()).arg(reader.errorString());
}

// ---- OPDS 2.0 (JSON) ----

QString localizedString(const QJsonValue &value)
{
    if (value.isString())
        return value.toString().trimmed();
    if (value.isObject()) {
        // Titles may be a language map, e.g. {"en": "Title"}.
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (it.value().isString())
                return it.value().toString().trimmed();
        }
    }
    return {};
}

QStringList nameList(const QJsonValue &value)
{
    QStringList names;
    const auto addOne = [&names](const QJsonValue &item) {
        const QString name = item.isObject() ? localizedString(item.toObject().value(QLatin1String("name"))) : localizedString(item);
        if (!name.isEmpty())
            names.append(name);
    };
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array)
            addOne(item);
    } else if (!value.isUndefined() && !value.isNull()) {
        addOne(value);
    }
    return names;
}

QList<Link> linksFromJson(const QJsonValue &value, const QUrl &base, const QString &defaultRel = {}, const QString &defaultType = {})
{
    QList<Link> links;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &item : array) {
        const QJsonObject object = item.toObject();
        Link link;
        link.href = resolveHref(base, object.value(QLatin1String("href")).toString());
        if (link.href.isEmpty())
            continue;
        link.type = object.value(QLatin1String("type")).toString();
        if (link.type.isEmpty())
            link.type = defaultType;
        link.title = object.value(QLatin1String("title")).toString();
        link.templated = object.value(QLatin1String("templated")).toBool() || link.href.contains(u'{');
        link.width = object.value(QLatin1String("width")).toInt();

        // `rel` is a string or a list; each relation becomes its own link.
        QStringList rels;
        const QJsonValue relValue = object.value(QLatin1String("rel"));
        if (relValue.isString()) {
            rels.append(relValue.toString());
        } else if (relValue.isArray()) {
            const QJsonArray relArray = relValue.toArray();
            for (const QJsonValue &rel : relArray)
                rels.append(rel.toString());
        }
        if (rels.isEmpty())
            rels.append(defaultRel);
        for (const QString &rel : std::as_const(rels)) {
            link.rel = rel;
            links.append(link);
        }
    }
    return links;
}

/// OPDS 2.0 lists cover sizes under `images` without relations; map the widest
/// to the cover and the smallest to the thumbnail.
QList<Link> imageLinksFromJson(const QJsonValue &value, const QUrl &base)
{
    QList<Link> images = linksFromJson(value, base, relImage);
    if (images.size() < 2)
        return images;

    int widest = 0;
    int smallest = 0;
    for (int i = 1; i < images.size(); ++i) {
        if (images.at(i).width > images.at(widest).width)
            widest = i;
        if (images.at(i).width > 0 && (images.at(smallest).width == 0 || images.at(i).width < images.at(smallest).width))
            smallest = i;
    }
    QList<Link> result;
    result.append(images.at(widest));
    if (smallest != widest && images.at(smallest).width > 0) {
        Link thumbnail = images.at(smallest);
        thumbnail.rel = relThumbnail;
        result.append(thumbnail);
    }
    return result;
}

Entry entryFromPublication(const QJsonObject &publication, const QUrl &base)
{
    Entry entry;
    const QJsonObject metadata = publication.value(QLatin1String("metadata")).toObject();
    entry.title = localizedString(metadata.value(QLatin1String("title")));
    entry.id = metadata.value(QLatin1String("identifier")).toString();
    entry.authors = nameList(metadata.value(QLatin1String("author")));
    entry.summary = metadata.value(QLatin1String("description")).toString().trimmed();
    entry.updated = metadata.value(QLatin1String("modified")).toString();
    if (entry.updated.isEmpty())
        entry.updated = metadata.value(QLatin1String("published")).toString();
    entry.categories = nameList(metadata.value(QLatin1String("subject")));

    const QJsonValue language = metadata.value(QLatin1String("language"));
    entry.language = language.isArray() ? language.toArray().first().toString() : language.toString();

    const QStringList series = nameList(metadata.value(QLatin1String("belongsTo")).toObject().value(QLatin1String("series")));
    if (!series.isEmpty())
        entry.series = series.first();

    entry.links = linksFromJson(publication.value(QLatin1String("links")), base);
    entry.links.append(imageLinksFromJson(publication.value(QLatin1String("images")), base));
    return entry;
}

Entry entryFromNavigation(const QJsonObject &item, const QUrl &base)
{
    Entry entry;
    entry.title = localizedString(item.value(QLatin1String("title")));
    QJsonArray single;
    single.append(item);
    entry.links = linksFromJson(single, base, QStringLiteral("subsection"), jsonCatalogType);
    if (!entry.links.isEmpty())
        entry.id = entry.links.first().href;
    return entry;
}

void appendEntries(const QJsonObject &object, const QUrl &base, QList<Entry> *entries)
{
    const QJsonArray navigation = object.value(QLatin1String("navigation")).toArray();
    for (const QJsonValue &item : navigation)
        entries->append(entryFromNavigation(item.toObject(), base));
    const QJsonArray publications = object.value(QLatin1String("publications")).toArray();
    for (const QJsonValue &item : publications)
        entries->append(entryFromPublication(item.toObject(), base));
}

}

FeedFormat detectFormat(const QByteArray &body, const QString &contentType)
{
    const QString type = contentType.toLower();
    if (type.contains(QLatin1String("json")))
        return FeedFormat::Json;
    if (type.contains(QLatin1String("xml")))
        return FeedFormat::Atom;

    qsizetype i = body.startsWith("\xEF\xBB\xBF") ? 3 : 0;
    while (i < body.size() && QChar::isSpace(static_cast<uchar>(body.at(i))))
        ++i;
    if (i < body.size()) {
        if (body.at(i) == '{')
            return FeedFormat::Json;
        if (body.at(i) == '<')
            return FeedFormat::Atom;
    }
    return FeedFormat::Unknown;
}

std::optional<Feed> parseFeed(const QByteArray &body, const QUrl &baseUrl, const QString &contentType, QString *error)
{
    switch (detectFormat(body, contentType)) {
    case FeedFormat::Atom:
        return parseAtomFeed(body, baseUrl, error);
    case FeedFormat::Json:
        return parseJsonFeed(body, baseUrl, error);
    case FeedFormat::Unknown:
        break;
    }
    setError(error, QStringLiteral("The server did not return an OPDS feed."));
    return std::nullopt;
}

std::optional<Feed> parseAtomFeed(const QByteArray &body, const QUrl &baseUrl, QString *error)
{
    QXmlStreamReader reader(body);
    if (!reader.readNextStartElement()) {
        setError(error, reader.hasError() ? xmlErrorText(reader) : QStringLiteral("The OPDS document is empty."));
        return std::nullopt;
    }

    const bool isAtom = reader.namespaceUri() == nsAtom;
    const QString root = reader.name().toString();
    if (!isAtom || (root != QLatin1String("feed") && root != QLatin1String("entry"))) {
        setError(error, QStringLiteral("Not an OPDS feed (root element <%1>).").arg(root));
        return std::nullopt;
    }

    Feed feed;
    // A lone <entry> is a complete acquisition entry document.
    if (root == QLatin1String("entry")) {
        feed.entries.append(readAtomEntry(reader, baseUrl));
    } else {
        while (reader.readNextStartElement()) {
            const QString ns = reader.namespaceUri().toString();
            const QString name = reader.name().toString();
            if (ns == nsAtom && name == QLatin1String("entry")) {
                feed.entries.append(readAtomEntry(reader, baseUrl));
            } else if (ns == nsAtom && name == QLatin1String("id")) {
                feed.id = elementText(reader);
            } else if (ns == nsAtom && name == QLatin1String("title")) {
                feed.title = readAtomText(reader);
            } else if (ns == nsAtom && name == QLatin1String("subtitle")) {
                feed.subtitle = readAtomText(reader);
            } else if (ns == nsAtom && name == QLatin1String("link")) {
                feed.links.append(readAtomLink(reader, baseUrl));
            } else if (ns.startsWith(nsOpenSearchPrefix)) {
                bool ok = false;
                const int number = elementText(reader).toInt(&ok);
                if (!ok)
                    continue;
                if (name == QLatin1String("totalResults"))
                    feed.totalResults = number;
                else if (name == QLatin1String("itemsPerPage"))
                    feed.itemsPerPage = number;
                else if (name == QLatin1String("startIndex"))
                    feed.startIndex = number;
            } else {
                reader.skipCurrentElement();
            }
        }
    }

    if (reader.hasError()) {
        setError(error, xmlErrorText(reader));
        return std::nullopt;
    }
    return feed;
}

std::optional<Feed> parseJsonFeed(const QByteArray &body, const QUrl &baseUrl, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (document.isNull()) {
        setError(error, QStringLiteral("Invalid JSON: %1").arg(parseError.errorString()));
        return std::nullopt;
    }
    if (!document.isObject()) {
        setError(error, QStringLiteral("Not an OPDS 2.0 feed (the root is not an object)."));
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    const QJsonObject metadata = root.value(QLatin1String("metadata")).toObject();
    const bool looksLikeFeed = root.contains(QLatin1String("metadata")) || root.contains(QLatin1String("publications"))
        || root.contains(QLatin1String("navigation")) || root.contains(QLatin1String("groups"));
    if (!looksLikeFeed) {
        setError(error, QStringLiteral("Not an OPDS 2.0 feed (no metadata, navigation or publications)."));
        return std::nullopt;
    }

    Feed feed;
    feed.title = localizedString(metadata.value(QLatin1String("title")));
    feed.subtitle = localizedString(metadata.value(QLatin1String("subtitle")));
    feed.id = metadata.value(QLatin1String("identifier")).toString();
    feed.totalResults = metadata.value(QLatin1String("numberOfItems")).toInt(-1);
    feed.itemsPerPage = metadata.value(QLatin1String("itemsPerPage")).toInt(-1);
    const int currentPage = metadata.value(QLatin1String("currentPage")).toInt(-1);
    if (currentPage >= 1 && feed.itemsPerPage > 0)
        feed.startIndex = (currentPage - 1) * feed.itemsPerPage + 1;

    feed.links = linksFromJson(root.value(QLatin1String("links")), baseUrl);
    appendEntries(root, baseUrl, &feed.entries);

    const QJsonArray groups = root.value(QLatin1String("groups")).toArray();
    for (const QJsonValue &item : groups) {
        const QJsonObject groupObject = item.toObject();
        Group group;
        group.title = localizedString(groupObject.value(QLatin1String("metadata")).toObject().value(QLatin1String("title")));
        const QList<Link> groupLinks = linksFromJson(groupObject.value(QLatin1String("links")), baseUrl);
        for (const Link &link : groupLinks) {
            if (link.rel == QLatin1String("self")) {
                group.moreLink = link;
                break;
            }
        }
        appendEntries(groupObject, baseUrl, &group.entries);
        feed.groups.append(group);
    }
    return feed;
}

std::optional<QString> parseOpenSearchDescription(const QByteArray &body, QString *error)
{
    QXmlStreamReader reader(body);
    if (!reader.readNextStartElement() || reader.name() != QLatin1String("OpenSearchDescription")) {
        setError(error, reader.hasError() ? xmlErrorText(reader) : QStringLiteral("Not an OpenSearch description document."));
        return std::nullopt;
    }

    QString atomTemplate;
    QString jsonTemplate;
    while (reader.readNextStartElement()) {
        if (reader.name() != QLatin1String("Url")) {
            reader.skipCurrentElement();
            continue;
        }
        const QString type = reader.attributes().value(QLatin1String("type")).toString();
        const QString urlTemplate = reader.attributes().value(QLatin1String("template")).toString();
        if (atomTemplate.isEmpty() && type.contains(QLatin1String("atom+xml")))
            atomTemplate = urlTemplate;
        else if (jsonTemplate.isEmpty() && type.contains(QLatin1String("opds+json")))
            jsonTemplate = urlTemplate;
        reader.skipCurrentElement();
    }
    if (reader.hasError()) {
        setError(error, xmlErrorText(reader));
        return std::nullopt;
    }

    QString chosen = atomTemplate.isEmpty() ? jsonTemplate : atomTemplate;
    if (chosen.isEmpty()) {
        setError(error, QStringLiteral("The search description offers no OPDS search URL."));
        return std::nullopt;
    }
    return chosen;
}

QUrl expandSearchTemplate(const QString &urlTemplate, const QString &searchTerms)
{
    static const QStringList termNames = {
        QStringLiteral("searchTerms"),
        QStringLiteral("query"),
        QStringLiteral("q"),
        QStringLiteral("search"),
    };
    const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(searchTerms));

    QString result;
    qsizetype position = 0;
    while (position < urlTemplate.size()) {
        const qsizetype open = urlTemplate.indexOf(u'{', position);
        const qsizetype close = open < 0 ? -1 : urlTemplate.indexOf(u'}', open);
        if (close < 0) {
            result += urlTemplate.mid(position);
            break;
        }
        result += urlTemplate.mid(position, open - position);
        QString expression = urlTemplate.mid(open + 1, close - open - 1);
        position = close + 1;

        QChar op;
        if (!expression.isEmpty() && (expression.at(0) == u'?' || expression.at(0) == u'&')) {
            op = expression.at(0);
            expression.remove(0, 1);
        }

        QStringList names;
        const QStringList rawNames = expression.split(u',', Qt::SkipEmptyParts);
        for (QString name : rawNames) {
            name = name.trimmed();
            // "?" marks an optional OpenSearch parameter, "*" an explode modifier.
            while (name.endsWith(u'?') || name.endsWith(u'*'))
                name.chop(1);
            const qsizetype colon = name.indexOf(u':');
            if (colon >= 0) {
                // "os:searchTerms" is a namespace prefix, "name:3" a length limit.
                const QString after = name.mid(colon + 1);
                name = (!after.isEmpty() && after.at(0).isDigit()) ? name.left(colon) : after;
            }
            names.append(name);
        }

        QString termsName;
        for (const QString &name : std::as_const(names)) {
            if (termNames.contains(name)) {
                termsName = name;
                break;
            }
        }
        if (termsName.isEmpty() && !op.isNull() && !names.isEmpty())
            termsName = names.first();
        if (termsName.isEmpty())
            continue;

        if (op.isNull()) {
            result += encoded;
        } else {
            const QChar separator = (op == u'?' && !result.contains(u'?')) ? u'?' : u'&';
            result += separator + termsName + u'=' + encoded;
        }
    }
    return QUrl::fromEncoded(result.toUtf8(), QUrl::TolerantMode);
}

}
