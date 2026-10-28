#include <KLocalizedQmlContext>
#include <KLocalizedString>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QSettings>

#include "core/dropbox/DropboxClient.h"
#include "core/dropbox/DropboxCredential.h"
#include "ui/LibraryController.h"
#include "ui/LibraryControllerQml.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("KalibreBox"));
    QGuiApplication::setApplicationName(QStringLiteral("KalibreBox"));
    QGuiApplication::setApplicationVersion(KALIBREBOX_VERSION);

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
#ifdef Q_OS_ANDROID
        QQuickStyle::setStyle(QStringLiteral("org.kde.breeze"));
#else
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
#endif
    }

    QSettings settings;

    auto *dropbox = new KalibreBox::DropboxClient(&settings, &app);
    if (const std::optional<KalibreBox::DropboxCredential> saved = KalibreBox::DropboxCredential::load(&settings))
        dropbox->setCredential(*saved);

    QQmlApplicationEngine engine;
    KLocalizedString::setApplicationDomain("kalibrebox");
    KLocalization::setupLocalizedContext(&engine);

    KalibreBox::LibraryControllerQml::instance = new KalibreBox::LibraryController(dropbox, &engine);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() {
            QCoreApplication::exit(EXIT_FAILURE);
        },
        Qt::QueuedConnection);

    engine.loadFromModule(QStringLiteral("KalibreBox"), QStringLiteral("Main"));

    return app.exec();
}
