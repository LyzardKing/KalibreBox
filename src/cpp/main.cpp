#include <KLocalizedContext>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSettings>

#include "core/dropbox/DropboxClient.h"
#include "core/dropbox/DropboxCredential.h"
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

    engine.rootContext()->setContextProperty(
        QStringLiteral("library"), new KalibreBox::LibraryController(dropbox, &engine));

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);

    engine.loadFromModule(QStringLiteral("KalibreBox"), QStringLiteral("Main"));

    return app.exec();
}
