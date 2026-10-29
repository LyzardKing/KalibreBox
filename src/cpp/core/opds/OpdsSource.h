#pragma once

#include "core/model/Book.h"
#include "core/opds/OpdsFeed.h"
#include "core/opds/OpdsTransport.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QUrl>
#include <optional>

namespace KalibreBox::Opds
{

/**
 * Presents an OPDS catalogue as a growing list of books: the latest ones, or the
 * results of a server-side search, loaded a batch at a time.
 *
 * Nothing is kept on disk; without a connection there are simply no books.
 */
class OpdsSource : public QObject
{
    Q_OBJECT

public:
    explicit OpdsSource(OpdsTransport *transport, QObject *parent = nullptr);
    ~OpdsSource() override;

    /// Catalogue root. Changing it forgets everything learned about the server.
    void setRoot(const QUrl &url);
    [[nodiscard]] QUrl root() const
    {
        return m_root;
    }

    /// Books to gather per load step; a step may span several catalogue pages.
    void setBatchSize(int count);
    [[nodiscard]] int batchSize() const
    {
        return m_batchSize;
    }

    /// Starts over with the latest books, or with search results for `query`.
    void load(const QString &query);
    /// Loads the next batch of the current listing, if there is one.
    void fetchMore();
    /// Drops in-flight work for the current listing.
    void cancel();
    /// Fetches the formats of a book that was listed as a stub.
    void loadDetails(const Book &stub);

    [[nodiscard]] bool loading() const
    {
        return m_loading;
    }
    [[nodiscard]] bool canLoadMore() const
    {
        return !m_loading && !m_nextUrl.isEmpty();
    }
    /// The server offers no search, so the query only filters what was loaded.
    [[nodiscard]] bool searchIsLocalOnly() const
    {
        return m_localOnly;
    }
    [[nodiscard]] QString query() const
    {
        return m_query;
    }

    /// Turns one catalogue entry into a book (id 0), or nothing if it is not a book.
    static std::optional<Book> bookFromEntry(const Entry &entry, const QUrl &feedUrl);
    static QList<BookFormat> formatsFromLinks(const QList<Link> &links, const QString &title);
    static QString extensionFor(const Link &link);

signals:
    /// The list starts over; drop all books.
    void reset();
    void booksAdded(const QList<Book> &books);
    void loadingChanged();
    /// `offline` is true when the server could not be reached at all.
    void failed(const QString &message, bool offline);
    void detailsReady(const Book &book);
    void detailsFailed(qlonglong bookId, const QString &message);

private:
    static constexpr int maxPagesPerStep = 10;

    void setLoading(bool loading);
    void fail(const QString &message, bool offline);
    void discover(quint64 generation, const std::function<void()> &next);
    void startListing(quint64 generation);
    void resolveSearch(quint64 generation, const std::function<void()> &next);
    void fetchPage(quint64 generation, const QUrl &url, int remaining, int pagesLeft);
    void handlePage(quint64 generation, const FeedResult &result, int remaining, int pagesLeft);
    QList<Book> collectBooks(const Feed &feed, const QUrl &feedUrl);
    void cancelRequest();

    OpdsTransport *m_transport;
    QUrl m_root;
    int m_batchSize = 20;

    // What the root feed taught us.
    bool m_discovered = false;
    QUrl m_browseUrl;
    QString m_searchTemplate;
    QUrl m_searchDescriptionUrl;
    bool m_searchDescriptionTried = false;
    std::optional<Feed> m_rootFeed;
    QUrl m_rootFeedUrl;

    // The current listing.
    quint64 m_generation = 0;
    RequestId m_request = 0;
    QString m_query;
    QString m_nextUrl;
    QSet<QString> m_visited;
    QSet<QString> m_seen;
    qsizetype m_loadedCount = 0;
    bool m_loading = false;
    bool m_localOnly = false;
    qlonglong m_lastId = 0;

    QHash<qlonglong, RequestId> m_detailsRequests;
};

}
