// SPDX-License-Identifier: GPL-3.0-only
#include "backend.h"
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>
#include <QSet>
#include <KSharedConfig>
#include <KConfigGroup>

namespace {
const QString service = QStringLiteral("org.waywallen.waywallen.Daemon");
const QString object = QStringLiteral("/org/waywallen/waywallen/Daemon");
const QString interface = QStringLiteral("org.waywallen.waywallen.Daemon1");
QString localFile(const QString &root, const QString &relative) {
    if (relative.isEmpty()) return {};
    const QUrl url(relative);
    const QString path = url.isLocalFile() ? url.toLocalFile()
        : (QDir::isAbsolutePath(relative) ? relative : QDir(root).filePath(relative));
    return QFileInfo(path).isFile() ? QFileInfo(path).absoluteFilePath() : QString();
}
}

Backend::Backend(bool previewOnly, QObject *parent) : QObject(parent), m_previewOnly(previewOnly) {
    QDBusInterface properties(service, object, "org.freedesktop.DBus.Properties");
    QDBusReply<QDBusVariant> current = properties.call("Get", interface, "CurrentWallpaperId");
    if (current.isValid()) m_currentId = current.value().variant().toString();
    loadLibrary();
    connectDaemon();
}

void Backend::loadLibrary() {
    const QString path = QDir::homePath() + "/.var/app/org.waywallen.waywallen/data/waywallen/waywallen-v2.db";
    if (!QFileInfo::exists(path)) { fail("No se encontró la biblioteca de Waywallen."); return; }
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", "wallpaper-library-readonly");
        db.setDatabaseName(path);
        db.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=1500");
        if (!db.open()) { fail("No se pudo leer la biblioteca: " + db.lastError().text()); return; }
        QSqlQuery query(db);
        if (!query.exec("SELECT i.id, i.display_name, i.type, i.path, i.preview_path, l.path "
                        "FROM item i JOIN library l ON l.id=i.library_id "
                        "WHERE i.hidden=0 ORDER BY i.create_at DESC, i.id DESC")) {
            fail("La biblioteca tiene un formato no compatible: " + query.lastError().text());
            return;
        }
        while (query.next()) {
            const QString id = query.value(0).toString();
            const QString root = query.value(5).toString();
            const QString resource = localFile(root, query.value(3).toString());
            QString preview = localFile(root, query.value(4).toString());
            const QString type = query.value(2).toString();
            if (preview.isEmpty() && type == "image") preview = resource;
            if (preview.isEmpty() && !resource.isEmpty()) {
                const QDir directory = QFileInfo(resource).absoluteDir();
                for (const auto &name : {"preview.jpg", "preview.png", "preview.gif"}) {
                    preview = localFile(directory.path(), name);
                    if (!preview.isEmpty()) break;
                }
            }
            if (resource.isEmpty()) continue; // Never offer deleted library entries.
            QString title = query.value(1).toString().trimmed();
            if (title.isEmpty()) title = QFileInfo(resource).completeBaseName();
            const QString label = type == "scene" ? "ESCENA" : type == "video" ? "VÍDEO"
                : type == "web" ? "WEB" : "IMAGEN";
            m_all.append(QVariantMap{{"id", id}, {"title", title}, {"type", label},
                {"preview", preview.isEmpty() ? QString() : QUrl::fromLocalFile(preview).toString()}});
        }
        db.close();
    }
    QSqlDatabase::removeDatabase("wallpaper-library-readonly");
    m_filtered = m_all;
}

void Backend::setSearch(const QString &value) {
    if (value == m_search) return;
    m_search = value;
    m_filtered.clear();
    for (const auto &item : m_all) {
        const auto map = item.toMap();
        if ((map["title"].toString() + " " + map["type"].toString()).contains(value.trimmed(), Qt::CaseInsensitive))
            m_filtered.append(item);
    }
    emit searchChanged();
    emit wallpapersChanged();
}

void Backend::setTargetIndex(int index) {
    if (index < -1 || index >= m_displays.size() || index == m_targetIndex) return;
    m_targetIndex = index;
    emit targetIndexChanged();
}

void Backend::preferScreen(const QString &model) {
    m_screenModel = model;
    if (model.isEmpty()) return;
    for (int i = 0; i < m_displays.size(); ++i) {
        if (m_displays[i].toMap()["name"].toString().contains(model, Qt::CaseInsensitive)) {
            setTargetIndex(i); return;
        }
    }
}

void Backend::fail(const QString &message) {
    m_error = message;
    m_busy = false;
    emit errorChanged();
    emit stateChanged();
}

void Backend::connectDaemon() {
    QDBusInterface properties(service, object, "org.freedesktop.DBus.Properties");
    QDBusReply<QDBusVariant> port = properties.call("Get", interface, "WsPort");
    QDBusReply<QDBusVariant> version = properties.call("Get", interface, "Version");
    if (!port.isValid() || !version.isValid() || !port.value().variant().toUInt()) {
        fail("Abre Waywallen antes de usar el selector."); return;
    }
    if (!version.value().variant().toString().startsWith("0.5.")) {
        fail("Esta integración está comprobada para Waywallen 0.5.x. Usa su interfaz para esta versión."); return;
    }
    connect(&m_socket, &QWebSocket::binaryMessageReceived, this, &Backend::receive);
    connect(&m_socket, &QWebSocket::errorOccurred, this, [this] {
        m_ready = false;
        fail("No se pudo conectar con Waywallen: " + m_socket.errorString());
    });
    connect(&m_socket, &QWebSocket::disconnected, this, [this] {
        m_ready = false;
        m_callbacks.clear();
        fail("Se cerró la conexión con Waywallen. Vuelve a abrir el selector.");
    });
    connect(&m_socket, &QWebSocket::connected, this, [this] {
        selectorwire::Request req;
        req.mutable_display_list();
        request(req, [this](const selectorwire::Response &response) {
            if (!response.has_display_list()) { fail("Waywallen no devolvió sus monitores."); return; }
            QSet<QString> canvases;
            for (const auto &display : response.display_list().displays()) {
                const QString canvas = QString::fromStdString(display.canvas_id());
                if (!canvas.isEmpty() && canvases.contains(canvas)) continue;
                if (!canvas.isEmpty()) canvases.insert(canvas);
                QString name = QString::fromStdString(display.alias().empty() ? display.name() : display.alias());
                if (!canvas.isEmpty()) name = "Lienzo · " + name;
                m_displays.append(QVariantMap{{"id", QString::number(display.display_id())},
                    {"canvas", canvas}, {"key", QString::fromStdString(display.settings_key())},
                    {"name", name}, {"label", name + QString(" · %1×%2").arg(display.width()).arg(display.height())}});
            }
            emit displaysChanged();
            if (m_displays.size() == 1) setTargetIndex(0);
            else {
                const QString saved = QSettings().value("targetKey").toString();
                for (int i = 0; i < m_displays.size(); ++i)
                    if (!saved.isEmpty() && m_displays[i].toMap()["key"].toString() == saved) setTargetIndex(i);
                preferScreen(m_screenModel);
            }
            if (m_displays.isEmpty()) { fail("Waywallen no tiene monitores conectados."); return; }
            selectorwire::Request settings;
            settings.mutable_settings_get();
            request(settings, [this](const selectorwire::Response &reply) {
                selectorwire::GlobalSettings global;
                if (!reply.has_settings_get() || reply.settings_get().global().empty()
                    || !global.ParseFromString(reply.settings_get().global())) {
                    fail("Waywallen devolvió una configuración no compatible."); return;
                }
                auto copy = global;
                copy.mutable_transition()->set_kind(1);
                std::string originalFields, copiedFields;
                const bool originalOk = global.GetReflection()->GetUnknownFields(global).SerializeToString(&originalFields);
                const bool copiedOk = copy.GetReflection()->GetUnknownFields(copy).SerializeToString(&copiedFields);
                if (!originalOk || !copiedOk || originalFields != copiedFields) { fail("No se pueden preservar los ajustes existentes."); return; }
                m_ready = !m_all.isEmpty();
                emit stateChanged();
            });
        });
    });
    m_socket.open(QUrl(QString("ws://127.0.0.1:%1").arg(port.value().variant().toUInt())));
}

void Backend::request(selectorwire::Request req, Callback callback) {
    const auto id = ++m_requestId;
    req.set_request_id(id);
    m_callbacks.insert(id, std::move(callback));
    const auto bytes = req.SerializeAsString();
    m_socket.sendBinaryMessage(QByteArray(bytes.data(), bytes.size()));
    QTimer::singleShot(30000, this, [this, id] {
        if (m_callbacks.remove(id)) fail("Waywallen tardó demasiado en responder. No se repetirá la operación automáticamente.");
    });
}

void Backend::receive(const QByteArray &data) {
    selectorwire::ServerFrame frame;
    if (!frame.ParseFromArray(data.constData(), data.size()) || !frame.has_response()) return;
    const auto &response = frame.response();
    auto it = m_callbacks.find(response.request_id());
    if (it == m_callbacks.end()) return;
    auto callback = std::move(it.value());
    m_callbacks.erase(it);
    if (response.error_code() != 0 || response.status() != 1) {
        fail("Waywallen: " + QString::fromStdString(response.message())); return;
    }
    callback(response);
}

void Backend::apply(const QString &id, int transition, bool accent) {
    if (m_previewOnly || !m_ready || m_busy || m_targetIndex < 0 || m_targetIndex >= m_displays.size()) return;
    bool known = false;
    for (const auto &item : m_all) if (item.toMap()["id"].toString() == id) known = true;
    if (!known || transition < 0 || transition > 3) return;
    const QVariantMap target = m_displays[m_targetIndex].toMap();
    m_busy = true;
    m_error.clear();
    emit stateChanged();
    emit errorChanged();
    selectorwire::Request get;
    get.mutable_settings_get();
    request(get, [this, id, target, transition, accent](const selectorwire::Response &response) {
        if (!response.has_settings_get()) { fail("Respuesta de configuración no compatible."); return; }
        const auto &settings = response.settings_get();
        selectorwire::GlobalSettings global;
        if (settings.global().empty() || !global.ParseFromString(settings.global())) { fail("No se pudo leer la configuración de Waywallen."); return; }
        auto *effect = global.mutable_transition();
        effect->set_kind(transition);
        effect->set_duration_ms(500);
        effect->set_angle(0);
        effect->set_origin_x(50);
        effect->set_origin_y(50);
        selectorwire::Request set;
        set.mutable_settings_set()->set_global(global.SerializeAsString());
        for (const auto &plugin : settings.plugins()) set.mutable_settings_set()->add_plugins(plugin);
        request(set, [this, id, target, accent](const selectorwire::Response &) {
            selectorwire::Request apply;
            apply.mutable_wallpaper_apply()->set_wallpaper_id(id.toStdString());
            auto *destination = apply.mutable_wallpaper_apply()->add_targets();
            if (!target["canvas"].toString().isEmpty()) destination->set_canvas_id(target["canvas"].toString().toStdString());
            else destination->set_display_id(target["id"].toString().toULongLong());
            request(apply, [this, id, target, accent](const selectorwire::Response &response) {
                if (!response.has_wallpaper_apply()) { fail("Waywallen no confirmó el cambio de fondo."); return; }
                QSettings().setValue("targetKey", target["key"]);
                if (accent) enableAccent();
                m_currentId = id;
                m_busy = false;
                emit currentIdChanged();
                emit stateChanged();
                emit applied();
            });
        });
    });
}

void Backend::enableAccent() {
    const auto config = KSharedConfig::openConfig("kdeglobals");
    KConfigGroup group(config, "General");
    group.writeEntry("AccentColorFromWallpaper", true, KConfig::Notify);
    config->sync();
    QDBusInterface plasma("org.kde.plasmashell", "/PlasmaShell", "org.kde.PlasmaShell");
    const QString script = QStringLiteral(
        "var ds=desktops(); var changed=0; for(var i=0;i<ds.length;i++){"
        "if(ds[i].wallpaperPlugin==='org.waywallen.kde'){"
        "ds[i].currentConfigGroup=['Wallpaper','org.waywallen.kde','General'];"
        "ds[i].writeConfig('AccentColorFromWallpaper',true); ds[i].reloadConfig(); changed++;}} print(changed);");
    auto *watcher = new QDBusPendingCallWatcher(plasma.asyncCall("evaluateScript", script), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        QDBusPendingReply<QString> reply = *watcher;
        if (reply.isError() || reply.value().trimmed() == "0") {
            const QString message = "El fondo cambió, pero no se pudo activar el acento automático de Plasma.";
            fail(message);
            emit accentWarning(message);
        }
        watcher->deleteLater();
    });
}
