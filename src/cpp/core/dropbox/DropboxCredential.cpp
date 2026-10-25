#include "core/dropbox/DropboxCredential.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace KalibreBox {

namespace {

const char *const prefsGroup = "DropboxPrefs";
const char *const credentialKey = "dropbox_credential";

bool fromJsonObject(const QJsonObject &object, DropboxCredential &credential)
{
    credential.accessToken = object.value(QStringLiteral("access_token")).toString();
    credential.refreshToken = object.value(QStringLiteral("refresh_token")).toString();
    credential.accountId = object.value(QStringLiteral("account_id")).toString();
    credential.expiresAt = static_cast<qint64>(
        object.value(QStringLiteral("expires_at")).toDouble(0.0));
    return credential.hasAccessToken() || credential.hasRefreshToken();
}

bool toJsonObject(const DropboxCredential &credential, QJsonObject *object)
{
    object->insert(QStringLiteral("access_token"), credential.accessToken);
    object->insert(QStringLiteral("refresh_token"), credential.refreshToken);
    object->insert(QStringLiteral("account_id"), credential.accountId);
    object->insert(QStringLiteral("expires_at"), static_cast<double>(credential.expiresAt));
    return true;
}

}

bool DropboxCredential::needsRefresh(qint64 marginSeconds) const
{
    if (!hasRefreshToken())
        return false;
    if (expiresAt == 0)
        return true;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    return now + marginSeconds >= expiresAt;
}

QString DropboxCredential::toJson() const
{
    QJsonObject object;
    toJsonObject(*this, &object);
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

std::optional<DropboxCredential> DropboxCredential::fromJson(const QString &json)
{
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    if (!document.isObject())
        return std::nullopt;
    DropboxCredential credential;
    if (!fromJsonObject(document.object(), credential))
        return std::nullopt;
    return credential;
}

void DropboxCredential::save(QSettings *settings, const DropboxCredential &credential)
{
    if (!settings)
        return;
    settings->beginGroup(QLatin1String(prefsGroup));
    settings->setValue(QLatin1String(credentialKey), credential.toJson());
    settings->endGroup();
}

std::optional<DropboxCredential> DropboxCredential::load(QSettings *settings)
{
    if (!settings)
        return std::nullopt;
    settings->beginGroup(QLatin1String(prefsGroup));
    const QString json = settings->value(QLatin1String(credentialKey)).toString();
    settings->endGroup();
    if (json.isEmpty())
        return std::nullopt;
    return DropboxCredential::fromJson(json);
}

void DropboxCredential::clear(QSettings *settings)
{
    if (!settings)
        return;
    settings->beginGroup(QLatin1String(prefsGroup));
    settings->remove(QLatin1String(credentialKey));
    settings->endGroup();
}

}
