#include "TextEditorDocumentIoService.h"

#include "../../core/IFileSystem.h"
#include "../../core/TherionSourceText.h"

#include <QHash>

#include <algorithm>

namespace TherionStudio
{
namespace
{
struct LineMatch
{
    int originalIndex = 0;
    int editedIndex = 0;
};

struct LineRange
{
    int originalBegin = 0;
    int originalEnd = 0;
    int editedBegin = 0;
    int editedEnd = 0;
};

QString normalizedEditorText(const TherionSourceText &source)
{
    QString text;
    const QVector<TherionSourceLine> &lines = source.physicalLines();
    for (int index = 0; index < lines.size(); ++index) {
        if (index > 0) {
            text.append(QLatin1Char('\n'));
        }
        text.append(lines.at(index).text);
    }
    return text;
}

void preserveLineEndingAt(const QVector<TherionSourceLine> &originalLines,
                          int originalIndex,
                          int editedIndex,
                          const QString &preferredEnding,
                          QVector<QString> *editedEndings)
{
    if (editedIndex < editedEndings->size() - 1) {
        const QString &originalEnding = originalLines.at(originalIndex).lineEnding;
        (*editedEndings)[editedIndex] = originalEnding.isEmpty() ? preferredEnding : originalEnding;
    }
}

QVector<LineMatch> uniqueLineAnchors(const QVector<TherionSourceLine> &originalLines,
                                     const QVector<TherionSourceLine> &editedLines,
                                     const LineRange &range)
{
    struct Occurrence
    {
        int index = 0;
        int count = 0;
    };

    QHash<QString, Occurrence> originalOccurrences;
    QHash<QString, Occurrence> editedOccurrences;
    for (int index = range.originalBegin; index < range.originalEnd; ++index) {
        Occurrence &occurrence = originalOccurrences[originalLines.at(index).text];
        occurrence.index = index;
        ++occurrence.count;
    }
    for (int index = range.editedBegin; index < range.editedEnd; ++index) {
        Occurrence &occurrence = editedOccurrences[editedLines.at(index).text];
        occurrence.index = index;
        ++occurrence.count;
    }

    QVector<LineMatch> candidates;
    for (int index = range.originalBegin; index < range.originalEnd; ++index) {
        const QString &text = originalLines.at(index).text;
        const Occurrence original = originalOccurrences.value(text);
        const Occurrence edited = editedOccurrences.value(text);
        if (original.count == 1 && edited.count == 1) {
            candidates.append(LineMatch{index, edited.index});
        }
    }

    QVector<int> tails;
    QVector<int> tailCandidateIndexes;
    QVector<int> previous(candidates.size(), -1);
    for (int index = 0; index < candidates.size(); ++index) {
        const int editedIndex = candidates.at(index).editedIndex;
        const auto position = std::lower_bound(tails.cbegin(), tails.cend(), editedIndex);
        const int tailIndex = static_cast<int>(position - tails.cbegin());
        if (tailIndex > 0) {
            previous[index] = tailCandidateIndexes.at(tailIndex - 1);
        }
        if (position == tails.cend()) {
            tails.append(editedIndex);
            tailCandidateIndexes.append(index);
        } else {
            tails[tailIndex] = editedIndex;
            tailCandidateIndexes[tailIndex] = index;
        }
    }

    QVector<LineMatch> anchors;
    if (!tailCandidateIndexes.isEmpty()) {
        int candidateIndex = tailCandidateIndexes.constLast();
        while (candidateIndex >= 0) {
            anchors.append(candidates.at(candidateIndex));
            candidateIndex = previous.at(candidateIndex);
        }
        std::reverse(anchors.begin(), anchors.end());
    }
    return anchors;
}

void preserveMatchedLineEndings(const QVector<TherionSourceLine> &originalLines,
                                const QVector<TherionSourceLine> &editedLines,
                                const QString &preferredEnding,
                                QVector<QString> *editedEndings)
{
    QVector<LineRange> pending{{0,
                                static_cast<int>(originalLines.size()),
                                0,
                                static_cast<int>(editedLines.size())}};
    while (!pending.isEmpty()) {
        LineRange range = pending.takeLast();
        QVector<LineMatch> suffix;
        while (range.originalBegin < range.originalEnd
               && range.editedBegin < range.editedEnd
               && originalLines.at(range.originalEnd - 1).text == editedLines.at(range.editedEnd - 1).text) {
            --range.originalEnd;
            --range.editedEnd;
            suffix.append(LineMatch{range.originalEnd, range.editedEnd});
        }

        while (range.originalBegin < range.originalEnd
               && range.editedBegin < range.editedEnd
               && originalLines.at(range.originalBegin).text == editedLines.at(range.editedBegin).text) {
            preserveLineEndingAt(originalLines,
                                 range.originalBegin,
                                 range.editedBegin,
                                 preferredEnding,
                                 editedEndings);
            ++range.originalBegin;
            ++range.editedBegin;
        }

        // Unique unchanged rows anchor the diff; positional pairing keeps endings on edited rows.
        const QVector<LineMatch> anchors = uniqueLineAnchors(originalLines, editedLines, range);
        if (anchors.isEmpty()) {
            // Positional pairing preserves an edited row's separator when no unchanged anchor is available.
            const int pairedCount = qMin(range.originalEnd - range.originalBegin,
                                         range.editedEnd - range.editedBegin);
            for (int index = 0; index < pairedCount; ++index) {
                preserveLineEndingAt(originalLines,
                                     range.originalBegin + index,
                                     range.editedBegin + index,
                                     preferredEnding,
                                     editedEndings);
            }
        } else {
            QVector<LineRange> gaps;
            int previousOriginal = range.originalBegin;
            int previousEdited = range.editedBegin;
            for (const LineMatch &anchor : anchors) {
                gaps.append(LineRange{previousOriginal,
                                      anchor.originalIndex,
                                      previousEdited,
                                      anchor.editedIndex});
                preserveLineEndingAt(originalLines,
                                     anchor.originalIndex,
                                     anchor.editedIndex,
                                     preferredEnding,
                                     editedEndings);
                previousOriginal = anchor.originalIndex + 1;
                previousEdited = anchor.editedIndex + 1;
            }
            gaps.append(LineRange{previousOriginal,
                                  range.originalEnd,
                                  previousEdited,
                                  range.editedEnd});
            for (auto iterator = gaps.crbegin(); iterator != gaps.crend(); ++iterator) {
                pending.append(*iterator);
            }
        }

        for (auto iterator = suffix.crbegin(); iterator != suffix.crend(); ++iterator) {
            const LineMatch &match = *iterator;
            preserveLineEndingAt(originalLines,
                                 match.originalIndex,
                                 match.editedIndex,
                                 preferredEnding,
                                 editedEndings);
        }
    }
}
}

bool TextEditorDocumentIoService::readDocument(IFileSystem &fileSystem,
                                               const QString &filePath,
                                               QString *errorMessage,
                                               ReadDocumentResult *result)
{
    if (result == nullptr) {
        return false;
    }

    ReadDocumentResult payload;
    payload.loadedEncodingName = QStringLiteral("UTF-8");
    if (!fileSystem.readTextFile(filePath,
                                 &payload.contents,
                                 &payload.loadedEncodingName,
                                 &payload.loadedEncodingLabel,
                                 errorMessage)) {
        return false;
    }

    *result = payload;
    return true;
}

bool TextEditorDocumentIoService::writeDocument(IFileSystem &fileSystem,
                                                const QString &filePath,
                                                const QString &textContents,
                                                const QString &fileEncodingName,
                                                QString *errorMessage)
{
    return fileSystem.writeTextFile(filePath,
                                    textContents,
                                    fileEncodingName,
                                    errorMessage);
}

QString TextEditorDocumentIoService::contentsWithPreservedLineEndings(const QString &editorText,
                                                                      const QString &originalContents)
{
    const TherionSourceText originalSource = TherionSourceText::fromText(originalContents);
    if (normalizedEditorText(originalSource) == editorText) {
        return originalContents;
    }

    const TherionSourceText editedSource = TherionSourceText::fromText(editorText);
    const QVector<TherionSourceLine> &originalLines = originalSource.physicalLines();
    const QVector<TherionSourceLine> &editedLines = editedSource.physicalLines();
    const QString preferredEnding = originalSource.preferredLineEnding();
    QVector<QString> editedEndings(editedLines.size(), preferredEnding);
    preserveMatchedLineEndings(originalLines, editedLines, preferredEnding, &editedEndings);

    QString contents;
    for (int index = 0; index < editedLines.size(); ++index) {
        contents.append(editedLines.at(index).text);
        if (index < editedLines.size() - 1) {
            contents.append(editedEndings.at(index));
        }
    }
    return contents;
}

TextEditorDocumentPersistenceStateService::LoadStateInput
TextEditorDocumentIoService::buildLoadStateInput(const BuildLoadStateInputRequest &request)
{
    TextEditorDocumentPersistenceStateService::LoadStateInput input;
    input.filePath = request.filePath;
    input.textContents = request.contents;
    input.loadedEncodingName = request.loadedEncodingName;
    input.loadedEncodingLabel = request.loadedEncodingLabel;
    input.cursorLineNumber = request.cursorLineNumber;
    input.cursorColumnNumber = request.cursorColumnNumber;
    input.blocksModeActive = request.blocksModeActive;
    input.blocksModeSupportedForCurrentFile = request.blocksModeSupportedForCurrentFile;
    input.openedEncodingStatusTemplate = request.openedEncodingStatusTemplate;
    return input;
}

TextEditorDocumentPersistenceStateService::SaveStateInput
TextEditorDocumentIoService::buildSaveStateInput(const BuildSaveStateInputRequest &request)
{
    TextEditorDocumentPersistenceStateService::SaveStateInput input;
    input.textContents = request.textContents;
    input.fileEncodingName = request.fileEncodingName;
    input.fileEncodingLabel = request.fileEncodingLabel;
    input.savedEncodingStatusTemplate = request.savedEncodingStatusTemplate;
    return input;
}
}
