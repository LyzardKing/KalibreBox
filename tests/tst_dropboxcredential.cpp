#include "core/dropbox/DropboxCredential.h"

#include <QSettings>
#include <QTest>
#include <QTemporaryDir>

using namespace KalibreBox;

class tst_dropboxcredential : public QObject
{
    Q_OBJECT

private slots:
    void emptyCredentialIsNotUsable();
    void needsRefreshUsesMargin();
    void jsonRoundTrip();
    void malformedJsonIsRejected();
    void persistsToSettings();
    void clearRemovesFromSettings();
};

DropboxCredential makeCredential()
{
    DropboxCredential credential;
    credential.accessToken = QStringLiteral("at.example");
    credential.refreshToken = QStringLiteral("rt.example");
    credential.accountId = QStringLiteral("dbid:xyz");
    credential.expiresAt = 1740000000;
    return credential;
}

void tst_dropboxcredential::emptyCredentialIsNotUsable()
{
    DropboxCredential credential;
    QVERIFY(!credential.hasAccessToken());
    QVERIFY(!credential.hasRefreshToken());
    QVERIFY(!credential.needsRefresh());
}

void tst_dropboxcredential::needsRefreshUsesMargin()
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    DropboxCredential credential = makeCredential();

    credential.expiresAt = now + 3600;
    QVERIFY(!credential.needsRefresh());

    // Within the 60 s refresh margin.
    credential.expiresAt = now + 30;
    QVERIFY(credential.needsRefresh());

    // Already expired.
    credential.expiresAt = now - 30;
    QVERIFY(credential.needsRefresh());

    // No refresh token means never refreshing.
    credential.refreshToken.clear();
    QVERIFY(!credential.needsRefresh());

    // Unknown expiry forces a refresh when a refresh token exists.
    credential.refreshToken = QStringLiteral("rt");
    credential.expiresAt = 0;
    QVERIFY(credential.needsRefresh());
}

void tst_dropboxcredential::jsonRoundTrip()
{
    const DropboxCredential credential = makeCredential();
    const QString json = credential.toJson();
    const std::optional<DropboxCredential> parsed = DropboxCredential::fromJson(json);
    QVERIFY(parsed.has_value());
    QCOMPARE(*parsed, credential);
    QVERIFY(json.contains(QLatin1String("\"access_token\":\"at.example\"")));
}

void tst_dropboxcredential::malformedJsonIsRejected()
{
    QVERIFY(!DropboxCredential::fromJson(QStringLiteral("not json")).has_value());
    QVERIFY(!DropboxCredential::fromJson(QStringLiteral("[1,2,3]")).has_value());
    QVERIFY(!DropboxCredential::fromJson(QStringLiteral("{}")).has_value());
}

void tst_dropboxcredential::persistsToSettings()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("prefs.ini"));

    {
        QSettings settings(path, QSettings::IniFormat);
        DropboxCredential::save(&settings, makeCredential());
    }
    {
        QSettings settings(path, QSettings::IniFormat);
        const std::optional<DropboxCredential> loaded = DropboxCredential::load(&settings);
        QVERIFY(loaded.has_value());
        QCOMPARE(*loaded, makeCredential());
    }
}

void tst_dropboxcredential::clearRemovesFromSettings()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("prefs.ini"));

    {
        QSettings settings(path, QSettings::IniFormat);
        DropboxCredential::save(&settings, makeCredential());
    }
    {
        QSettings settings(path, QSettings::IniFormat);
        DropboxCredential::clear(&settings);
        QVERIFY(!DropboxCredential::load(&settings).has_value());
    }
}

QTEST_MAIN(tst_dropboxcredential)
#include "tst_dropboxcredential.moc"
