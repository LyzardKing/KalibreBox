#include "ui/AndroidFiles.h"

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QMimeDatabase>
#endif

namespace KalibreBox::AndroidFiles
{

#ifdef Q_OS_ANDROID

namespace
{

constexpr int kGrantReadUriPermission = 1;

QJniObject jstr(const QString &value)
{
    return QJniObject::fromString(value);
}

bool failed(QString *error, const QString &message)
{
    QJniEnvironment env;
    env.checkAndClearExceptions();
    if (error)
        *error = message;
    return false;
}

bool launch(const QString &path, bool share, QString *error)
{
    const QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid())
        return failed(error, QStringLiteral("No Android context."));

    const QString mimeType = QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchExtension).name();
    const QString authority = activity.callObjectMethod("getPackageName", "()Ljava/lang/String;").toString() + QStringLiteral(".fileprovider");

    const QJniObject file("java/io/File", "(Ljava/lang/String;)V", jstr(path).object<jstring>());
    const QJniObject uri = QJniObject::callStaticObjectMethod("androidx/core/content/FileProvider",
                                                              "getUriForFile",
                                                              "(Landroid/content/Context;Ljava/lang/String;Ljava/io/File;)Landroid/net/Uri;",
                                                              activity.object(),
                                                              jstr(authority).object<jstring>(),
                                                              file.object());
    if (!uri.isValid())
        return failed(error, QStringLiteral("Could not share the file with other apps."));

    const QString action = share ? QStringLiteral("android.intent.action.SEND") : QStringLiteral("android.intent.action.VIEW");
    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V", jstr(action).object<jstring>());
    if (share) {
        intent.callObjectMethod("setType", "(Ljava/lang/String;)Landroid/content/Intent;", jstr(mimeType).object<jstring>());
        intent.callObjectMethod("putExtra",
                                "(Ljava/lang/String;Landroid/os/Parcelable;)Landroid/content/Intent;",
                                jstr(QStringLiteral("android.intent.extra.STREAM")).object<jstring>(),
                                uri.object());
    } else {
        intent.callObjectMethod("setDataAndType", "(Landroid/net/Uri;Ljava/lang/String;)Landroid/content/Intent;", uri.object(), jstr(mimeType).object<jstring>());
    }
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", kGrantReadUriPermission);

    if (share) {
        intent = QJniObject::callStaticObjectMethod("android/content/Intent",
                                                    "createChooser",
                                                    "(Landroid/content/Intent;Ljava/lang/CharSequence;)Landroid/content/Intent;",
                                                    intent.object(),
                                                    jstr(QStringLiteral("Share")).object<jstring>());
        intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", kGrantReadUriPermission);
    }

    activity.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", intent.object());
    QJniEnvironment env;
    if (env.checkAndClearExceptions())
        return failed(error, share ? QStringLiteral("Could not open the share sheet.") : QStringLiteral("No app can open this file type."));
    return true;
}

}

bool openFile(const QString &path, QString *error)
{
    return launch(path, false, error);
}

bool shareFile(const QString &path, QString *error)
{
    return launch(path, true, error);
}

#else

bool openFile(const QString &, QString *error)
{
    if (error)
        *error = QStringLiteral("Only available on Android.");
    return false;
}

bool shareFile(const QString &, QString *error)
{
    return openFile(QString(), error);
}

#endif

}
