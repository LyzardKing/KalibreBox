#pragma once

#include <QList>
#include <QMetaType>

namespace KalibreBox {

struct BookFormat
{
    QString format;
    QString fileName;

    bool operator==(const BookFormat &other) const = default;
};

struct Book
{
    qlonglong id = 0;
    QString title;
    QString path;
    bool hasCover = false;
    QString comment;
    QString authors;
    QList<BookFormat> formats;

    bool operator==(const Book &other) const = default;
};

}

Q_DECLARE_METATYPE(KalibreBox::BookFormat)
Q_DECLARE_METATYPE(KalibreBox::Book)
