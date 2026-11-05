// SPDX-License-Identifier: GPL-3.0-only
#include "backend.h"
#include "slantedpreview.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QScreen>
#include <QCursor>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QCommandLineParser>
#include <QTimer>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>

class WindowControl : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.local.WaywallenSelector")
public:
    QQuickWindow *window = nullptr;
public slots:
    void toggle() {
        if (!window) return;
        if (window->isVisible()) window->close();
        else { window->show(); window->requestActivate(); }
    }
};

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("local.desktop");
    app.setApplicationName("WaywallenSelector");
    app.setApplicationDisplayName("Selector de wallpapers");
    app.setDesktopFileName("org.local.WaywallenSelector");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"diagnose", "Check library and daemon without changing anything."});
    parser.addOption({"capture", "Save an image of the selector only and exit (no wallpaper is changed).", "file"});
    parser.process(app);
    const bool diagnose = parser.isSet("diagnose");
    const bool capture = parser.isSet("capture");
    const QString service = "org.local.WaywallenSelector";
    WindowControl control;
    if (!diagnose && !capture) {
        if (!QDBusConnection::sessionBus().registerService(service)) {
            QDBusInterface existing(service, "/Selector", "org.local.WaywallenSelector");
            existing.call("toggle");
            return 0;
        }
        QDBusConnection::sessionBus().registerObject("/Selector", &control, QDBusConnection::ExportAllSlots);
    }

    Backend backend(capture || diagnose);
    if (diagnose) {
        auto report = [&] {
            if (!backend.ready() && backend.error().isEmpty()) return;
            QJsonArray firstIds;
            const auto wallpapers = backend.wallpapers();
            for (int i = 0; i < qMin(5, wallpapers.size()); ++i)
                firstIds.append(wallpapers[i].toMap()["id"].toString());
            QTextStream(stdout) << QJsonDocument(QJsonObject{
                {"wallpapers", backend.total()}, {"monitors", backend.displays().size()},
                {"ready", backend.ready()}, {"error", backend.error()},
                {"firstWallpaperIds", firstIds}
            }).toJson();
            app.exit(backend.ready() ? 0 : 1);
        };
        QObject::connect(&backend, &Backend::stateChanged, &app, report);
        QTimer::singleShot(0, &app, report);
        QTimer::singleShot(8000, &app, [&] { app.exit(2); });
        return app.exec();
    }

    qmlRegisterType<SlantedPreview>("Local.Selector", 1, 0, "SlantedPreview");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("selector", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window) return 1;
    control.window = window;
    auto *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    window->setScreen(screen);
    const QRect available = screen->availableGeometry();
    const QSize windowSize(qMin(1800, available.width() - 60), qMin(460, available.height() - 80));
    // A fixed-size chooser is naturally floating in Krohnkite. Publish these
    // constraints before mapping the Wayland window; no tiler reload is needed.
    window->setMinimumSize(windowSize);
    window->setMaximumSize(windowSize);
    window->resize(windowSize);
    window->setPosition(available.center() - QPoint(window->width() / 2, window->height() / 2));
    backend.preferScreen(screen->model());
    window->show();
    window->requestActivate();
    if (capture) {
        QTimer::singleShot(3000, &app, [&] {
            const bool saved = window->grabWindow().save(parser.value("capture"));
            app.exit(saved ? 0 : 3);
        });
    }
    return app.exec();
}
#include "main.moc"
