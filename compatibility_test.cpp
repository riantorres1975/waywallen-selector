// SPDX-License-Identifier: GPL-3.0-only
#include "compatibility.h"
#include <QSqlQuery>
#include <QtTest>

class CompatibilityTest : public QObject {
    Q_OBJECT
private slots:
    void versions_data() {
        QTest::addColumn<QString>("version");
        QTest::addColumn<bool>("supported");
        QTest::newRow("0.5") << "0.5.0" << true;
        QTest::newRow("0.5-patch") << "0.5.12" << true;
        QTest::newRow("0.6") << "0.6.0" << true;
        QTest::newRow("0.6-package") << "0.6.0-1" << true;
        QTest::newRow("older") << "0.4.9" << false;
        QTest::newRow("future") << "0.7.0" << false;
        QTest::newRow("major") << "1.6.0" << false;
        QTest::newRow("different-minor") << "0.60.0" << false;
        QTest::newRow("malformed") << "0.6.invalid" << false;
        QTest::newRow("empty") << "" << false;
    }

    void versions() {
        QFETCH(QString, version);
        QFETCH(bool, supported);
        QCOMPARE(Compatibility::supportedVersion(version), supported);
    }

    void library_data() {
        QTest::addColumn<QString>("timestamp");
        QTest::newRow("0.5-schema") << "create_at";
        QTest::newRow("0.6-schema") << "added_at";
    }

    void library() {
        QFETCH(QString, timestamp);
        const QString connection = "compatibility-" + timestamp;
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(":memory:");
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("CREATE TABLE library(id INTEGER PRIMARY KEY, path TEXT)"));
            QVERIFY(query.exec(QString("CREATE TABLE item(id INTEGER PRIMARY KEY, library_id INTEGER, "
                "display_name TEXT, type TEXT, path TEXT, preview_path TEXT, hidden INTEGER, %1 INTEGER)").arg(timestamp)));
            QVERIFY(query.exec("INSERT INTO library VALUES(1, '/library')"));
            QVERIFY(query.exec("INSERT INTO item VALUES"
                "(1,1,'older','image','old.png','old.png',0,100),"
                "(2,1,'new','image','new.png','new.png',0,200),"
                "(3,1,'same-date','image','tie.png','tie.png',0,200),"
                "(4,1,'hidden','image','hidden.png','hidden.png',1,300)"));
            QVERIFY(query.exec("PRAGMA query_only=ON"));
            QVERIFY(query.exec(Compatibility::libraryQuery(db)));
            QList<int> ids;
            while (query.next()) {
                ids.append(query.value(0).toInt());
                QCOMPARE(query.value(5).toString(), QString("/library"));
            }
            QCOMPARE(ids, QList<int>({3, 2, 1}));
            QVERIFY(!query.exec("DELETE FROM item"));
        }
        QSqlDatabase::removeDatabase(connection);
    }

    void unknownSchema() {
        const QString connection = "unknown-schema";
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(":memory:");
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("CREATE TABLE item(id INTEGER PRIMARY KEY, file_mtime_ns INTEGER)"));
            QVERIFY(Compatibility::libraryQuery(db).isEmpty());
        }
        QSqlDatabase::removeDatabase(connection);
    }

    void desktopOnly() {
        selectorwire::DisplayInfo legacy;
        legacy.set_display_id(7);
        QVERIFY(Compatibility::desktopTarget(legacy));
        // Test the upstream v0.6 role wire tag independently of our accessor.
        const std::string lockScreen("\x08\x07\xc8\x01\x01", 5);
        selectorwire::DisplayInfo decoded;
        QVERIFY(decoded.ParseFromString(lockScreen));
        QCOMPARE(decoded.role(), 1);
        QVERIFY(!Compatibility::desktopTarget(decoded));
        decoded.set_role(2); // Unknown future roles must not become destinations.
        QVERIFY(!Compatibility::desktopTarget(decoded));
    }
};

QTEST_GUILESS_MAIN(CompatibilityTest)
#include "compatibility_test.moc"
