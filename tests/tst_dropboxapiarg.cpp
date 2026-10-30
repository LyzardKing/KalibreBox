#include "core/dropbox/DropboxApiArg.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace KalibreBox;

class tst_dropboxapiarg : public QObject
{
    Q_OBJECT

private slots:
    void asciiPassesThrough();
    void nonAsciiGetsEscaped();
    void surrogatesAreEscapedPerUnit();
};

void tst_dropboxapiarg::asciiPassesThrough()
{
    QJsonObject body;
    body.insert(QStringLiteral("path"), QStringLiteral("/Calibre Library/Metadata.db"));
    const QString header = asciiJsonArgument(QJsonDocument(body));

    // Compact JSON, untouched: no escape sequence appeared anywhere.
    QCOMPARE(header, QStringLiteral(R"({"path":"/Calibre Library/Metadata.db"})"));
    for (const QChar ch : header)
        QVERIFY2(ch.unicode() <= 0x7E, qPrintable(QString::number(ushort(ch.unicode()), 16)));
}

void tst_dropboxapiarg::nonAsciiGetsEscaped()
{
    QJsonObject body;
    // é = U+00E9; the endpoints expect it verbatim as escape, not UTF-8 bytes.
    body.insert(QStringLiteral("é"), QStringLiteral("Libro español"));
    const QString header = asciiJsonArgument(QJsonDocument(body));

    QCOMPARE(header, QStringLiteral(R"({"\u00e9":"Libro espa\u00f1ol"})"));

    // And the escaped form parses back to the same JSON.
    const QJsonDocument roundTrip = QJsonDocument::fromJson(header.toUtf8());
    QCOMPARE(roundTrip.object().value(QStringLiteral("é")).toString(), QStringLiteral("Libro español"));
}

void tst_dropboxapiarg::surrogatesAreEscapedPerUnit()
{
    QJsonObject body;
    // 📚 = U+1F4DA, outside the BMP, so two \uXXXX escapes are required.
    body.insert(QStringLiteral("path"), QStringLiteral("/📚/metadata.db"));
    const QString header = asciiJsonArgument(QJsonDocument(body));

    QCOMPARE(header, QStringLiteral(R"({"path":"/\ud83d\udcda/metadata.db"})"));

    const QJsonDocument roundTrip = QJsonDocument::fromJson(header.toUtf8());
    QCOMPARE(roundTrip.object().value(QStringLiteral("path")).toString(), QStringLiteral("/📚/metadata.db"));
}

QTEST_MAIN(tst_dropboxapiarg)
#include "tst_dropboxapiarg.moc"
