// SPDX-License-Identifier: GPL-3.0-only
#include "slantedpreview.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

// No real backend: these tests cannot contact Waywallen or change a wallpaper.
class FakeSelector : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList wallpapers MEMBER wallpapers NOTIFY wallpapersChanged)
    Q_PROPERTY(bool busy MEMBER busy NOTIFY stateChanged)
    Q_PROPERTY(bool ready READ ready CONSTANT)
    Q_PROPERTY(QString error READ error CONSTANT)
public:
    QVariantList wallpapers;
    bool busy = false;
    QString appliedId;
    bool ready() const { return true; }
    QString error() const { return {}; }
    FakeSelector() {
        for (int i = 0; i < 12; ++i)
            wallpapers.append(QVariantMap{{"id", QString::number(i)},
                {"title", QString::number(i)}, {"preview", ""}});
    }
    Q_INVOKABLE void apply(const QString &id, int, bool, bool) { appliedId = id; }
signals:
    void wallpapersChanged();
    void stateChanged();
    void applied();
    void accentWarning(const QString &message);
    void appearanceWarning(const QString &message);
};

class CarouselInputTest : public QObject {
    Q_OBJECT
    std::unique_ptr<FakeSelector> selector;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow *window = nullptr;
    QQuickItem *gallery = nullptr;
private slots:
    void init() {
        selector = std::make_unique<FakeSelector>();
        engine = std::make_unique<QQmlApplicationEngine>();
        engine->rootContext()->setContextProperty("selector", selector.get());
        engine->load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        window->show();
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        gallery = window->findChild<QQuickItem *>("gallery");
        QVERIFY(gallery);
        QTRY_COMPARE(gallery->property("currentIndex").toInt(), 3);
        QTRY_COMPARE(gallery->property("offset").toReal(), 9.0);
    }
    void cleanup() {
        gallery = nullptr;
        window = nullptr;
        engine.reset();
        selector.reset();
    }
    void wheelNavigation_data() {
        QTest::addColumn<QPoint>("position");
        QTest::addColumn<QPoint>("angle");
        QTest::addColumn<QPoint>("pixels");
        QTest::addColumn<int>("expected");
        QTest::newRow("vertical-over-card") << QPoint(900, 230) << QPoint(0, -120) << QPoint() << 4;
        QTest::newRow("horizontal-over-card") << QPoint(900, 230) << QPoint(-120, 0) << QPoint() << 4;
        QTest::newRow("touchpad-pixels") << QPoint(900, 230) << QPoint() << QPoint(0, -60) << 4;
        QTest::newRow("several-notches") << QPoint(900, 230) << QPoint(0, -360) << QPoint() << 6;
        QTest::newRow("window-edge") << QPoint(2, 230) << QPoint(0, -120) << QPoint() << 4;
        QTest::newRow("reverse") << QPoint(900, 230) << QPoint(0, 120) << QPoint() << 2;
    }
    void wheelNavigation() {
        QFETCH(QPoint, position);
        QFETCH(QPoint, angle);
        QFETCH(QPoint, pixels);
        QFETCH(int, expected);
        QTest::wheelEvent(window, position, angle, pixels);
        QCOMPARE(gallery->property("currentIndex").toInt(), expected);
        QVERIFY(selector->appliedId.isEmpty());
    }
    void highResolutionAndWrap() {
        for (int i = 0; i < 2; ++i) {
            QTest::wheelEvent(window, QPoint(900, 230), QPoint(0, -40));
            QCOMPARE(gallery->property("currentIndex").toInt(), 3);
        }
        QTest::wheelEvent(window, QPoint(900, 230), QPoint(0, -40));
        QCOMPARE(gallery->property("currentIndex").toInt(), 4);
        gallery->setProperty("currentIndex", 0);
        QTest::wheelEvent(window, QPoint(900, 230), QPoint(0, 120));
        QCOMPARE(gallery->property("currentIndex").toInt(), 11);
        QTest::wheelEvent(window, QPoint(900, 230), QPoint(0, -120));
        QCOMPARE(gallery->property("currentIndex").toInt(), 0);
    }
    void busyIgnoresWheel() {
        selector->busy = true;
        emit selector->stateChanged();
        QTest::wheelEvent(window, QPoint(900, 230), QPoint(0, -120));
        QCOMPARE(gallery->property("currentIndex").toInt(), 3);
        QVERIFY(selector->appliedId.isEmpty());
    }
    void keyboardAndConfirm() {
        QTRY_VERIFY(window->isActive());
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(gallery->property("currentIndex").toInt(), 4);
        QTest::keyClick(window, Qt::Key_Left);
        QCOMPARE(gallery->property("currentIndex").toInt(), 3);
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(selector->appliedId, QString("3"));
    }
    void clickStillWorks() {
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(900, 230));
        QCOMPARE(selector->appliedId, QString("3"));
    }
    void dragDoesNotApply() {
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, QPoint(900, 230));
        for (int x = 860; x >= 500; x -= 40)
            QTest::mouseMove(window, QPoint(x, 230), 20);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(500, 230));
        QTRY_VERIFY(gallery->property("currentIndex").toInt() != 3);
        QVERIFY(selector->appliedId.isEmpty());
    }
    void escapeClosesWithoutApplying() {
        QTRY_VERIFY(window->isActive());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!window->isVisible());
        QVERIFY(selector->appliedId.isEmpty());
    }
};

int main(int argc, char **argv) {
    QTemporaryDir config;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QGuiApplication app(argc, argv);
    app.setOrganizationName("selector-tests");
    app.setApplicationName("carousel-input");
    qmlRegisterType<SlantedPreview>("Local.Selector", 1, 0, "SlantedPreview");
    CarouselInputTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ui_test.moc"
