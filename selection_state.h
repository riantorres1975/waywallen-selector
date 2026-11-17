// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QSettings>
#include <QVariantList>

namespace SelectionState {
inline QString initialWallpaperId(const QVariantList &wallpapers, const QString &currentId,
                                  const QSettings &settings) {
    // Store the stable ID, not an index: importing wallpapers changes positions.
    const QString saved = settings.value("navigation/lastAppliedWallpaperId").toString();
    for (const auto &candidate : {saved, currentId}) {
        if (candidate.isEmpty()) continue;
        for (const auto &item : wallpapers)
            if (item.toMap()["id"].toString() == candidate) return candidate;
    }
    return {};
}

inline void rememberAppliedWallpaper(QSettings &settings, const QString &id) {
    if (id.isEmpty()) return;
    settings.setValue("navigation/lastAppliedWallpaperId", id);
    settings.sync();
}
}
