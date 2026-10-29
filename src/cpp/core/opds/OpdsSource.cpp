#include "core/opds/OpdsSource.h"

#include "core/opds/OpdsParser.h"

#include <QRegularExpression>

#include <functional>

namespace KalibreBox::Opds
{

namespace
{

struct MimeExtension {
    const char *mime;
    const char *extension;
};

constexpr MimeExtension mimeExtensions[] = {
    {"application/epub+zip", "epub"},
    {"application/pdf", "pdf"},
    {"application/x-mobipocket-ebook", "mobi"},
    {"application/vnd.amazon.ebook", "azw3"},
    {"application/vnd.amazon.mobi8-ebook", "azw3"},
    {"application/x-fictionbook+xml", "fb2"},
    {"application/fb2+zip", "fb2.zip"},
    {"application/x-cbz", "cbz"},
    {"application/vnd.comicbook+zip", "cbz"},
    {"application/x-cbr", "cbr"},
    {"image/vnd.djvu", "djvu"},
    {"application/rtf", "rtf"},
    {"text/plain", "txt"},
    {"text/html", "html"},
    {"application/zip", "zip"},
};

QString safeFileName(const QString &title)
{
    static const QRegularExpression unsafe(QStringLiteral("[/\\\\:*?\"<>|\\x00-\\x1f]"));
    QString name = title;
    name.replace(unsafe, QStringLiteral("_"));
    name = name.simplified();
    // Leading dots would hide the file or climb out of the folder.
    while (name.startsWith(u'.'))
        name.remove(0, 1);
    if (name.size() > 100)
        name.truncate(100);
    return name.isEmpty() ? QStringLiteral("book") : name;
}

bool isRemote(const QString &href)
{
    return href.startsWith(QLatin1String("http://")) || href.startsWith(QLatin1String("https://"));
}

/// The smallest remote image of the entry; inline data: images are generic placeholders.
QString remoteCoverOf(const Entry &entry)
{
    for (const std::optional<Link> &link : {entry.thumbnailLink(), entry.coverLink()}) {
        if (link && isRemote(link->href))
            return link->href;
    }
    return {};
}

struct Stub {
    QString coverUrl;
};

/// Project Gutenberg lists books as stubs (a "subsection" link to /ebooks/<n>.opds)
/// and keeps the downloads on that page. Nothing in the feed marks them as books,
/// so they are recognised by the address.
std::optional<Stub> stubFor(const QUrl &href, const QUrl &feedUrl)
{
    if (href.host().toLower() != feedUrl.host().toLower())
        return std::nullopt;
    static const QRegularExpression pattern(QStringLiteral("^/ebooks/(\\d+)\\.opds$"));
    const QRegularExpressionMatch match = pattern.match(href.path());
    if (!match.hasMatch())
        return std::nullopt;

    Stub stub;
    if (href.host().toLower().endsWith(QLatin1String("gutenberg.org"))) {
        const QString number = match.captured(1);
        stub.coverUrl = QStringLiteral("%1://%2/cache/epub/%3/pg%3.cover.small.jpg").arg(href.scheme(), href.host(), number);
    }
    return stub;
}

QString uniqueLabel(const QString &label, const QList<BookFormat> &existing)
{
    const auto taken = [&existing](const QString &candidate) {
        for (const BookFormat &format : existing) {
            if (format.format == candidate)
                return true;
        }
        return false;
    };
    QString candidate = label;
    for (int n = 2; taken(candidate); ++n)
        candidate = QStringLiteral("%1 (%2)").arg(label).arg(n);
    return candidate;
}

}

OpdsSource::OpdsSource(OpdsTransport *transport, QObject *parent)
    : QObject(parent)
    , m_transport(transport)
{
}

OpdsSource::~OpdsSource()
{
    cancelRequest();
    for (const RequestId id : std::as_const(m_detailsRequests)) {
        if (id != 0)
            m_transport->cancel(id);
    }
}

void OpdsSource::setRoot(const QUrl &url)
{
    cancel();
    m_root = url;
    m_discovered = false;
    m_browseUrl.clear();
    m_searchTemplate.clear();
    m_searchDescriptionUrl.clear();
    m_searchDescriptionTried = false;
    m_rootFeed.reset();
}

void OpdsSource::setBatchSize(int count)
{
    m_batchSize = qMax(1, count);
}

void OpdsSource::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void OpdsSource::cancelRequest()
{
    if (m_request != 0)
        m_transport->cancel(m_request);
    m_request = 0;
}

void OpdsSource::cancel()
{
    ++m_generation;
    cancelRequest();
    setLoading(false);
}

void OpdsSource::fail(const QString &message, bool offline)
{
    setLoading(false);
    emit failed(message, offline);
}

void OpdsSource::load(const QString &query)
{
    ++m_generation;
    cancelRequest();
    // The root feed kept from discovery is only good for the load that fetched it.
    if (m_discovered)
        m_rootFeed.reset();
    m_query = query.trimmed();
    m_nextUrl.clear();
    m_visited.clear();
    m_seen.clear();
    m_loadedCount = 0;
    m_localOnly = false;

    const quint64 generation = m_generation;
    emit reset();
    if (generation != m_generation)
        return; // a slot started another load
    setLoading(true);
    discover(generation, [this, generation]() {
        startListing(generation);
    });
}

void OpdsSource::fetchMore()
{
    if (!canLoadMore())
        return;
    setLoading(true);
    fetchPage(m_generation, QUrl(m_nextUrl), m_batchSize, maxPagesPerStep);
}

void OpdsSource::discover(quint64 generation, const std::function<void()> &next)
{
    if (m_discovered) {
        next();
        return;
    }

    m_request = m_transport->fetchFeed(m_root, [this, generation, next](const FeedResult &result) {
        if (generation != m_generation)
            return;
        m_request = 0;
        if (!result.feed) {
            fail(result.error, result.networkError);
            return;
        }
        const Feed &feed = *result.feed;

        bool hasBooks = false;
        const auto scan = [&hasBooks](const QList<Entry> &entries) {
            for (const Entry &entry : entries)
                hasBooks = hasBooks || entry.hasAcquisition();
        };
        scan(feed.entries);
        for (const Group &group : feed.groups)
            scan(group.entries);

        if (hasBooks) {
            // A flat catalogue: the root already is the listing, so keep it for the first page.
            m_browseUrl = result.url;
            m_rootFeed = feed;
            m_rootFeedUrl = result.url;
        } else if (const std::optional<Link> newest = feed.newestLink()) {
            m_browseUrl = QUrl(newest->href);
        } else {
            m_browseUrl = result.url;
        }

        const SearchTarget search = feed.searchTarget();
        if (search.kind == SearchKind::Template)
            m_searchTemplate = search.href;
        else if (search.kind == SearchKind::Description)
            m_searchDescriptionUrl = QUrl(search.href);

        m_discovered = true;
        next();
    });
}

void OpdsSource::resolveSearch(quint64 generation, const std::function<void()> &next)
{
    if (!m_searchTemplate.isEmpty() || m_searchDescriptionUrl.isEmpty() || m_searchDescriptionTried) {
        next();
        return;
    }
    m_searchDescriptionTried = true;
    m_request = m_transport->fetchBytes(m_searchDescriptionUrl, [this, generation, next](const BytesResult &result) {
        if (generation != m_generation)
            return;
        m_request = 0;
        // A broken description only costs server-side search; browsing still works.
        if (result.ok) {
            if (const std::optional<QString> urlTemplate = parseOpenSearchDescription(result.body)) {
                if (QUrl(*urlTemplate, QUrl::TolerantMode).isRelative()) {
                    // Only the part before the first {variable} can be resolved as a URL.
                    const qsizetype brace = urlTemplate->indexOf(u'{');
                    const QString head = brace < 0 ? *urlTemplate : urlTemplate->left(brace);
                    m_searchTemplate =
                        m_searchDescriptionUrl.resolved(QUrl(head)).toString(QUrl::FullyEncoded) + (brace < 0 ? QString() : urlTemplate->mid(brace));
                } else {
                    m_searchTemplate = *urlTemplate;
                }
            }
        }
        next();
    });
}

void OpdsSource::startListing(quint64 generation)
{
    if (m_query.isEmpty()) {
        fetchPage(generation, m_browseUrl, m_batchSize, maxPagesPerStep);
        return;
    }
    resolveSearch(generation, [this, generation]() {
        if (m_searchTemplate.isEmpty()) {
            m_localOnly = true;
            fetchPage(generation, m_browseUrl, m_batchSize, maxPagesPerStep);
            return;
        }
        fetchPage(generation, expandSearchTemplate(m_searchTemplate, m_query), m_batchSize, maxPagesPerStep);
    });
}

void OpdsSource::fetchPage(quint64 generation, const QUrl &url, int remaining, int pagesLeft)
{
    const QString key = url.toString();
    if (m_visited.contains(key)) {
        // A catalogue that links back to itself must not loop forever.
        m_nextUrl.clear();
        setLoading(false);
        return;
    }
    m_visited.insert(key);

    if (m_rootFeed && url == m_rootFeedUrl) {
        FeedResult cached;
        cached.feed = std::exchange(m_rootFeed, std::nullopt);
        cached.url = m_rootFeedUrl;
        handlePage(generation, cached, remaining, pagesLeft);
        return;
    }

    m_request = m_transport->fetchFeed(url, [this, generation, remaining, pagesLeft](const FeedResult &result) {
        if (generation != m_generation)
            return;
        m_request = 0;
        handlePage(generation, result, remaining, pagesLeft);
    });
}

void OpdsSource::handlePage(quint64 generation, const FeedResult &result, int remaining, int pagesLeft)
{
    if (!result.feed) {
        fail(result.error, result.networkError);
        return;
    }

    const QList<Book> added = collectBooks(*result.feed, result.url);
    const std::optional<Link> next = result.feed->nextLink();
    m_nextUrl = next ? next->href : QString();
    m_loadedCount += added.size();
    if (!added.isEmpty()) {
        emit booksAdded(added);
        if (generation != m_generation)
            return;
    }

    const int left = remaining - static_cast<int>(added.size());
    if (left > 0 && !m_nextUrl.isEmpty() && pagesLeft > 1) {
        fetchPage(generation, QUrl(m_nextUrl), left, pagesLeft - 1);
        return;
    }

    setLoading(false);
    if (m_loadedCount == 0 && m_nextUrl.isEmpty()) {
        emit failed(m_query.isEmpty() ? QStringLiteral("This catalogue offers no list of books to browse.")
                                      : QStringLiteral("No results for \"%1\".").arg(m_query),
                    false);
    }
}

QList<Book> OpdsSource::collectBooks(const Feed &feed, const QUrl &feedUrl)
{
    QList<Entry> entries = feed.entries;
    for (const Group &group : feed.groups)
        entries.append(group.entries);

    QList<Book> books;
    for (const Entry &entry : std::as_const(entries)) {
        std::optional<Book> book = bookFromEntry(entry, feedUrl);
        if (!book)
            continue;
        const QString key = !book->detailsUrl.isEmpty() ? book->detailsUrl : !entry.id.isEmpty() ? entry.id : book->formats.first().url;
        if (m_seen.contains(key))
            continue;
        m_seen.insert(key);
        book->id = ++m_lastId;
        books.append(*book);
    }
    return books;
}

std::optional<Book> OpdsSource::bookFromEntry(const Entry &entry, const QUrl &feedUrl)
{
    if (entry.hasAcquisition()) {
        const QList<BookFormat> formats = formatsFromLinks(entry.downloadLinks(), entry.title);
        if (formats.isEmpty())
            return std::nullopt; // only for sale or on loan
        Book book;
        book.title = entry.title;
        book.authors = entry.authors.join(QStringLiteral(", "));
        book.comment = entry.summary;
        book.formats = formats;
        book.coverUrl = remoteCoverOf(entry);
        book.hasCover = !book.coverUrl.isEmpty();
        book.path = entry.id;
        return book;
    }

    const std::optional<Link> navigation = entry.navigationLink();
    if (!navigation)
        return std::nullopt;
    const std::optional<Stub> stub = stubFor(QUrl(navigation->href), feedUrl);
    if (!stub)
        return std::nullopt;

    Book book;
    book.title = entry.title;
    book.authors = entry.summary.section(u'\n', 0, 0);
    book.detailsUrl = navigation->href;
    book.coverUrl = stub->coverUrl.isEmpty() ? remoteCoverOf(entry) : stub->coverUrl;
    book.hasCover = !book.coverUrl.isEmpty();
    return book;
}

QString OpdsSource::extensionFor(const Link &link)
{
    const QString mime = link.type.section(u';', 0, 0).trimmed().toLower();
    for (const MimeExtension &entry : mimeExtensions) {
        if (mime == QLatin1String(entry.mime))
            return QString::fromLatin1(entry.extension);
    }
    // Generic types such as application/octet-stream: fall back to the file name.
    const QString name = QUrl(link.href).path().section(u'/', -1);
    const qsizetype dot = name.lastIndexOf(u'.');
    if (dot >= 0) {
        const QString suffix = name.mid(dot + 1).toLower();
        static const QRegularExpression plain(QStringLiteral("^[a-z0-9]{1,5}$"));
        if (plain.match(suffix).hasMatch())
            return suffix;
    }
    return QStringLiteral("bin");
}

QList<BookFormat> OpdsSource::formatsFromLinks(const QList<Link> &links, const QString &title)
{
    QList<BookFormat> formats;
    QSet<QString> urls;
    for (const Link &link : links) {
        if (!isRemote(link.href) || urls.contains(link.href))
            continue;
        urls.insert(link.href);

        BookFormat format;
        format.extension = extensionFor(link);
        const QString label = link.title.trimmed().isEmpty() ? format.extension.toUpper() : link.title.trimmed();
        format.format = uniqueLabel(label, formats);
        format.fileName = safeFileName(title);
        format.url = link.href;
        formats.append(format);
    }
    return formats;
}

void OpdsSource::loadDetails(const Book &stub)
{
    if (stub.detailsUrl.isEmpty() || m_detailsRequests.contains(stub.id))
        return;

    m_detailsRequests.insert(stub.id, 0);
    const RequestId request = m_transport->fetchFeed(QUrl(stub.detailsUrl), [this, stub](const FeedResult &result) {
        m_detailsRequests.remove(stub.id);
        if (!result.feed) {
            emit detailsFailed(stub.id, result.error);
            return;
        }

        Book book = stub;
        book.detailsUrl.clear();
        for (const Entry &entry : result.feed->entries) {
            if (!entry.hasAcquisition())
                continue;
            // A book may come as several editions (with or without images); offer them all.
            for (BookFormat format : formatsFromLinks(entry.downloadLinks(), stub.title)) {
                bool known = false;
                for (const BookFormat &existing : std::as_const(book.formats))
                    known = known || existing.url == format.url;
                if (known)
                    continue;
                format.format = uniqueLabel(format.format, book.formats);
                book.formats.append(format);
            }
            if (entry.summary.size() > book.comment.size())
                book.comment = entry.summary;
            if (book.authors.isEmpty())
                book.authors = entry.authors.join(QStringLiteral(", "));
            if (book.coverUrl.isEmpty()) {
                book.coverUrl = remoteCoverOf(entry);
                book.hasCover = !book.coverUrl.isEmpty();
            }
        }
        if (book.formats.isEmpty()) {
            emit detailsFailed(stub.id, QStringLiteral("This book has no downloadable files."));
            return;
        }
        emit detailsReady(book);
    });
    // A transport that answered immediately already removed the entry.
    if (m_detailsRequests.contains(stub.id))
        m_detailsRequests.insert(stub.id, request);
}

}
