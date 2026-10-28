#include "core/text/TextNormalization.h"

#include <QTest>

using namespace KalibreBox::TextNormalization;

class tst_textnormalization : public QObject
{
    Q_OBJECT

private slots:
    void lowercases();
    void stripsAccents();
    void keepsPlainStringsStable();
};

void tst_textnormalization::lowercases()
{
    QCOMPARE(normalizeForSearch(QStringLiteral("The HOBBIT")), QStringLiteral("the hobbit"));
}

void tst_textnormalization::stripsAccents()
{
    QCOMPARE(normalizeForSearch(QStringLiteral("José")), QStringLiteral("jose"));
    QCOMPARE(normalizeForSearch(QStringLiteral("Árbol")), QStringLiteral("arbol"));
    QCOMPARE(normalizeForSearch(QStringLiteral("Guulé")), QStringLiteral("guule"));
}

void tst_textnormalization::keepsPlainStringsStable()
{
    QCOMPARE(normalizeForSearch(QStringLiteral("hobbit 2")), QStringLiteral("hobbit 2"));
    QCOMPARE(normalizeForSearch(QString()), QString());
}

QTEST_MAIN(tst_textnormalization)
#include "tst_textnormalization.moc"
