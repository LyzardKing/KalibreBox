#include "ui/BookListModel.h"

namespace KalibreBox
{

BookListModel::BookListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void BookListModel::setBooks(const QList<Book> &books, const QHash<qlonglong, QString> &coverUrls)
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

void BookListModel::appendBooks(const QList<Book> &books, const QHash<qlonglong, QString> &coverUrls)
{
    if (books.isEmpty())
        return;
    const int first = static_cast<int>(m_books.size());
    beginInsertRows({}, first, first + static_cast<int>(books.size()) - 1);
    for (const Book &book : books) {
        m_rowForId.insert(book.id, static_cast<int>(m_books.size()));
        m_books.append(book);
    }
    m_coverUrls.insert(coverUrls);
    endInsertRows();
}

bool BookListModel::updateBook(const Book &book)
{
    const auto it = m_rowForId.constFind(book.id);
    if (it == m_rowForId.constEnd())
        return false;
    m_books[it.value()] = book;
    const QModelIndex modelIndex = index(it.value());
    Q_EMIT dataChanged(modelIndex, modelIndex);
    return true;
}

QVariantList BookListModel::formatsToVariant(const QList<BookFormat> &formats)
{
    QVariantList result;
    result.reserve(formats.size());
    for (const BookFormat &format : formats) {
        QVariantMap entry;
        entry.insert(QStringLiteral("format"), format.format);
        entry.insert(QStringLiteral("fileName"), format.fileName);
        result.append(entry);
    }
    return result;
}

void BookListModel::setCoverRequestHandler(const std::function<void(qlonglong)> &handler)
{
    m_coverRequestHandler = handler;
}

bool BookListModel::setCover(qlonglong bookId, const QString &coverUrl)
{
    if (!m_rowForId.contains(bookId))
        return false;
    m_coverUrls.insert(bookId, coverUrl);
    const QModelIndex modelIndex = index(m_rowForId.value(bookId));
    Q_EMIT dataChanged(modelIndex, modelIndex, {CoverRole, HasCoverRole});
    return true;
}

QList<qlonglong> BookListModel::bookIdsInRange(int first, int last) const
{
    QList<qlonglong> ids;
    for (int row = qMax(0, first); row <= last && row < m_books.size(); ++row)
        ids.append(m_books.at(row).id);
    return ids;
}

int BookListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_books.size());
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
    case FormatsRole:
        return formatsToVariant(book.formats);
    case NeedsDetailsRole:
        return !book.detailsUrl.isEmpty();
    default:
        return {};
    }
}

QVariantMap BookListModel::bookSnapshot(qlonglong bookId) const
{
    QVariantMap snapshot;
    const auto it = m_rowForId.constFind(bookId);
    if (it == m_rowForId.constEnd())
        return snapshot;
    const Book &book = m_books.at(it.value());
    snapshot.insert(QStringLiteral("bookId"), book.id);
    snapshot.insert(QStringLiteral("title"), book.title);
    snapshot.insert(QStringLiteral("authors"), book.authors);
    snapshot.insert(QStringLiteral("comment"), book.comment);
    snapshot.insert(QStringLiteral("path"), book.path);
    snapshot.insert(QStringLiteral("formats"), formatsToVariant(book.formats));
    snapshot.insert(QStringLiteral("cover"), m_coverUrls.value(book.id));
    return snapshot;
}

bool BookListModel::bookNeedsDetails(qlonglong bookId) const
{
    const auto it = m_rowForId.constFind(bookId);
    return it != m_rowForId.constEnd() && !m_books.at(it.value()).detailsUrl.isEmpty();
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
    roles.insert(NeedsDetailsRole, QByteArrayLiteral("needsDetails"));
    return roles;
}

}
