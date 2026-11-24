#pragma once

#include "TherionCompileFiguresLoader.h"
#include "../core/ProjectStructureIndex.h"

#include <QStyledItemDelegate>
#include <QCoreApplication>

class QStandardItemModel;

namespace TherionStudio
{
class StructureScrapDistortion final : public QStyledItemDelegate
{
    Q_DECLARE_TR_FUNCTIONS(StructureScrapDistortion)

public:
    enum class State { Unavailable, Low, Moderate, High };

    explicit StructureScrapDistortion(QObject *parent = nullptr);
    static State classify(const TherionScrapFigures *scrap);
    static void updateModel(QStandardItemModel *model,
                            const QVector<ProjectStructureEntry> &entries,
                            const TherionCompileFigures &figures,
                            bool enabled,
                            bool sourcesChanged);

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override;
};
}
