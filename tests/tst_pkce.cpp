#include "core/dropbox/Pkce.h"

#include <QCryptographicHash>
#include <QTest>

using namespace KalibreBox;

class tst_pkce : public QObject
{
    Q_OBJECT

private slots:
    void verifierMeetsRfc7636Shape();
    void challengeIsBase64UrlSha256();
    void pairsAreUnlinked();
};

void tst_pkce::verifierMeetsRfc7636Shape()
{
    const PkceCodes codes = Pkce::generate();
    // 32 random bytes -> 43 unpadded base64url characters.
    QCOMPARE(codes.verifier.size(), 43);
    for (const QChar c : codes.verifier) {
        QVERIFY2(c.isLetterOrNumber() || c == u'-' || c == u'_',
                 qPrintable(QStringLiteral("unexpected verifier character: %1").arg(c)));
    }
}

void tst_pkce::challengeIsBase64UrlSha256()
{
    const QString verifier = QStringLiteral("test-verifier-string-with-enough-length-01");
    const QString expectedChallenge = QString::fromUtf8(
        QCryptographicHash::hash(verifier.toUtf8(), QCryptographicHash::Sha256)
            .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));

    // No "=" padding allowed.
    QVERIFY(!expectedChallenge.contains(u'='));
    QCOMPARE(Pkce::challengeForVerifier(verifier), expectedChallenge);
}

void tst_pkce::pairsAreUnlinked()
{
    const PkceCodes first = Pkce::generate();
    const PkceCodes second = Pkce::generate();
    QVERIFY(first.verifier != second.verifier);
    QVERIFY(first.challenge != second.challenge);
}

QTEST_MAIN(tst_pkce)
#include "tst_pkce.moc"
