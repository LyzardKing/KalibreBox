#include <KLocalizedContext>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSettings>

#include "core/dropbox/DropboxClient.h"
#include "core/dropbox/DropboxCredential.h"
#include "dev/DevController.h"
#include "ui/LibraryController.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("KalibreBox"));
    QGuiApplication::setApplicationName(QStringLiteral("KalibreBox"));
    QGuiApplication::setApplicationVersion(KALIBREBOX_VERSION);

    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    QSettings settings;

    auto *dropbox = new KalibreBox::DropboxClient(&settings, &app);
    if (const std::optional<KalibreBox::DropboxCredential> saved =
            KalibreBox::DropboxCredential::load(&settings))
        dropbox->setCredential(*saved);

    QQmlApplicationEngine engine;
    auto *localization = new KLocalizedContext(&engine);
    localization->setTranslationDomain(QStringLiteral("kalibrebox"));
    engine.rootContext()->setContextObject(localization);

    auto *devController = new KalibreBox::DevController(dropbox, &engine);
    auto *libraryController = new KalibreBox::LibraryController(dropbox, &engine);
    QObject::connect(devController, &KalibreBox::DevController::booksChanged,
                     libraryController, &KalibreBox::LibraryController::refresh);
    QObject::connect(devController, &KalibreBox::DevController::librariesChanged,
                     libraryController, &KalibreBox::LibraryController::refresh);
    engine.rootContext()->setContextProperty(QStringLiteral("dev"), devController);
    engine.rootContext()->setContextProperty(QStringLiteral("library"),
                                             libraryController);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);

    engine.loadFromModule(QStringLiteral("KalibreBox"), QStringLiteral("Main"));

    return app.exec();
}
