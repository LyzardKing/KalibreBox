#include "core/text/TextNormalization.h"

#include <QString>

namespace KalibreBox::TextNormalization
{

QString normalizeForSearch(const QString &input)
{
    // 1. Decompose into base character + combining marks ("é" -> "e" + "´").
    const QString decomposed = input.normalized(QString::NormalizationForm_D);
    // 2. Drop the combining marks.
    QString result;
    result.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() != QChar::Mark_NonSpacing)
            result.append(c);
    }
    // 3. Lowercase for case-insensitive matching.
    return result.toLower();
}

}
