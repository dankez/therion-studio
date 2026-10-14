#include "../src/app/text_editor/TextEditorSourceRewriteController.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QPlainTextEdit>
#include <QTextDocument>

#include <iostream>

using namespace TherionStudio;

namespace
{
bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

void pumpEvents()
{
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

int runSystemNormalizationApplyUndoRedoTest()
{
    const QString beforeText = QStringLiteral("survey demo\nendsurvey\n");
    const QString afterText = QStringLiteral("encoding utf-8\nsurvey demo\nendsurvey\n");

    bool loading = false;
    int currentLineNumber = 1;
    int dirtyStateCalls = 0;
    int rebuildCalls = 0;
    int textChangedCalls = 0;
    int contextHelpCalls = 0;

    QPlainTextEdit editor;
    editor.setPlainText(beforeText);

    TextEditorSourceRewriteContext context;
    context.editor = &editor;
    context.loading = &loading;
    context.currentLineNumber = &currentLineNumber;
    context.applyDirtyStateFromCurrentState = [&dirtyStateCalls]() {
        ++dirtyStateCalls;
    };
    context.rebuildBlocksCanvasFromText = [&rebuildCalls]() {
        ++rebuildCalls;
    };
    context.documentTextChanged = [&textChangedCalls]() {
        ++textChangedCalls;
    };
    context.updateContextHelp = [&contextHelpCalls]() {
        ++contextHelpCalls;
    };

    TextEditorSourceRewriteController controller(std::move(context));

    const TextEditorSourceTransactionResult result = controller.replaceTextForSystemNormalizationResult(afterText);
    pumpEvents();
    if (!expect(result == TextEditorSourceTransactionResult::Applied,
                "System normalization transaction should apply when encoding directive is missing.")) {
        return 1;
    }

    if (!expect(editor.toPlainText() == afterText,
                "System normalization transaction should insert a root encoding directive.")) {
        return 1;
    }

    if (!expect(editor.document() != nullptr && editor.document()->isUndoAvailable(),
                "System normalization transaction should produce an undoable change.")) {
        return 1;
    }

    if (!expect(dirtyStateCalls == 1,
                "System normalization transaction should refresh dirty state exactly once.")) {
        return 1;
    }

    if (!expect(rebuildCalls == 1,
                "System normalization transaction should trigger one projection refresh.")) {
        return 1;
    }

    if (!expect(textChangedCalls == 1,
                "System normalization transaction should emit one document-text-changed callback.")) {
        return 1;
    }

    if (!expect(contextHelpCalls == 1,
                "System normalization transaction should refresh context help once.")) {
        return 1;
    }

    editor.undo();
    pumpEvents();
    if (!expect(editor.toPlainText() == beforeText,
                "System normalization undo should restore the original source text.")) {
        return 1;
    }

    editor.redo();
    pumpEvents();
    if (!expect(editor.toPlainText() == afterText,
                "System normalization redo should reapply the normalized source text.")) {
        return 1;
    }

    return 0;
}

int runSystemNormalizationNoOpTest()
{
    const QString textWithEncoding = QStringLiteral("encoding utf-8\nsurvey demo\nendsurvey\n");

    bool loading = false;
    int currentLineNumber = 1;
    int dirtyStateCalls = 0;
    int rebuildCalls = 0;
    int textChangedCalls = 0;
    int contextHelpCalls = 0;

    QPlainTextEdit editor;
    editor.setPlainText(textWithEncoding);

    TextEditorSourceRewriteContext context;
    context.editor = &editor;
    context.loading = &loading;
    context.currentLineNumber = &currentLineNumber;
    context.applyDirtyStateFromCurrentState = [&dirtyStateCalls]() {
        ++dirtyStateCalls;
    };
    context.rebuildBlocksCanvasFromText = [&rebuildCalls]() {
        ++rebuildCalls;
    };
    context.documentTextChanged = [&textChangedCalls]() {
        ++textChangedCalls;
    };
    context.updateContextHelp = [&contextHelpCalls]() {
        ++contextHelpCalls;
    };

    TextEditorSourceRewriteController controller(std::move(context));

    const TextEditorSourceTransactionResult result =
        controller.replaceTextForSystemNormalizationResult(textWithEncoding);
    pumpEvents();
    if (!expect(result == TextEditorSourceTransactionResult::NoChange,
                "System normalization should report no-change when source is already normalized.")) {
        return 1;
    }

    if (!expect(editor.toPlainText() == textWithEncoding,
                "System normalization no-op should preserve source text exactly.")) {
        return 1;
    }

    if (!expect(dirtyStateCalls == 0 && rebuildCalls == 0 && textChangedCalls == 0 && contextHelpCalls == 0,
                "System normalization no-op should not trigger update callbacks.")) {
        return 1;
    }

    return 0;
}

int runEditorUndoTransactionStaleRevisionTest()
{
    const QString beforeText = QStringLiteral("survey demo\nendsurvey\n");
    const QString afterText = QStringLiteral("survey changed\nendsurvey\n");

    bool loading = false;
    int currentLineNumber = 1;
    int dirtyStateCalls = 0;
    int rebuildCalls = 0;
    int textChangedCalls = 0;
    int contextHelpCalls = 0;

    QPlainTextEdit editor;
    editor.setPlainText(beforeText);
    const int expectedRevision = editor.document() != nullptr ? editor.document()->revision() : 0;
    editor.setPlainText(QStringLiteral("survey concurrent\nendsurvey\n"));

    TextEditorSourceRewriteContext context;
    context.editor = &editor;
    context.loading = &loading;
    context.currentLineNumber = &currentLineNumber;
    context.applyDirtyStateFromCurrentState = [&dirtyStateCalls]() {
        ++dirtyStateCalls;
    };
    context.rebuildBlocksCanvasFromText = [&rebuildCalls]() {
        ++rebuildCalls;
    };
    context.documentTextChanged = [&textChangedCalls]() {
        ++textChangedCalls;
    };
    context.updateContextHelp = [&contextHelpCalls]() {
        ++contextHelpCalls;
    };

    TextEditorSourceRewriteController controller(std::move(context));

    TextEditorSourceTransactionRequest request;
    request.label = QStringLiteral("Stale Rewrite");
    request.beforeText = beforeText;
    request.afterText = afterText;
    request.expectedSourceRevision = expectedRevision;
    request.staleStatusMessage = QStringLiteral("rewrite stale");

    QString statusMessage;
    const TextEditorSourceTransactionResult result =
        controller.applyTransactionRequestWithEditorUndoResult(request, &statusMessage);
    pumpEvents();

    if (!expect(result == TextEditorSourceTransactionResult::Stale,
                "Editor undo transaction should report a stale result for revision drift.")) {
        return 1;
    }
    if (!expect(statusMessage == QStringLiteral("rewrite stale"),
                "Editor undo transaction should return the stale status message.")) {
        return 1;
    }
    if (!expect(editor.toPlainText() == QStringLiteral("survey concurrent\nendsurvey\n"),
                "Stale editor undo transaction should preserve concurrent source text.")) {
        return 1;
    }
    if (!expect(dirtyStateCalls == 0 && rebuildCalls == 0 && textChangedCalls == 0 && contextHelpCalls == 0,
                "Stale editor undo transaction should not trigger update callbacks.")) {
        return 1;
    }

    return 0;
}

int runSnapshotReplacesOnlyChangedRangeTest()
{
    QStringList lines;
    for (int index = 0; index < 5000; ++index) {
        lines.append(QStringLiteral("point %1 %2 station -name s%3").arg(index).arg(index * 2).arg(index));
    }
    const QString beforeText = lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    lines[2500] = QStringLiteral("point 1 2 station -name moved");
    const QString afterText = lines.join(QLatin1Char('\n')) + QLatin1Char('\n');

    bool loading = false;
    int currentLineNumber = 1;
    QPlainTextEdit editor;
    editor.setPlainText(beforeText);

    TextEditorSourceRewriteContext context;
    context.editor = &editor;
    context.loading = &loading;
    context.currentLineNumber = &currentLineNumber;
    TextEditorSourceRewriteController controller(std::move(context));

    int maxTouched = 0;
    QObject::connect(editor.document(), &QTextDocument::contentsChange,
                     [&maxTouched](int, int removed, int added) {
                         maxTouched = qMax(maxTouched, qMax(removed, added));
                     });

    controller.applySourceSnapshotForTransaction(afterText);
    if (!expect(editor.toPlainText() == afterText,
                "Snapshot apply should produce the exact target text.")) {
        return 1;
    }
    if (!expect(maxTouched > 0 && maxTouched < 100,
                "Snapshot apply should replace only the changed range, not the whole document.")) {
        std::cerr << "touched characters: " << maxTouched << '\n';
        return 1;
    }

    const QString surrogateBefore = QStringLiteral("a") + QString::fromUcs4(U"\U0001F600") + QStringLiteral("b\n");
    const QString surrogateAfter = QStringLiteral("a") + QString::fromUcs4(U"\U0001F601") + QStringLiteral("b\n");
    editor.setPlainText(surrogateBefore);
    controller.applySourceSnapshotForTransaction(surrogateAfter);
    if (!expect(editor.toPlainText() == surrogateAfter,
                "Snapshot apply should keep surrogate pairs intact.")) {
        return 1;
    }

    return 0;
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    if (runSystemNormalizationApplyUndoRedoTest() != 0) {
        return 1;
    }
    if (runSystemNormalizationNoOpTest() != 0) {
        return 1;
    }
    if (runEditorUndoTransactionStaleRevisionTest() != 0) {
        return 1;
    }
    if (runSnapshotReplacesOnlyChangedRangeTest() != 0) {
        return 1;
    }

    return 0;
}
