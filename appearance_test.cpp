// SPDX-License-Identifier: GPL-3.0-only
#include <QApplication>
#include <QDBusConnection>
#include <QProcess>
#include <QStyle>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QtTest>
#include <KConfigGroup>
#include <KConfigWatcher>
#include <KSharedConfig>

class FakeEffects : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.Effects")
public:
    QStringList reconfigured;
public slots:
    void reconfigureEffect(const QString &name) { reconfigured << name; }
};

class AppearanceTest : public QObject {
    Q_OBJECT
    FakeEffects effects;
    QStringList styles;
    int palettes = 0;
private slots:
    void notification(int type, int) {
        if (type == 0) ++palettes;
        if (type != 2) return;
        // Mirror plasma-integration's StyleChanged handler on the main thread.
        const auto config = KSharedConfig::openConfig("kdeglobals", KConfig::SimpleConfig);
        config->reparseConfiguration();
        const auto style = KConfigGroup(config, "KDE").readEntry("widgetStyle", QString("breeze"));
        if (qApp->style()->name().compare(style, Qt::CaseInsensitive) != 0)
            qApp->setStyle(QStyleFactory::create(style));
        styles << qApp->style()->name().toLower();
    }
    void initTestCase() {
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.isConnected());
        QVERIFY(bus.registerService("org.kde.KWin"));
        QVERIFY(bus.registerObject("/Effects", &effects, QDBusConnection::ExportAllSlots));
        QVERIFY(bus.registerService("org.kde.dolphin.selector-test"));
        QVERIFY(bus.registerObject("/MainApplication", qApp, QDBusConnection::ExportScriptableProperties));
        QVERIFY(bus.connect({}, "/KGlobalSettings", "org.kde.KGlobalSettings", "notifyChange", this, SLOT(notification(int,int))));
    }
    void nativeReload_data() {
        QTest::addColumn<bool>("explicitStyle");
        QTest::newRow("implicit-breeze") << false;
        QTest::newRow("explicit-breeze") << true;
    }
    void nativeReload() {
        QFETCH(bool, explicitStyle);
        if (!QStyleFactory::keys().contains("kvantum", Qt::CaseInsensitive))
            QSKIP("Kvantum is not installed on this system.");
        qApp->setStyle(QStyleFactory::create("kvantum"));
        styles.clear();
        effects.reconfigured.clear();
        palettes = 0;
        const auto config = KSharedConfig::openConfig("kdeglobals", KConfig::SimpleConfig);
        config->reparseConfiguration();
        KConfigGroup group(config, "KDE");
        if (explicitStyle) group.writeEntry("widgetStyle", "breeze");
        else group.deleteEntry("widgetStyle");
        KConfigGroup(config, "General").writeEntry("font", "KEEP ORIGINAL FONT");
        QVERIFY(config->sync());
        const auto plasma = KSharedConfig::openConfig("plasmarc", KConfig::SimpleConfig);
        KConfigGroup(plasma, "Theme").writeEntry("name", "WaywallenSelector-Plasma-A");
        QVERIFY(plasma->sync());
        const auto watcher = KConfigWatcher::create(plasma);
        int themeChanges = 0;
        connect(watcher.data(), &KConfigWatcher::configChanged, this,
                [&](const KConfigGroup &changed, const QByteArrayList &keys) {
            if (changed.name() == "Theme" && keys.contains("name")) ++themeChanges;
        });
        QProcess child;
        child.start(qEnvironmentVariable("SELECTOR_TEST_BINARY"), {"--reload-appearance", "palette,plasma,kvantum,borders"});
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 15000);
        const auto error = child.readAllStandardError();
        QVERIFY2(child.exitCode() == 0, error.constData());
        QCOMPARE(styles, QStringList({"breeze", "kvantum"}));
        QCOMPARE(effects.reconfigured, QStringList({"kwin4_effect_shapecorners"}));
        QTRY_VERIFY(palettes > 0);
        QTRY_VERIFY(themeChanges > 0);
        config->reparseConfiguration();
        QCOMPARE(group.hasKey("widgetStyle"), explicitStyle);
        QCOMPARE(KConfigGroup(config, "General").readEntry("font"), QString("KEEP ORIGINAL FONT"));
    }
};

int main(int argc, char **argv) {
    QTemporaryDir config;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QApplication app(argc, argv);
    app.setApplicationName("selector-appearance-test");
    AppearanceTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "appearance_test.moc"
