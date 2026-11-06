// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QStringList>

// Publish palette/theme changes through KDE's actual live-reload interfaces.
// No application restarts, KWin script reloads, or wallpaper operations.
QStringList reloadAppearance(const QStringList &parts);
