#include "TextEditorDocumentController.h"

#include <QCoreApplication>
#include "TextEditorDocumentIoService.h"
#include "TextEditorDocumentPersistenceStateService.h"
#include "TextEditorDocumentPreconditionsService.h"
#include "TextEditorDocumentWorkflowController.h"

#include "../../core/IFileSystem.h"

#include <QPlainTextEdit>
#include <QTextCursor>
#include <QTextDocument>

#include <utility>

namespace TherionStudio
{
TextEditorDocumentController::TextEditorDocumentController(IFileSystem &fileSystem,
                                                           TextEditorDocumentContext context)
    : fileSystem_(fileSystem)
    , context_(std::move(context))
{
}

QString TextEditorDocumentController::tr(const char *text) const
{
    return QCoreApplication::translate("TherionStudio::TextEditorDocumentController", text);
}

bool TextEditorDocumentController::loadFile(const QString &filePath, QString *errorMessage)
{
    TextEditorDocumentPreconditionsService::LoadPreconditions preconditions;
    preconditions.hasEditor = context_.editor != nullptr;
    preconditions.hasFilePath = context_.filePath != nullptr;
    preconditions.hasFileEncodingName = context_.fileEncodingName != nullptr;
    preconditions.hasFileEncodingLabel = context_.fileEncodingLabel != nullptr;
    preconditions.hasEncodingStatusNote = context_.encodingStatusNote != nullptr;
    preconditions.hasLoadingFlag = context_.loading != nullptr;
    preconditions.hasCurrentLineNumber = context_.currentLineNumber != nullptr;
    preconditions.hasCurrentColumnNumber = context_.currentColumnNumber != nullptr;
    preconditions.hasHighlightedLineNumber = context_.highlightedLineNumber != nullptr;
    preconditions.hasCleanTextSnapshot = context_.cleanTextSnapshot != nullptr;
    preconditions.hasCleanEncodingNameSnapshot = context_.cleanEncodingNameSnapshot != nullptr;
    preconditions.hasDirtyFlag = context_.dirty != nullptr;
    preconditions.hasBlocksModeActiveFlag = context_.blocksModeActive != nullptr;
    preconditions.hasBlockDetailsSelectedLineNumber = context_.blockDetailsSelectedLineNumber != nullptr;
    preconditions.hasBlockDetailsSelectedKind = context_.blockDetailsSelectedKind != nullptr;
    if (!TextEditorDocumentPreconditionsService::canLoad(preconditions)) {
        return false;
    }

    TextEditorDocumentIoService::ReadDocumentResult readResult;
    if (!TextEditorDocumentIoService::readDocument(fileSystem_, filePath, errorMessage, &readResult)) {
        return false;
    }

    (*context_.filePath) = filePath;
    (*context_.loading) = true;
    context_.editor->setPlainText(readResult.contents);
    (*context_.loading) = false;
    loadedSourceContents_ = readResult.contents;
    const QTextCursor cursor = context_.editor->textCursor();

    TextEditorDocumentIoService::BuildLoadStateInputRequest loadInputRequest;
    loadInputRequest.filePath = filePath;
    loadInputRequest.contents = context_.editor->toPlainText();
    loadInputRequest.loadedEncodingName = readResult.loadedEncodingName;
    loadInputRequest.loadedEncodingLabel = readResult.loadedEncodingLabel;
    loadInputRequest.cursorLineNumber = cursor.blockNumber() + 1;
    loadInputRequest.cursorColumnNumber = cursor.positionInBlock() + 1;
    loadInputRequest.blocksModeActive = *context_.blocksModeActive;
    loadInputRequest.blocksModeSupportedForCurrentFile = !context_.isBlocksModeSupportedForCurrentFile
        || context_.isBlocksModeSupportedForCurrentFile();
    loadInputRequest.openedEncodingStatusTemplate = tr("Opened as %1. Save keeps this encoding unless you convert to UTF-8.");
    const auto loadInput = TextEditorDocumentIoService::buildLoadStateInput(loadInputRequest);
    const auto loadUpdate = TextEditorDocumentPersistenceStateService::buildLoadStateUpdate(loadInput);

    (*context_.filePath) = loadUpdate.filePath;
    (*context_.fileEncodingName) = loadUpdate.fileEncodingName;
    (*context_.fileEncodingLabel) = loadUpdate.fileEncodingLabel;
    (*context_.encodingStatusNote) = loadUpdate.encodingStatusNote;
    (*context_.currentLineNumber) = loadUpdate.currentLineNumber;
    (*context_.currentColumnNumber) = loadUpdate.currentColumnNumber;
    (*context_.highlightedLineNumber) = loadUpdate.highlightedLineNumber;
    (*context_.cleanTextSnapshot) = loadUpdate.cleanTextSnapshot;
    (*context_.cleanEncodingNameSnapshot) = loadUpdate.cleanEncodingNameSnapshot;
    context_.editor->document()->setModified(false);
    (*context_.dirty) = loadUpdate.dirty;
    (*context_.blockDetailsSelectedLineNumber) = loadUpdate.blockDetailsSelectedLineNumber;
    (*context_.blockDetailsSelectedKind) = loadUpdate.blockDetailsSelectedKind;

    TextEditorDocumentWorkflowController::LoadActions loadActions;
    loadActions.refreshBlocksModeAvailability = context_.refreshBlocksModeAvailability;
    loadActions.setBlocksModeActive = context_.setBlocksModeActive;
    loadActions.rebuildBlocksCanvasFromText = context_.rebuildBlocksCanvasFromText;
    loadActions.clearBlockDetailsPane = context_.clearBlockDetailsPane;
    loadActions.populateBlockToolbox = context_.populateBlockToolbox;
    loadActions.refreshEditorModeUi = context_.refreshEditorModeUi;
    loadActions.refreshTitle = context_.refreshTitle;
    loadActions.refreshStatus = context_.refreshStatus;
    loadActions.refreshCurrentLineHighlight = context_.refreshCurrentLineHighlight;
    loadActions.dirtyStateChanged = context_.dirtyStateChanged;
    loadActions.updateContextHelp = context_.updateContextHelp;
    TextEditorDocumentWorkflowController::runPostLoadWorkflow(loadUpdate.disableBlocksMode, loadActions);
    return true;
}

bool TextEditorDocumentController::save(QString *errorMessage)
{
    TextEditorDocumentPreconditionsService::SavePreconditions preconditions;
    preconditions.hasEditor = context_.editor != nullptr;
    preconditions.hasFilePath = context_.filePath != nullptr;
    preconditions.hasFileEncodingName = context_.fileEncodingName != nullptr;
    preconditions.hasFileEncodingLabel = context_.fileEncodingLabel != nullptr;
    preconditions.hasEncodingStatusNote = context_.encodingStatusNote != nullptr;
    preconditions.hasCleanTextSnapshot = context_.cleanTextSnapshot != nullptr;
    preconditions.hasCleanEncodingNameSnapshot = context_.cleanEncodingNameSnapshot != nullptr;
    preconditions.hasDirtyFlag = context_.dirty != nullptr;
    preconditions.filePath = context_.filePath == nullptr ? QString() : (*context_.filePath);
    if (!TextEditorDocumentPreconditionsService::canSave(preconditions,
                                                         tr("This document does not have a file path yet."),
                                                         errorMessage)) {
        return false;
    }

    const QString editorText = context_.editor->toPlainText();
    const QString sourceContents = TextEditorDocumentIoService::contentsWithPreservedLineEndings(
        editorText,
        loadedSourceContents_);
    if (!TextEditorDocumentIoService::writeDocument(fileSystem_,
                                                    (*context_.filePath),
                                                    sourceContents,
                                                    (*context_.fileEncodingName),
                                                    errorMessage)) {
        return false;
    }
    loadedSourceContents_ = sourceContents;

    TextEditorDocumentIoService::BuildSaveStateInputRequest saveInputRequest;
    saveInputRequest.textContents = editorText;
    saveInputRequest.fileEncodingName = (*context_.fileEncodingName);
    saveInputRequest.fileEncodingLabel = (*context_.fileEncodingLabel);
    saveInputRequest.savedEncodingStatusTemplate = tr("Saved using %1 encoding.");
    const auto saveInput = TextEditorDocumentIoService::buildSaveStateInput(saveInputRequest);
    const auto saveUpdate = TextEditorDocumentPersistenceStateService::buildSaveStateUpdate(saveInput);

    (*context_.cleanTextSnapshot) = saveUpdate.cleanTextSnapshot;
    (*context_.cleanEncodingNameSnapshot) = saveUpdate.cleanEncodingNameSnapshot;
    context_.editor->document()->setModified(false);
    (*context_.dirty) = saveUpdate.dirty;
    (*context_.encodingStatusNote) = saveUpdate.encodingStatusNote;
    TextEditorDocumentWorkflowController::SaveActions saveActions;
    saveActions.refreshTitle = context_.refreshTitle;
    saveActions.refreshStatus = context_.refreshStatus;
    saveActions.dirtyStateChanged = context_.dirtyStateChanged;
    TextEditorDocumentWorkflowController::runPostSaveWorkflow(saveActions);
    return true;
}

void TextEditorDocumentController::setProjectRootPath(const QString &projectRootPath)
{
    if (context_.projectRootPath == nullptr) {
        return;
    }

    (*context_.projectRootPath) = projectRootPath;
    TextEditorDocumentWorkflowController::ProjectRootActions actions;
    actions.refreshStatus = context_.refreshStatus;
    TextEditorDocumentWorkflowController::runPostProjectRootSetWorkflow(actions);
}
}
