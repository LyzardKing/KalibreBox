#include "core/dropbox/Pkce.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

namespace KalibreBox {

namespace {

QByteArray toBase64Url(const QByteArray &data)
{
    return data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

}

namespace Pkce {

PkceCodes generate()
{
    constexpr int verifierSeedBytes = 32; // -> 43 base64url chars
    QByteArray raw(verifierSeedBytes, Qt::Uninitialized);
    QRandomGenerator::system()->generate(raw.begin(), raw.end());

    PkceCodes codes;
    codes.verifier = QString::fromUtf8(toBase64Url(raw));
    codes.challenge = challengeForVerifier(codes.verifier);
    return codes;
}

QString challengeForVerifier(const QString &verifier)
{
    const QByteArray digest =
        QCryptographicHash::hash(verifier.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromUtf8(toBase64Url(digest));
}

}
}
