#include "core/dropbox/DropboxAuthProvider.h"

#ifdef Q_OS_ANDROID

#include <QDebug>
#include <QJniObject>

// Called back from io.github.kalibrebox.MainActivity whenever the browser
// finishes the implicit OAuth flow with a db-<appkey>:// redirect.
extern "C" JNIEXPORT void JNICALL
Java_io_github_kalibrebox_MainActivity_nativeOnDropboxAuthUrl(JNIEnv *env,
                                                              jclass,
                                                              jstring url)
{
    const QJniObject jUrl(url);
    qInfo() << "Dropbox JNI redirect received";
    KalibreBox::DropboxAuthProvider::dispatchAuthRedirect(jUrl.toString());
}

#endif // Q_OS_ANDROID
