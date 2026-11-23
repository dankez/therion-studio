#pragma once

#include "TextEditorDocumentPersistenceStateService.h"

#include <QString>

namespace TherionStudio
{
class IFileSystem;

class TextEditorDocumentIoService final
{
public:
    struct ReadDocumentResult
    {
        QString contents;
        QString loadedEncodingName;
        QString loadedEncodingLabel;
    };

    struct BuildLoadStateInputRequest
    {
        QString filePath;
        QString contents;
        QString loadedEncodingName;
        QString loadedEncodingLabel;
        int cursorLineNumber = 1;
        int cursorColumnNumber = 1;
        bool blocksModeActive = false;
        bool blocksModeSupportedForCurrentFile = true;
        QString openedEncodingStatusTemplate;
    };

    struct BuildSaveStateInputRequest
    {
        QString textContents;
        QString fileEncodingName;
        QString fileEncodingLabel;
        QString savedEncodingStatusTemplate;
    };

    static bool readDocument(IFileSystem &fileSystem,
                             const QString &filePath,
                             QString *errorMessage,
                             ReadDocumentResult *result);

    static bool writeDocument(IFileSystem &fileSystem,
                              const QString &filePath,
                              const QString &textContents,
                              const QString &fileEncodingName,
                              QString *errorMessage);

    static QString contentsWithPreservedLineEndings(const QString &editorText,
                                                    const QString &originalContents);

    static TextEditorDocumentPersistenceStateService::LoadStateInput buildLoadStateInput(
        const BuildLoadStateInputRequest &request);

    static TextEditorDocumentPersistenceStateService::SaveStateInput buildSaveStateInput(
        const BuildSaveStateInputRequest &request);
};
}
