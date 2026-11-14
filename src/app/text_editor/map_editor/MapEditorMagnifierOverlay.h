#pragma once

#include <QMetaObject>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QTransform>
#include <QWidget>

class QGraphicsScene;
class QGraphicsView;

namespace TherionStudio
{
class MapEditorMagnifierOverlay final : public QWidget
{
public:
    explicit MapEditorMagnifierOverlay(QGraphicsView *view, QWidget *parent = nullptr);

    void setView(QGraphicsView *view);
    void setCursorPosition(const QPoint &viewportPosition,
                           const QPointF &scenePosition,
                           const QString &coordinateText);
    void setOverlayActive(bool active);
    void updatePlacement();
    void invalidateContent();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void syncSceneConnection();
    QRect contentRect() const;
    QRectF sceneSourceRect() const;

    QPointer<QGraphicsView> view_;
    QPointer<QGraphicsScene> connectedScene_;
    QMetaObject::Connection sceneChangedConnection_;
    QPoint viewportPosition_;
    QPointF scenePosition_;
    QString coordinateText_;
    bool hasCursorPosition_ = false;
    bool active_ = false;
    quint64 contentGeneration_ = 1;
    quint64 cachedGeneration_ = 0;
    QRectF cachedSourceRect_;
    QTransform cachedViewTransform_;
    qreal cachedDpr_ = 0.0;
    QPixmap cachedLensPixmap_;
};
}
