#include "../src/app/ValidationBulkFixPlanner.h"

#include <QtTest/QtTest>

using namespace TherionStudio;

namespace
{
const QString kEmptyObjectCode = QStringLiteral("empty-scrap-object");

TherionSourceValidationCatalog basicCatalog()
{
    TherionSourceValidationCatalog catalog;
    catalog.commandNames = {
        QStringLiteral("area"),
        QStringLiteral("endarea"),
        QStringLiteral("endline"),
        QStringLiteral("endscrap"),
        QStringLiteral("line"),
        QStringLiteral("point"),
        QStringLiteral("scrap"),
    };
    return catalog;
}
}

class ValidationBulkFixPlannerTest final : public QObject
{
    Q_OBJECT

private slots:
    void removesAllEmptyObjectsInOneBatchAndKeepsOtherContent()
    {
        const QString contents = QStringLiteral("scrap s\n"
                                                "  area water\n"
                                                "  endarea\n"
                                                "  line wall\n"
                                                "    0 0\n"
                                                "    1 1\n"
                                                "  endline\n"
                                                "  line wall\n"
                                                "  endline\n"
                                                "  area water\n"
                                                "    # keep commented area\n"
                                                "  endarea\n"
                                                "  area water\n"
                                                "\n"
                                                "  endarea\n"
                                                "endscrap\n");
        const TherionSourceValidationResult result = TherionSourceValidator::validate(contents, basicCatalog());

        const QVector<ValidationBulkFixFileBatch> batches =
            ValidationBulkFixPlanner::plan(result.diagnostics, {}, kEmptyObjectCode);
        QCOMPARE(batches.size(), 1);
        QCOMPARE(ValidationBulkFixPlanner::fixCount(batches), 3);
        QCOMPARE(TherionSourceValidator::applyFixes(contents, batches.constFirst().fixes),
                 QStringLiteral("scrap s\n"
                                "  line wall\n"
                                "    0 0\n"
                                "    1 1\n"
                                "  endline\n"
                                "  area water\n"
                                "    # keep commented area\n"
                                "  endarea\n"
                                "endscrap\n"));
    }

    void groupsByFileSkipsOtherCodesAndDuplicates()
    {
        auto diagnostic = [](const QString &code, int startOffset, bool hasFix = true) {
            TherionSourceDiagnostic result;
            result.code = code;
            result.hasFix = hasFix;
            result.fix.startOffset = startOffset;
            result.fix.length = 4;
            result.fix.expectedSourceDigest = QByteArrayLiteral("digest");
            return result;
        };
        const QVector<TherionSourceDiagnostic> diagnostics = {
            diagnostic(kEmptyObjectCode, 10),
            diagnostic(QStringLiteral("unclosed-block"), 20),
            diagnostic(kEmptyObjectCode, 30),
            diagnostic(kEmptyObjectCode, 10),
            diagnostic(kEmptyObjectCode, 40, false),
            diagnostic(kEmptyObjectCode, 50),
        };
        const QVector<QString> paths = {
            QStringLiteral("/p/a.th2"), QStringLiteral("/p/a.th2"), QStringLiteral("/p/b.th2"),
            QStringLiteral("/p/a.th2"), QStringLiteral("/p/a.th2"), QStringLiteral("/p/a.th2"),
        };

        const QVector<ValidationBulkFixFileBatch> batches =
            ValidationBulkFixPlanner::plan(diagnostics, paths, kEmptyObjectCode);
        QCOMPARE(batches.size(), 2);
        QCOMPARE(batches.at(0).filePath, QStringLiteral("/p/a.th2"));
        QCOMPARE(batches.at(0).fixes.size(), 2);
        QCOMPARE(batches.at(0).fixes.at(1).startOffset, 50);
        QCOMPARE(batches.at(1).filePath, QStringLiteral("/p/b.th2"));
        QCOMPARE(ValidationBulkFixPlanner::fixCount(batches), 3);
    }
};

int runValidationBulkFixPlannerTest(int argc, char **argv)
{
    ValidationBulkFixPlannerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "ValidationBulkFixPlannerTest.moc"
