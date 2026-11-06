// SPDX-License-Identifier: GPL-3.0-only
#include "appearance_reload.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <KConfigGroup>
#include <KSharedConfig>
#include <unistd.h>

namespace {
void notifyConfig(const QString &name, const QHash<QString, QByteArrayList> &groups) {
    qDBusRegisterMetaType<QByteArrayList>();
    qDBusRegisterMetaType<QHash<QString, QByteArrayList>>();
    auto message = QDBusMessage::createSignal("/" + name, "org.kde.kconfig.notify", "ConfigChanged");
    message.setArguments({QVariant::fromValue(groups)});
    QDBusConnection::sessionBus().send(message);
}

QStringList kvantumOwners() {
    const auto bus = QDBusConnection::sessionBus();
    const QDBusReply<QStringList> names = bus.interface()->registeredServiceNames();
    QSet<QString> owners;
    if (!names.isValid()) return {};
    for (const auto &name : names.value()) {
        if (!name.startsWith("org.kde.dolphin")) continue;
        const QDBusReply<uint> pid = bus.interface()->servicePid(name);
        if (!pid.isValid() || QFileInfo("/proc/" + QString::number(pid.value())).ownerId() != getuid()) continue;
        QFile environment("/proc/" + QString::number(pid.value()) + "/environ");
        if (!environment.open(QIODevice::ReadOnly)) continue;
        bool kvantum = false;
        for (const auto &entry : environment.readAll().split('\0'))
            if (entry.toLower() == "qt_style_override=kvantum") kvantum = true;
        if (!kvantum) continue;
        const QDBusReply<QString> owner = bus.interface()->serviceOwner(name);
        if (owner.isValid()) owners.insert(owner.value());
    }
    return owners.values();
}

bool targetedStyleChange(const QString &owner) {
    auto message = QDBusMessage::createTargetedSignal(owner, "/KGlobalSettings", "org.kde.KGlobalSettings", "notifyChange");
    message.setArguments({2, 0}); // KGlobalSettings / plasma-integration: StyleChanged.
    const auto bus = QDBusConnection::sessionBus();
    if (!bus.send(message)) return false;
    // Same sender + destination: wait until the style notification was processed
    // before changing/restoring the temporary setting. No timing-based sleeps.
    // Unlike Peer.Ping (which a transport thread may answer), this property
    // belongs to the application's main-thread QObject in KDBusService.
    auto barrier = QDBusMessage::createMethodCall(owner, "/MainApplication", "org.freedesktop.DBus.Properties", "Get");
    barrier.setArguments({"org.qtproject.Qt.QCoreApplication", "applicationName"});
    return bus.call(barrier, QDBus::Block, 1500).type() != QDBusMessage::ErrorMessage;
}

void reloadDolphinKvantum(QStringList &warnings) {
    const auto owners = kvantumOwners();
    if (owners.isEmpty()) return;
    if (owners.size() > 4) {
        warnings << "Hay más de cuatro procesos de Dolphin: se guardó el tema, pero no se forzó su recarga en vivo.";
        return;
    }
    const auto config = KSharedConfig::openConfig("kdeglobals", KConfig::SimpleConfig);
    config->reparseConfiguration();
    KConfigGroup group(config, "KDE");
    const bool hadStyle = group.hasKey("widgetStyle");
    const QString original = group.readEntry("widgetStyle", QString("breeze"));
    QString temporary = original;
    auto writeStyle = [&](const QString &style) {
        config->reparseConfiguration();
        group.writeEntry("widgetStyle", style); // Deliberately NO KConfig::Notify.
        temporary = style;
        return config->sync();
    };
    // Kvantum caches its SVG renderer in the QStyle. A palette-only signal is
    // insufficient. Recreate that style only in its Dolphin processes; all
    // other applications keep their original style, and no windows are closed.
    if ((original.compare("breeze", Qt::CaseInsensitive) == 0 || writeStyle("breeze"))) {
        for (const auto &owner : owners)
            if (!targetedStyleChange(owner)) warnings << "Dolphin no confirmó la primera fase de recarga del estilo.";
        if (writeStyle("kvantum")) {
            for (const auto &owner : owners)
                if (!targetedStyleChange(owner)) warnings << "Dolphin no confirmó la recarga del tema Kvantum.";
        } else warnings << "No se pudo preparar la recarga de Kvantum.";
    } else warnings << "No se pudo preparar la recarga del estilo de Dolphin.";
    config->reparseConfiguration();
    // Never discard a different style selected by the user during the reload.
    if (group.readEntry("widgetStyle", original) == temporary) {
        if (hadStyle) group.writeEntry("widgetStyle", original);
        else group.deleteEntry("widgetStyle");
        if (!config->sync()) warnings << "No se pudo restaurar el estilo global original.";
    } else warnings << "El estilo global cambió durante la recarga; se conservó la elección nueva.";
}
}

QStringList reloadAppearance(const QStringList &parts) {
    QStringList warnings;
    const auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return {"No hay conexión con la sesión de KDE para recargar los colores."};
    if (parts.contains("palette")) {
        QHash<QString, QByteArrayList> groups;
        groups.insert("General", {"ColorScheme", "AccentColor", "AccentColorFromWallpaper"});
        for (const auto &role : {"Button", "Complementary", "Header", "Selection", "Tooltip", "View", "Window"})
            groups.insert("Colors:" + QString::fromLatin1(role), {"BackgroundNormal", "ForegroundNormal", "DecorationFocus"});
        notifyConfig("kdeglobals", groups);
        auto message = QDBusMessage::createSignal("/KGlobalSettings", "org.kde.KGlobalSettings", "notifyChange");
        message.setArguments({0, 0}); // PaletteChanged, not a global style change.
        bus.send(message);
    }
    if (parts.contains("plasma")) notifyConfig("plasmarc", {{"Theme", {"name"}}});
    if (parts.contains("kvantum")) reloadDolphinKvantum(warnings);
    if (parts.contains("borders")) {
        auto message = QDBusMessage::createMethodCall("org.kde.KWin", "/Effects", "org.kde.kwin.Effects", "reconfigureEffect");
        message.setArguments({"kwin4_effect_shapecorners"});
        const auto reply = bus.call(message, QDBus::Block, 3000);
        if (reply.type() == QDBusMessage::ErrorMessage) warnings << "No se pudo recargar el efecto de bordes: " + reply.errorMessage();
    }
    return warnings;
}
