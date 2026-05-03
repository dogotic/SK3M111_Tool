#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "serialhandler.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("SK3M111 Viewer");
    app.setApplicationVersion("1.0");

    SerialHandler serialHandler;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("serialHandler", &serialHandler);
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
