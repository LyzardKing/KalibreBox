#pragma once

#include <QString>

namespace KalibreBox {

struct PkceCodes
{
    QString verifier;   ///< 43+ URL-safe chars of entropy (RFC 7636).
    QString challenge;  ///< BASE64url(SHA256(verifier)) without padding.

    bool operator==(const PkceCodes &other) const = default;
};

namespace Pkce {

/// Generates a fresh verifier + S256 challenge pair (verifier is 43 chars).
PkceCodes generate();

/// Computes the S256 challenge for a given verifier (test helper).
QString challengeForVerifier(const QString &verifier);

}

}
