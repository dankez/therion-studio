#pragma once

#include "MapEditorObjectStyleCatalog.h"

#include <QColor>
#include <QCache>
#include <QGraphicsPathItem>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QTransform>
#include <QVector>

#include <array>
#include <optional>

namespace TherionStudio
{
struct MapEditorLineDecorationVertex
{
    QPointF anchor;
    std::optional<qreal> pathDistance;
    std::optional<qreal> orientationDegrees;
    std::optional<qreal> leftSize;
};

struct MapEditorLineDecorationVertexMetric
{
    qreal distance = 0.0;
    std::optional<qreal> orientationDegrees;
    std::optional<qreal> leftSize;
};

class MapEditorLineDecorationItem final : public QGraphicsPathItem
{
public:
    MapEditorLineDecorationItem(const QPainterPath &path,
                                const QVector<MapEditorLineDecorationStyle> &decorations,
                                const QColor &fallbackColor,
                                bool reversed,
                                int fallbackSeed,
                                const QVector<MapEditorLineDecorationVertex> &lineVertices = {},
                                qreal linePointLengthScale = 1.0,
                                QGraphicsItem *parent = nullptr);

    void setDecorationPath(const QPainterPath &path);
    void setDecorationPath(const QPainterPath &path,
                           const QVector<MapEditorLineDecorationVertex> &lineVertices);
    bool usesLineVertexMetrics() const;
    void updateViewTransform(const QTransform &viewTransform);
    QRectF boundingRect() const override;
    QRectF sourceGeometryBounds() const;
    qreal hitDistancePixels(const QPointF &scenePosition, const QTransform &viewTransform) const;

protected:
    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget = nullptr) override;

private:
    struct ProjectedSample
    {
        QPointF point;
        QPointF normal;
    };

    struct ProjectedPathCacheEntry
    {
        QTransform transform;
        QList<QPolygonF> polygons;
        QVector<QVector<qreal>> cumulativeDistances;
        mutable QCache<QPair<qsizetype, qreal>, ProjectedSample> samples{128};
        bool valid = false;
    };

    void updatePaintBounds();
    qreal cachedPathLength() const;
    const QVector<MapEditorLineDecorationVertexMetric> &cachedSourceMetrics() const;
    std::optional<QPointF> cachedSourceNormal(qreal distance) const;
    ProjectedSample cachedProjectedSample(const ProjectedPathCacheEntry &cache,
                                         qsizetype polylineIndex, qreal distance) const;
    const ProjectedPathCacheEntry &projectedPolygonsForProjection(const QTransform &projection) const;
    void drawDecorations(QPainter *painter,
                         qreal zoomOutScale,
                         const QTransform &projection,
                         const QPointF &projectionOffset,
                         const ProjectedPathCacheEntry &cache);
    void drawScreenDecoration(QPainter *painter,
                              const MapEditorLineDecorationStyle &decoration,
                              qreal zoomOutScale,
                              const QTransform &projection,
                              const QPointF &projectionOffset,
                              const ProjectedPathCacheEntry &cache);
    void drawTick(QPainter *painter,
                  const MapEditorLineDecorationStyle &decoration,
                  const QPointF &point,
                  const QPointF &normal,
                  int markerIndex);
    void drawRung(QPainter *painter,
                  const MapEditorLineDecorationStyle &decoration,
                  const QPointF &point,
                  const QPointF &normal);
    void drawSymbol(QPainter *painter,
                    const MapEditorLineDecorationStyle &decoration,
                    const QPointF &point,
                    const QPointF &tangent,
                    const QPointF &normal,
                    int markerIndex,
                    qreal zoomOutScale);
    void drawWave(QPainter *painter,
                  const MapEditorLineDecorationStyle &decoration,
                  const QPointF &point,
                  const QPointF &tangent,
                  const QPointF &normal,
                  int markerIndex);

    QVector<MapEditorLineDecorationStyle> decorations_;
    QVector<MapEditorLineDecorationVertex> lineVertices_;
    QColor fallbackColor_;
    bool reversed_ = false;
    int fallbackSeed_ = 0;
    qreal linePointLengthScale_ = 1.0;
    QRectF paintBounds_;
    std::optional<QColor> interactionColorOverride_;
    qreal screenPaddingScale_ = 2.0;
    mutable QPainterPath projectedPathSource_;
    mutable std::array<ProjectedPathCacheEntry, 2> projectedPathCache_;
    mutable std::size_t mostRecentProjection_ = 0;
    mutable qreal cachedPathLength_ = -1.0;
    mutable QVector<MapEditorLineDecorationVertexMetric> cachedSourceMetrics_;
    mutable bool cachedSourceMetricsValid_ = false;
    mutable QCache<qreal, std::optional<QPointF>> sourceNormals_{128};
};

void updateMapEditorScreenSpaceMetrics(QGraphicsScene *scene, const QTransform &viewTransform);
}
