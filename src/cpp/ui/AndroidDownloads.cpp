#include "ui/AndroidDownloads.h"

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QMimeDatabase>
#endif

namespace KalibreBox::AndroidDownloads
{

#ifdef Q_OS_ANDROID

namespace
{

constexpr int kAndroidQ = 29;

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

// API < 29 has no scoped storage: write into the public Downloads folder,
// which needs WRITE_EXTERNAL_STORAGE granted at runtime.
QJniObject openLegacyStream(const QJniObject &activity, const QString &displayName, QString *error)
{
    const QString permission = QStringLiteral("android.permission.WRITE_EXTERNAL_STORAGE");
    const jint granted = activity.callMethod<jint>("checkSelfPermission", "(Ljava/lang/String;)I", jstr(permission).object<jstring>());
    if (granted != 0) {
        const QJniObject permissions =
            QJniObject::callStaticObjectMethod("java/lang/reflect/Array",
                                               "newInstance",
                                               "(Ljava/lang/Class;I)Ljava/lang/Object;",
                                               QJniObject::callStaticObjectMethod("java/lang/Class",
                                                                                  "forName",
                                                                                  "(Ljava/lang/String;)Ljava/lang/Class;",
                                                                                  jstr(QStringLiteral("java.lang.String")).object<jstring>())
                                                   .object(),
                                               1);
        QJniObject::callStaticMethod<void>("java/lang/reflect/Array",
                                           "set",
                                           "(Ljava/lang/Object;ILjava/lang/Object;)V",
                                           permissions.object(),
                                           0,
                                           jstr(permission).object<jstring>());
        activity.callMethod<void>("requestPermissions", "([Ljava/lang/String;I)V", permissions.object<jobjectArray>(), 1);
        failed(error, QStringLiteral("Allow storage access, then download again."));
        return {};
    }

    const QJniObject directory = QJniObject::callStaticObjectMethod("android/os/Environment",
                                                                    "getExternalStoragePublicDirectory",
                                                                    "(Ljava/lang/String;)Ljava/io/File;",
                                                                    jstr(QStringLiteral("Download")).object<jstring>());
    directory.callMethod<jboolean>("mkdirs");
    const QJniObject file("java/io/File", "(Ljava/io/File;Ljava/lang/String;)V", directory.object(), jstr(displayName).object<jstring>());
    const QJniObject stream("java/io/FileOutputStream", "(Ljava/io/File;)V", file.object());
    if (!stream.isValid())
        failed(error, QStringLiteral("Could not create the file in Downloads."));
    return stream;
}

QJniObject newValues(const QString &key, int value)
{
    const QJniObject values("android/content/ContentValues");
    const QJniObject boxed = QJniObject::callStaticObjectMethod("java/lang/Integer", "valueOf", "(I)Ljava/lang/Integer;", value);
    values.callMethod<void>("put", "(Ljava/lang/String;Ljava/lang/Integer;)V", jstr(key).object<jstring>(), boxed.object());
    return values;
}

}

bool saveToPublicDownloads(const QByteArray &data, const QString &displayName, QString *error)
{
    const QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid())
        return failed(error, QStringLiteral("No Android context."));

    QJniObject uri;
    QJniObject resolver;
    QJniObject out;
    const bool scoped = QNativeInterface::QAndroidApplication::sdkVersion() >= kAndroidQ;

    if (scoped) {
        const QString mimeType = QMimeDatabase().mimeTypeForFile(displayName, QMimeDatabase::MatchExtension).name();
        resolver = activity.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
        const QJniObject values = newValues(QStringLiteral("is_pending"), 1);
        values.callMethod<void>("put",
                                "(Ljava/lang/String;Ljava/lang/String;)V",
                                jstr(QStringLiteral("_display_name")).object<jstring>(),
                                jstr(displayName).object<jstring>());
        values.callMethod<void>("put",
                                "(Ljava/lang/String;Ljava/lang/String;)V",
                                jstr(QStringLiteral("mime_type")).object<jstring>(),
                                jstr(mimeType).object<jstring>());
        const QJniObject collection = QJniObject::callStaticObjectMethod("android/provider/MediaStore$Downloads",
                                                                         "getContentUri",
                                                                         "(Ljava/lang/String;)Landroid/net/Uri;",
                                                                         jstr(QStringLiteral("external_primary")).object<jstring>());
        uri = resolver.callObjectMethod("insert", "(Landroid/net/Uri;Landroid/content/ContentValues;)Landroid/net/Uri;", collection.object(), values.object());
        if (!uri.isValid())
            return failed(error, QStringLiteral("Could not create the download entry."));
        out = resolver.callObjectMethod("openOutputStream", "(Landroid/net/Uri;)Ljava/io/OutputStream;", uri.object());
    } else {
        out = openLegacyStream(activity, displayName, error);
        if (!out.isValid())
            return false;
    }
    if (!out.isValid())
        return failed(error, QStringLiteral("Could not open Downloads for writing."));

    // Written in chunks so no single JNI array has to hold the whole book.
    constexpr qsizetype kChunk = 1 << 20;
    QJniEnvironment env;
    bool copyFailed = false;
    for (qsizetype offset = 0; offset < data.size() && !copyFailed; offset += kChunk) {
        const jsize length = static_cast<jsize>(qMin(kChunk, data.size() - offset));
        jbyteArray chunk = env->NewByteArray(length);
        env->SetByteArrayRegion(chunk, 0, length, reinterpret_cast<const jbyte *>(data.constData() + offset));
        out.callMethod<void>("write", "([B)V", chunk);
        env->DeleteLocalRef(chunk);
        copyFailed = env.checkAndClearExceptions();
    }
    out.callMethod<void>("close");

    if (copyFailed) {
        if (scoped)
            resolver.callMethod<jint>("delete", "(Landroid/net/Uri;Ljava/lang/String;[Ljava/lang/String;)I", uri.object(), nullptr, nullptr);
        return failed(error, QStringLiteral("Copying to Downloads failed."));
    }

    if (scoped) {
        const QJniObject done = newValues(QStringLiteral("is_pending"), 0);
        resolver.callMethod<jint>("update",
                                  "(Landroid/net/Uri;Landroid/content/ContentValues;Ljava/lang/String;[Ljava/lang/String;)I",
                                  uri.object(),
                                  done.object(),
                                  nullptr,
                                  nullptr);
    }
    return !env.checkAndClearExceptions();
}

#else

bool saveToPublicDownloads(const QByteArray &, const QString &, QString *error)
{
    if (error)
        *error = QStringLiteral("Only available on Android.");
    return false;
}

#endif

}
