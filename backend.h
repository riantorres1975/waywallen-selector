// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QObject>
#include <QVariantList>
#include <QWebSocket>
#include <QHash>
#include <functional>
#include "protocol.pb.h"

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList wallpapers READ wallpapers NOTIFY wallpapersChanged)
    Q_PROPERTY(QVariantList displays READ displays NOTIFY displaysChanged)
    Q_PROPERTY(int total READ total CONSTANT)
    Q_PROPERTY(int targetIndex READ targetIndex WRITE setTargetIndex NOTIFY targetIndexChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool previewOnly READ previewOnly CONSTANT)
    Q_PROPERTY(QString currentId READ currentId NOTIFY currentIdChanged)
public:
    explicit Backend(bool previewOnly = false, QObject *parent = nullptr);
    QVariantList wallpapers() const { return m_filtered; }
    QVariantList displays() const { return m_displays; }
    int total() const { return m_all.size(); }
    int targetIndex() const { return m_targetIndex; }
    QString search() const { return m_search; }
    QString error() const { return m_error; }
    bool ready() const { return m_ready; }
    bool busy() const { return m_busy; }
    bool previewOnly() const { return m_previewOnly; }
    QString currentId() const { return m_currentId; }
    void setSearch(const QString &value);
    void setTargetIndex(int index);
    void preferScreen(const QString &model);
    Q_INVOKABLE void apply(const QString &id, int transition, bool accent, bool appColors = false);
signals:
    void wallpapersChanged();
    void displaysChanged();
    void targetIndexChanged();
    void searchChanged();
    void errorChanged();
    void stateChanged();
    void currentIdChanged();
    void applied();
    void accentWarning(const QString &message);
    void appearanceWarning(const QString &message);
private:
    using Callback = std::function<void(const selectorwire::Response &)>;
    void loadLibrary();
    void connectDaemon();
    void receive(const QByteArray &data);
    void request(selectorwire::Request req, Callback callback);
    void fail(const QString &message);
    void enableAccent();
    void syncColors(const QString &id, std::function<void()> finished);
    QVariantList m_all, m_filtered, m_displays;
    QString m_search, m_error, m_currentId, m_screenModel;
    QWebSocket m_socket;
    QHash<quint64, Callback> m_callbacks;
    quint64 m_requestId = 0;
    int m_targetIndex = -1;
    bool m_ready = false, m_busy = false;
    bool m_previewOnly = false;
};
