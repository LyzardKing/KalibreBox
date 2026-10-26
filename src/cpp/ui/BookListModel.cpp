#include "ui/BookListModel.h"

#include <algorithm>

namespace KalibreBox {

BookListModel::BookListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void BookListModel::setBooks(const QList<Book> &books,
                             const QHash<qlonglong, QString> &coverUrls)
{
    beginResetModel();
    m_books = books;
    m_coverUrls = coverUrls;
    m_requestedCovers.clear();
    m_rowForId.clear();
    for (int row = 0; row < m_books.size(); ++row)
        m_rowForId.insert(m_books.at(row).id, row);
    endResetModel();
}

void BookListModel::setCoverRequestHandler(
    const std::function<void(qlonglong)> &handler)
{
    m_coverRequestHandler = handler;
}

bool BookListModel::setCover(qlonglong bookId, const QString &coverUrl)
{
    if (!m_rowForId.contains(bookId))
        return false;
    m_coverUrls.insert(bookId, coverUrl);
    const QModelIndex modelIndex = index(m_rowForId.value(bookId));
    Q_EMIT dataChanged(modelIndex, modelIndex, {CoverRole});
    return true;
}

int BookListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_books.size();
}

QVariant BookListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_books.size())
        return {};

    const Book &book = m_books.at(index.row());
    switch (role) {
    case IdRole:
        return book.id;
    case TitleRole:
        return book.title;
    case AuthorsRole:
        return book.authors;
    case CommentRole:
        return book.comment;
    case HasCoverRole:
        return book.hasCover && m_coverUrls.contains(book.id);
    case PathRole:
        return book.path;
    case CoverRole:
        return m_coverUrls.value(book.id);
    case FormatsRole: {
        QVariantList formats;
        formats.reserve(book.formats.size());
        for (const BookFormat &format : book.formats) {
            QVariantMap entry;
            entry.insert(QStringLiteral("format"), format.format);
            entry.insert(QStringLiteral("fileName"), format.fileName);
            formats.append(entry);
        }
        return formats;
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> BookListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles.insert(IdRole, QByteArrayLiteral("bookId"));
    roles.insert(TitleRole, QByteArrayLiteral("title"));
    roles.insert(AuthorsRole, QByteArrayLiteral("authors"));
    roles.insert(CommentRole, QByteArrayLiteral("comment"));
    roles.insert(HasCoverRole, QByteArrayLiteral("hasCover"));
    roles.insert(PathRole, QByteArrayLiteral("path"));
    roles.insert(FormatsRole, QByteArrayLiteral("formats"));
    roles.insert(CoverRole, QByteArrayLiteral("cover"));
    return roles;
}

}
