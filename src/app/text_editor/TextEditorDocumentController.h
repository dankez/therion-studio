#pragma once

#include <functional>

#include <QString>

class QPlainTextEdit;

namespace TherionStudio
{
class IFileSystem;
struct TextEditorDocumentContext
{
    QPlainTextEdit *editor = nullptr;
    QString *filePath = nullptr;
    QString *projectRootPath = nullptr;
    QString *fileEncodingName = nullptr;
    QString *fileEncodingLabel = nullptr;
    QString *encodingStatusNote = nullptr;
    QString *cleanTextSnapshot = nullptr;
    QString *cleanEncodingNameSnapshot = nullptr;
    QString *blockDetailsSelectedKind = nullptr;
    bool *loading = nullptr;
    bool *dirty = nullptr;
    bool *blocksModeActive = nullptr;
    int *currentLineNumber = nullptr;
    int *currentColumnNumber = nullptr;
    int *highlightedLineNumber = nullptr;
    int *blockDetailsSelectedLineNumber = nullptr;
    std::function<QString(const char *)> translate;
    std::function<void()> refreshBlocksModeAvailability;
    std::function<bool()> isBlocksModeSupportedForCurrentFile;
    std::function<void(bool)> setBlocksModeActive;
    std::function<void()> rebuildBlocksCanvasFromText;
    std::function<void()> clearBlockDetailsPane;
    std::function<void()> populateBlockToolbox;
    std::function<void()> refreshEditorModeUi;
    std::function<void()> refreshTitle;
    std::function<void()> refreshCurrentLineHighlight;
    std::function<void(bool)> dirtyStateChanged;
    std::function<void()> updateContextHelp;
    std::function<void()> refreshStatus;
};

class TextEditorDocumentController final
{
public:
    explicit TextEditorDocumentController(IFileSystem &fileSystem, TextEditorDocumentContext context);

    bool loadFile(const QString &filePath, QString *errorMessage = nullptr);
    bool save(QString *errorMessage = nullptr);
    void setProjectRootPath(const QString &projectRootPath);

private:
    QString tr(const char *text) const;

    IFileSystem &fileSystem_;
    TextEditorDocumentContext context_;
    QString loadedSourceContents_;
};
}
