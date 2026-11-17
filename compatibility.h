// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlRecord>
#include "protocol.pb.h"

namespace Compatibility {
inline bool supportedVersion(const QString &version) {
    static const QRegularExpression supported(QStringLiteral(R"(^0\.(5|6)\.\d+(?:[-+][\w.-]+)?$)"));
    return supported.match(version).hasMatch();
}

inline QString libraryQuery(const QSqlDatabase &db) {
    const auto columns = db.record("item");
    // Waywallen 0.6 renamed the import timestamp. Never sort by file mtime:
    // the carousel must keep showing the most recently added wallpapers first.
    const QString timestamp = columns.indexOf("added_at") >= 0 ? "added_at"
        : columns.indexOf("create_at") >= 0 ? "create_at" : QString();
    if (timestamp.isEmpty()) return {};
    return QStringLiteral("SELECT i.id, i.display_name, i.type, i.path, i.preview_path, l.path "
                          "FROM item i JOIN library l ON l.id=i.library_id "
                          "WHERE i.hidden=0 ORDER BY i.%1 DESC, i.id DESC").arg(timestamp);
}

inline bool desktopTarget(const selectorwire::DisplayInfo &display) {
    // An absent role defaults to desktop in both supported versions.
    // Never offer lock-screen surfaces as wallpaper destinations.
    return display.role() == 0;
}
}
