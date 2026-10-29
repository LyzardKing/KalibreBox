#pragma once

#include <QList>
#include <QMetaType>

namespace KalibreBox
{

struct BookFormat {
    /// Label shown to the user (unique within a book).
    QString format;
    QString fileName;
    /// OPDS only: where to download from, and the extension to save with.
    QString url;
    QString extension;

    bool operator==(const BookFormat &other) const = default;
};

struct Book {
    qlonglong id = 0;
    QString title;
    QString path;
    bool hasCover = false;
    QString comment;
    QString authors;
    QList<BookFormat> formats;
    /// OPDS only: remote cover, and the catalogue entry that still has to be
    /// opened to learn the formats (empty once the book is complete).
    QString coverUrl;
    QString detailsUrl;

    bool operator==(const Book &other) const = default;
};

}

Q_DECLARE_METATYPE(KalibreBox::BookFormat)
Q_DECLARE_METATYPE(KalibreBox::Book)
