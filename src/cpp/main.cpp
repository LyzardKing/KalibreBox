#include <KLocalizedContext>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("KalibreBox"));
    QGuiApplication::setApplicationName(QStringLiteral("KalibreBox"));
    QGuiApplication::setApplicationVersion(KALIBREBOX_VERSION);

    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    QQmlApplicationEngine engine;
    auto *localization = new KLocalizedContext(&engine);
    localization->setTranslationDomain(QStringLiteral("kalibrebox"));
    engine.rootContext()->setContextObject(localization);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);

    engine.loadFromModule(QStringLiteral("KalibreBox"), QStringLiteral("Main"));

    return app.exec();
}
