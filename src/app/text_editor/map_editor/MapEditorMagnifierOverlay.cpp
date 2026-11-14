#include "MapEditorMagnifierOverlay.h"

#include "MapEditorSceneThemePolicy.h"

#include <QCoreApplication>
#include <QFont>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QRegion>
#include <QtMath>

namespace TherionStudio
{
namespace
{
constexpr int kOverlayWidth = 188;
constexpr int kOverlayHeight = 212;
constexpr int kOverlayMargin = 12;
constexpr int kContentMargin = 10;
constexpr int kCaptionHeight = 24;
constexpr int kSourceViewportSize = 84;
}

MapEditorMagnifierOverlay::MapEditorMagnifierOverlay(QGraphicsView *view, QWidget *parent)
    : QWidget(parent)
    , view_(view)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(kOverlayWidth, kOverlayHeight);
    QPainterPath maskPath;
    maskPath.addRoundedRect(QRectF(0.0, 0.0, kOverlayWidth, kOverlayHeight), 12.0, 12.0);
    setMask(QRegion(maskPath.toFillPolygon().toPolygon()));
    syncSceneConnection();
    hide();
}

void MapEditorMagnifierOverlay::syncSceneConnection()
{
    QGraphicsScene *scene = view_ != nullptr ? view_->scene() : nullptr;
    if (scene == connectedScene_) {
        return;
    }
    if (sceneChangedConnection_) {
        QObject::disconnect(sceneChangedConnection_);
        sceneChangedConnection_ = QMetaObject::Connection();
    }
    connectedScene_ = scene;
    if (connectedScene_ != nullptr) {
        sceneChangedConnection_ = QObject::connect(connectedScene_, &QGraphicsScene::changed, this, [this]() {
            invalidateContent();
        });
    }
    invalidateContent();
}

void MapEditorMagnifierOverlay::invalidateContent()
{
    ++contentGeneration_;
}

void MapEditorMagnifierOverlay::setView(QGraphicsView *view)
{
    view_ = view;
    syncSceneConnection();
    updatePlacement();
    update();
}

void MapEditorMagnifierOverlay::setCursorPosition(const QPoint &viewportPosition,
                                                  const QPointF &scenePosition,
                                                  const QString &coordinateText)
{
    if (hasCursorPosition_
        && viewportPosition_ == viewportPosition
        && scenePosition_ == scenePosition
        && coordinateText_ == coordinateText) {
        return;
    }
    viewportPosition_ = viewportPosition;
    scenePosition_ = scenePosition;
    coordinateText_ = coordinateText;
    hasCursorPosition_ = true;
    update();
}

void MapEditorMagnifierOverlay::setOverlayActive(bool active)
{
    syncSceneConnection();
    const bool wasVisible = isVisible();
    active_ = active;
    const bool shouldBeVisible = active_ && hasCursorPosition_ && view_ != nullptr && view_->scene() != nullptr;
    if (shouldBeVisible != wasVisible) {
        setVisible(shouldBeVisible);
        if (shouldBeVisible) {
            raise();
            update();
        }
    }
}

void MapEditorMagnifierOverlay::updatePlacement()
{
    QWidget *parent = parentWidget();
    if (parent == nullptr) {
        return;
    }

    QRect anchorRect = parent->rect();
    if (view_ != nullptr && parent == view_ && view_->viewport() != nullptr) {
        anchorRect = view_->viewport()->geometry();
    }

    const int x = qMax(anchorRect.left() + kOverlayMargin, anchorRect.right() - width() - kOverlayMargin + 1);
    const int y = anchorRect.top() + kOverlayMargin;
    move(x, y);
    raise();
}

QRect MapEditorMagnifierOverlay::contentRect() const
{
    return rect().adjusted(kContentMargin,
                           kContentMargin,
                           -kContentMargin,
                           -kContentMargin - kCaptionHeight);
}

QRectF MapEditorMagnifierOverlay::sceneSourceRect() const
{
    if (view_ == nullptr) {
        return QRectF();
    }

    const QRect viewportSourceRect(viewportPosition_.x() - (kSourceViewportSize / 2),
                                   viewportPosition_.y() - (kSourceViewportSize / 2),
                                   kSourceViewportSize,
                                   kSourceViewportSize);
    return view_->mapToScene(viewportSourceRect).boundingRect();
}

void MapEditorMagnifierOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    if (view_ == nullptr || view_->scene() == nullptr || !hasCursorPosition_) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QPalette appPalette = palette();
    const QColor panelColor = appPalette.color(QPalette::Window);
    const QColor borderColor = appPalette.color(QPalette::Mid);
    const QColor textColor = appPalette.color(QPalette::WindowText);
    QColor crosshairColor = QColor(QStringLiteral("#ffda00"));
    crosshairColor.setAlpha(220);

    painter.fillRect(rect(), panelColor);
    const QRect panelRect = rect().adjusted(1, 1, -1, -1);
    QPainterPath panelPath;
    panelPath.addRoundedRect(panelRect, 12, 12);
    painter.setPen(QPen(borderColor, 1.0));
    painter.drawPath(panelPath);

    const QRect lensRect = contentRect();
    const QRectF sourceRect = sceneSourceRect();
    const QTransform viewTransform = view_->transform();
    const qreal dpr = devicePixelRatioF();
    const QSize targetPixelSize(qMax(1, qCeil(lensRect.width() * dpr)),
                                qMax(1, qCeil(lensRect.height() * dpr)));
    if (sourceRect.isValid()) {
        const bool cacheValid = !cachedLensPixmap_.isNull()
            && cachedLensPixmap_.size() == targetPixelSize
            && cachedGeneration_ == contentGeneration_
            && cachedSourceRect_ == sourceRect
            && cachedViewTransform_ == viewTransform
            && qFuzzyCompare(cachedDpr_, dpr);
        if (!cacheValid) {
            QPixmap rendered(targetPixelSize);
            rendered.setDevicePixelRatio(dpr);
            rendered.fill(mapEditorCanvasViewportBackgroundColor());
            QPainter lensPainter(&rendered);
            lensPainter.setRenderHint(QPainter::Antialiasing, true);
            view_->scene()->render(&lensPainter,
                                   QRectF(0.0, 0.0, lensRect.width(), lensRect.height()),
                                   sourceRect,
                                   Qt::KeepAspectRatioByExpanding);
            cachedLensPixmap_ = rendered;
            cachedGeneration_ = contentGeneration_;
            cachedSourceRect_ = sourceRect;
            cachedViewTransform_ = viewTransform;
            cachedDpr_ = dpr;
        }
    }

    painter.save();
    QPainterPath clipPath;
    clipPath.addRoundedRect(lensRect, 8, 8);
    painter.setClipPath(clipPath);
    if (sourceRect.isValid() && !cachedLensPixmap_.isNull()) {
        painter.drawPixmap(lensRect.topLeft(), cachedLensPixmap_);
    } else {
        painter.fillRect(lensRect, mapEditorCanvasViewportBackgroundColor());
    }
    painter.restore();

    painter.setPen(QPen(borderColor, 1.0));
    painter.drawRoundedRect(lensRect, 8, 8);

    const QPoint center = lensRect.center();
    painter.setPen(QPen(QColor(0, 0, 0, 140), 3.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPoint(center.x() - 14, center.y()), QPoint(center.x() + 14, center.y()));
    painter.drawLine(QPoint(center.x(), center.y() - 14), QPoint(center.x(), center.y() + 14));
    painter.setPen(QPen(crosshairColor, 1.4, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPoint(center.x() - 14, center.y()), QPoint(center.x() + 14, center.y()));
    painter.drawLine(QPoint(center.x(), center.y() - 14), QPoint(center.x(), center.y() + 14));

    painter.setPen(textColor);
    QFont captionFont = painter.font();
    captionFont.setPointSizeF(qMax<qreal>(9.0, captionFont.pointSizeF() - 1.0));
    captionFont.setBold(true);
    painter.setFont(captionFont);
    const QRect captionRect(kContentMargin,
                            lensRect.bottom() + 4,
                            width() - (2 * kContentMargin),
                            kCaptionHeight - 4);
    painter.drawText(captionRect,
                     Qt::AlignCenter,
                     coordinateText_.isEmpty()
                         ? QCoreApplication::translate(
                               "TherionStudio::MapEditorMagnifierOverlay", "Cursor")
                         : coordinateText_);
}
}
