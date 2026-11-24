#pragma once

#include "../core/TherionSourceValidator.h"

#include <QString>
#include <QVector>

namespace TherionStudio
{
struct ValidationBulkFixFileBatch
{
    QString filePath;
    QVector<TherionSourceDiagnosticFix> fixes;
};

// Groups safe validation fixes that share one diagnostic code into per-file batches, so each file can be fixed
// in one source transaction. Batches keep first-seen file order; duplicate fix ranges are dropped.
class ValidationBulkFixPlanner final
{
public:
    [[nodiscard]] static QVector<ValidationBulkFixFileBatch> plan(
        const QVector<TherionSourceDiagnostic> &diagnostics,
        const QVector<QString> &diagnosticFilePaths,
        const QString &diagnosticCode);
    [[nodiscard]] static int fixCount(const QVector<ValidationBulkFixFileBatch> &batches);
};
}
