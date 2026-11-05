// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QQuickPaintedItem>
#include <QImage>
#include <QUrl>

class SlantedPreview : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool selected READ selected WRITE setSelected NOTIFY selectedChanged)
public:
    explicit SlantedPreview(QQuickItem *parent = nullptr);
    QUrl source() const { return m_source; }
    bool selected() const { return m_selected; }
    void setSource(const QUrl &source);
    void setSelected(bool value);
    void paint(QPainter *painter) override;
    bool contains(const QPointF &point) const override;
signals:
    void sourceChanged();
    void selectedChanged();
private:
    QImage m_image;
    QUrl m_source;
    quint64 m_generation = 0;
    bool m_selected = false;
};
