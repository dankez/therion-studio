#include "StructureScrapDistortion.h"

#include "MainWindowStructureRoles.h"

#include <QApplication>
#include <QPainter>
#include <QStandardItemModel>
#include <QStyleOptionViewItem>
#include <QWidget>

#include <cmath>
#include <functional>

namespace TherionStudio
{
StructureScrapDistortion::StructureScrapDistortion(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

StructureScrapDistortion::State StructureScrapDistortion::classify(const TherionScrapFigures *scrap)
{
    if (scrap == nullptr || !scrap->maximumDistortion
        || !std::isfinite(*scrap->maximumDistortion) || *scrap->maximumDistortion < 0.0) {
        return State::Unavailable;
    }
    if (*scrap->maximumDistortion >= 25.0) {
        return State::High;
    }
    return *scrap->maximumDistortion >= 10.0 ? State::Moderate : State::Low;
}

void StructureScrapDistortion::updateModel(QStandardItemModel *model,
                                           const QVector<ProjectStructureEntry> &entries,
                                           const TherionCompileFigures &figures,
                                           bool enabled,
                                           bool sourcesChanged)
{
    if (model == nullptr) {
        return;
    }
    QHash<QString, QString> surveyNamesById;
    for (const ProjectStructureEntry &entry : entries) {
        if (entry.kind == ProjectStructureEntryKind::Survey) {
            surveyNamesById.insert(entry.objectId, ProjectStructureIndex::surveyFullName(entry));
        }
    }
    QHash<QString, State> states;
    QHash<QString, QString> tooltips;
    const bool available = figures.isAvailable() && figures.database.isAvailable();
    // Out-of-date figures keep their level, like the Structure info panel, but are drawn faded.
    const bool outdated = figures.laterCompileFailed || sourcesChanged;
    for (const ProjectStructureEntry &entry : entries) {
        if (entry.kind != ProjectStructureEntryKind::Scrap) {
            continue;
        }
        const QString key = therionCompiledObjectKey(entry.name, surveyNamesById.value(entry.parentObjectId));
        const auto scrap = figures.database.scrapsByKey.constFind(key);
        const State state = available
            ? classify(scrap != figures.database.scrapsByKey.constEnd() ? &scrap.value() : nullptr)
            : State::Unavailable;
        states.insert(entry.objectId, state);
        QString text = tr("Distortion unavailable. Compile with a database export.");
        if (state != State::Unavailable) {
            QString level;
            switch (state) {
            case State::Low: level = tr("Low"); break;
            case State::Moderate: level = tr("Moderate"); break;
            case State::High: level = tr("High"); break;
            case State::Unavailable: break;
            }
            text = tr("%1 distortion: maximum %2 %. Advisory thresholds: green <10 %, orange 10–<25 %, red >=25 %. "
                      "This is a scrap statistic, not the location or cause of distortion.")
                       .arg(level, QString::number(*scrap->maximumDistortion, 'f', 2));
            if (outdated) {
                text += QLatin1Char('\n')
                    + tr("Out of date: from the last successful compile; sources or a later compile changed since.");
            }
        }
        tooltips.insert(entry.objectId, text);
    }
    std::function<void(QStandardItem *)> visit = [&](QStandardItem *parent) {
        for (int row = 0; row < parent->rowCount(); ++row) {
            QStandardItem *item = parent->child(row);
            if (item == nullptr) {
                continue;
            }
            const QString id = item->data(ObjectIdRole).toString();
            if (states.contains(id)) {
                const QVariant presented = item->data(ScrapDistortionPresentedToolTipRole);
                if (!presented.isValid() || presented.toString() != item->toolTip()) {
                    item->setData(item->toolTip(), ScrapDistortionOriginalToolTipRole);
                }
                const State state = states.value(id);
                item->setData(enabled ? QVariant(static_cast<int>(state)) : QVariant(), ScrapDistortionStateRole);
                item->setData(enabled && outdated && state != State::Unavailable ? QVariant(true) : QVariant(),
                              ScrapDistortionOutdatedRole);
                const QString original = item->data(ScrapDistortionOriginalToolTipRole).toString();
                const QString tooltip = enabled
                    ? (original.isEmpty() ? tooltips.value(id) : original + QLatin1Char('\n') + tooltips.value(id))
                    : original;
                item->setToolTip(tooltip);
                item->setData(tooltip, ScrapDistortionPresentedToolTipRole);
            }
            visit(item);
        }
    };
    visit(model->invisibleRootItem());
}

void StructureScrapDistortion::initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const
{
    QStyledItemDelegate::initStyleOption(option, index);
    const QVariant data = index.data(ScrapDistortionStateRole);
    if (!data.isValid() || option->icon.isNull()) {
        return;
    }
    QColor background;
    switch (static_cast<State>(data.toInt())) {
    case State::Low: background = QColor(37, 145, 70); break;
    case State::Moderate: background = QColor(193, 111, 0); break;
    case State::High: background = QColor(197, 45, 54); break;
    case State::Unavailable: background = QColor(110, 110, 110); break;
    }
    if (index.data(ScrapDistortionOutdatedRole).toBool()) {
        background.setAlpha(110);
    }
    const qreal ratio = option->widget != nullptr ? option->widget->devicePixelRatioF() : qApp->devicePixelRatio();
    const QSize size = option->decorationSize.expandedTo(QSize(16, 16));
    QPixmap pixmap(size * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(QRectF(QPointF(), QSizeF(size)).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    QPixmap glyph = option->icon.pixmap(size);
    {
        QPainter tint(&glyph);
        tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tint.fillRect(glyph.rect(), Qt::white);
    }
    painter.drawPixmap(QRect(2, 2, size.width() - 4, size.height() - 4), glyph);
    painter.end();
    option->icon = QIcon(pixmap);
}
}
