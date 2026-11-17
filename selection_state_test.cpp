// SPDX-License-Identifier: GPL-3.0-only
#include "selection_state.h"
#include <QTemporaryDir>
#include <QtTest>

class SelectionStateTest : public QObject {
    Q_OBJECT
    static QVariantList library(std::initializer_list<const char *> ids) {
        QVariantList result;
        for (const auto *id : ids) result.append(QVariantMap{{"id", QString::fromLatin1(id)}});
        return result;
    }
private slots:
    void remembersAppliedIdAcrossSettingsInstances() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString file = directory.filePath("selector.ini");
        {
            QSettings settings(file, QSettings::IniFormat);
            settings.setValue("appearance/automaticAppColors", true);
            SelectionState::rememberAppliedWallpaper(settings, "42");
            QCOMPARE(settings.status(), QSettings::NoError);
        }
        const QSettings reopened(file, QSettings::IniFormat);
        const auto wallpapers = library({"90", "42", "7"});
        QCOMPARE(SelectionState::initialWallpaperId(wallpapers, "7", reopened), QString("42"));
        QVERIFY(reopened.value("appearance/automaticAppColors").toBool());
        // Newly imported wallpapers must not move the remembered selection.
        QCOMPARE(SelectionState::initialWallpaperId(library({"100", "99", "90", "42", "7"}),
                                                    "7", reopened), QString("42"));
    }

    void fallsBackToCurrentOrRecent() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings settings(directory.filePath("selector.ini"), QSettings::IniFormat);
        const auto wallpapers = library({"90", "42", "7"});
        QCOMPARE(SelectionState::initialWallpaperId(wallpapers, "7", settings), QString("7"));
        SelectionState::rememberAppliedWallpaper(settings, "removed-or-hidden");
        QCOMPARE(SelectionState::initialWallpaperId(wallpapers, "42", settings), QString("42"));
        QVERIFY(SelectionState::initialWallpaperId(wallpapers, "also-removed", settings).isEmpty());
        QVERIFY(SelectionState::initialWallpaperId({}, "42", settings).isEmpty());
    }

    void openingAndBrowsingNeverWriteSettings() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings settings(directory.filePath("selector.ini"), QSettings::IniFormat);
        SelectionState::rememberAppliedWallpaper(settings, "42");
        const auto before = settings.allKeys();
        // Only a confirmed apply calls rememberAppliedWallpaper in the backend;
        // resolving a new window's initial selection is strictly read-only.
        for (int i = 0; i < 3; ++i)
            QCOMPARE(SelectionState::initialWallpaperId(library({"7", "42", "90"}),
                                                        "90", settings), QString("42"));
        SelectionState::rememberAppliedWallpaper(settings, "");
        QCOMPARE(settings.allKeys(), before);
        QCOMPARE(settings.value("navigation/lastAppliedWallpaperId").toString(), QString("42"));
    }
};

QTEST_GUILESS_MAIN(SelectionStateTest)
#include "selection_state_test.moc"
