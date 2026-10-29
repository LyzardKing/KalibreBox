#pragma once

#include <QString>
#include <optional>

class QSettings;

namespace KalibreBox
{

/**
 * OAuth credential with a refresh token (from token_access_type=offline).
 * Everything needed to obtain fresh access tokens without signing in again.
 */
struct DropboxCredential {
    QString accessToken;
    QString refreshToken;
    QString accountId;
    qint64 expiresAt = 0; ///< Unix epoch seconds when accessToken expires.

    [[nodiscard]] bool hasAccessToken() const
    {
        return !accessToken.isEmpty();
    }
    [[nodiscard]] bool hasRefreshToken() const
    {
        return !refreshToken.isEmpty();
    }

    /// True when a refresh is needed before `marginSeconds` from now.
    [[nodiscard]] bool needsRefresh(qint64 marginSeconds = 60) const;

    bool operator==(const DropboxCredential &other) const = default;

    // --- JSON round trip ---
    [[nodiscard]] QString toJson() const;
    static std::optional<DropboxCredential> fromJson(const QString &json);

    // --- Persistence (settings key mirrors the Android DropboxPrefs pilot) ---
    static void save(QSettings *settings, const DropboxCredential &credential);
    static std::optional<DropboxCredential> load(QSettings *settings);
    static void clear(QSettings *settings);
};

}
