#pragma once

#include "core/model/Book.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <functional>

namespace KalibreBox
{

/**
 * Reads the books of the active library for the QML grid.
 * Roles expose parsed values from Calibre's metadata.db; CoverRole either
 * returns a local file:// URL when the cover is already cached, or an empty
 * string and triggers the (asynchronous) cover download request once.
 */
class BookListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        AuthorsRole,
        CommentRole,
        HasCoverRole,
        PathRole,
        FormatsRole,
        CoverRole,
        NeedsDetailsRole
    };
    Q_ENUM(Role)

    explicit BookListModel(QObject *parent = nullptr);

    /// Ids of the books in rows [first, last], clamped to the list.
    QList<qlonglong> bookIdsInRange(int first, int last) const;
    void setBooks(const QList<Book> &books, const QHash<qlonglong, QString> &coverUrls);
    /// Adds books at the end without resetting the view (catalogue paging).
    void appendBooks(const QList<Book> &books, const QHash<qlonglong, QString> &coverUrls);
    /// Replaces the stored book with the same id; false if it is not listed.
    bool updateBook(const Book &book);

    static QVariantList formatsToVariant(const QList<BookFormat> &formats);

    /**
     * CoverRole invokes this once per book that has no cached cover yet,
     * so the owner (LibraryController) can start the download.
     */
    void setCoverRequestHandler(const std::function<void(qlonglong)> &handler);

    /// Marks a freshly downloaded cover; returns false if the book is gone.
    bool setCover(qlonglong bookId, const QString &coverUrl);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    QList<Book> m_books;
    QHash<qlonglong, QString> m_coverUrls;
    QHash<qlonglong, int> m_rowForId;
    mutable QSet<qlonglong> m_requestedCovers;
    std::function<void(qlonglong)> m_coverRequestHandler;
};

}
