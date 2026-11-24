#include "ValidationBulkFixPlanner.h"

#include <QHash>
#include <QSet>

namespace TherionStudio
{
QVector<ValidationBulkFixFileBatch> ValidationBulkFixPlanner::plan(
    const QVector<TherionSourceDiagnostic> &diagnostics,
    const QVector<QString> &diagnosticFilePaths,
    const QString &diagnosticCode)
{
    QVector<ValidationBulkFixFileBatch> batches;
    QHash<QString, int> batchIndexByPath;
    QHash<QString, QSet<int>> startOffsetsByPath;
    for (int index = 0; index < diagnostics.size(); ++index) {
        const TherionSourceDiagnostic &diagnostic = diagnostics.at(index);
        if (diagnostic.code != diagnosticCode || !diagnostic.hasFix || diagnostic.fix.expectedSourceDigest.isEmpty()) {
            continue;
        }
        const QString filePath = index < diagnosticFilePaths.size() ? diagnosticFilePaths.at(index) : QString();
        QSet<int> &startOffsets = startOffsetsByPath[filePath];
        if (startOffsets.contains(diagnostic.fix.startOffset)) {
            continue;
        }
        startOffsets.insert(diagnostic.fix.startOffset);
        auto it = batchIndexByPath.constFind(filePath);
        if (it == batchIndexByPath.constEnd()) {
            it = batchIndexByPath.insert(filePath, batches.size());
            batches.append(ValidationBulkFixFileBatch{filePath, {}});
        }
        batches[it.value()].fixes.append(diagnostic.fix);
    }
    return batches;
}

int ValidationBulkFixPlanner::fixCount(const QVector<ValidationBulkFixFileBatch> &batches)
{
    int count = 0;
    for (const ValidationBulkFixFileBatch &batch : batches) {
        count += batch.fixes.size();
    }
    return count;
}
}
