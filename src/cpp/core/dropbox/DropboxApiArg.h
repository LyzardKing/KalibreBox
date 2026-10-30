#pragma once

#include <QJsonDocument>
#include <QString>

namespace KalibreBox
{

/**
 * The Dropbox-API-Arg header only allows ASCII: characters above 0x7E have
 * to be escaped as \uXXXX, otherwise requests carrying non-ASCII paths are
 * rejected. Escaping happens per UTF-16 code unit, which is precisely the
 * JSON escape form the endpoints expect (surrogate pairs included).
 */
inline QString asciiJsonArgument(const QJsonDocument &document)
{
    const QString text = QString::fromUtf8(document.toJson(QJsonDocument::Compact));
    QString escaped;
    escaped.reserve(text.size() + 8);
    for (QChar ch : text) {
        const uint code = ch.unicode();
        if (code <= 0x7E) {
            escaped.append(ch);
            continue;
        }
        escaped.append(QStringLiteral("\\u%1").arg(code, 4, 16, QLatin1Char('0')));
    }
    return escaped;
}

}
