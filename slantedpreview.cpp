// SPDX-License-Identifier: GPL-3.0-only
#include "slantedpreview.h"
#include <QPainter>
#include <QPainterPath>
#include <QImageReader>
#include <QFutureWatcher>
#include <QtConcurrent>

SlantedPreview::SlantedPreview(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
}

void SlantedPreview::setSource(const QUrl &source) {
    if (source == m_source) return;
    m_source = source;
    m_image = QImage();
    const auto generation = ++m_generation;
    emit sourceChanged();
    update();
    if (!source.isLocalFile()) return;
    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, generation] {
        if (generation == m_generation) { m_image = watcher->result(); update(); }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([path = source.toLocalFile()] {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        if (reader.size().isValid()) reader.setScaledSize(reader.size().scaled(760, 760, Qt::KeepAspectRatio));
        return reader.read(); // First frame only: previews never start new wallpaper renderers.
    }));
}

void SlantedPreview::setSelected(bool value) {
    if (value == m_selected) return;
    m_selected = value;
    emit selectedChanged();
    update();
}

bool SlantedPreview::contains(const QPointF &point) const {
    const qreal w = width(), h = height(), skew = qMin(w * 0.22, 60.0);
    QPainterPath path;
    path.moveTo(skew, 2); path.lineTo(w - 2, 2); path.lineTo(w - skew, h - 2);
    path.lineTo(2, h - 2); path.closeSubpath();
    return path.contains(point);
}

void SlantedPreview::paint(QPainter *painter) {
    const qreal w = width(), h = height(), skew = qMin(w * 0.22, 60.0);
    QPainterPath path;
    path.moveTo(skew, 2); path.lineTo(w - 2, 2); path.lineTo(w - skew, h - 2);
    path.lineTo(2, h - 2); path.closeSubpath();
    painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    painter->save();
    painter->setClipPath(path);
    painter->fillRect(boundingRect(), QColor("#313244"));
    if (!m_image.isNull()) {
        const QSizeF scaled = QSizeF(m_image.size()).scaled(QSizeF(w, h), Qt::KeepAspectRatioByExpanding);
        painter->drawImage(QRectF((w - scaled.width()) / 2, (h - scaled.height()) / 2, scaled.width(), scaled.height()), m_image);
    }
    painter->restore();
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(QColor(m_selected ? "#c061ff" : "#615078"), m_selected ? 2.5 : 1));
    painter->drawPath(path);
}
