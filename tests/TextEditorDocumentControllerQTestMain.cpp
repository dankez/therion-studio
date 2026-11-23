#include "../src/app/text_editor/TextEditorDocumentController.h"
#include "../src/core/IFileSystem.h"
#include "../src/core/QtFileSystem.h"

#include <QFile>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QtTest/QtTest>

using namespace TherionStudio;

namespace
{
class FakeFileSystem final : public IFileSystem
{
public:
    QString readContents;
    QString writtenContents;
    bool readSucceeds = true;
    bool writeSucceeds = true;

    bool readTextFile(const QString &,
                      QString *contents,
                      QString *encodingName,
                      QString *encodingLabel,
                      QString *) override
    {
        if (!readSucceeds) {
            return false;
        }
        if (contents != nullptr) {
            *contents = readContents;
        }
        if (encodingName != nullptr) {
            *encodingName = QStringLiteral("UTF-8");
        }
        if (encodingLabel != nullptr) {
            *encodingLabel = QStringLiteral("UTF-8");
        }
        return true;
    }

    bool writeTextFile(const QString &,
                       const QString &contents,
                       const QString &,
                       QString *) override
    {
        writtenContents = contents;
        return writeSucceeds;
    }

    bool readUtf8TextFile(const QString &, QString *, QString *) override
    {
        return false;
    }

    bool writeUtf8TextFile(const QString &, const QString &, QString *) override
    {
        return false;
    }
};

struct ControllerFixture
{
    QPlainTextEdit editor;
    QString filePath;
    QString projectRootPath;
    QString fileEncodingName = QStringLiteral("UTF-8");
    QString fileEncodingLabel = QStringLiteral("UTF-8");
    QString encodingStatusNote;
    QString cleanTextSnapshot;
    QString cleanEncodingNameSnapshot;
    QString blockDetailsSelectedKind;
    bool loading = false;
    bool dirty = false;
    bool blocksModeActive = false;
    int currentLineNumber = 1;
    int currentColumnNumber = 1;
    int highlightedLineNumber = 0;
    int blockDetailsSelectedLineNumber = 0;

    TextEditorDocumentContext context()
    {
        TextEditorDocumentContext result;
        result.editor = &editor;
        result.filePath = &filePath;
        result.projectRootPath = &projectRootPath;
        result.fileEncodingName = &fileEncodingName;
        result.fileEncodingLabel = &fileEncodingLabel;
        result.encodingStatusNote = &encodingStatusNote;
        result.cleanTextSnapshot = &cleanTextSnapshot;
        result.cleanEncodingNameSnapshot = &cleanEncodingNameSnapshot;
        result.blockDetailsSelectedKind = &blockDetailsSelectedKind;
        result.loading = &loading;
        result.dirty = &dirty;
        result.blocksModeActive = &blocksModeActive;
        result.currentLineNumber = &currentLineNumber;
        result.currentColumnNumber = &currentColumnNumber;
        result.highlightedLineNumber = &highlightedLineNumber;
        result.blockDetailsSelectedLineNumber = &blockDetailsSelectedLineNumber;
        result.translate = [](const char *text) { return QString::fromUtf8(text); };
        return result;
    }
};

class TextEditorDocumentControllerQTest final : public QObject
{
    Q_OBJECT

private slots:
    void unchangedSavePreservesMixedLineEndings();
    void fileRoundTripPreservesMixedLineEndings();
    void editedSavePreservesExistingLineEndings();
    void insertedLineKeepsUnchangedLineEndings();
    void insertedDuplicateLineKeepsMixedLineEndings();
    void undoRestoresOriginalMixedLineEndings();
    void saveAndReopenPreservesEditedLineEndings();
};

void TextEditorDocumentControllerQTest::unchangedSavePreservesMixedLineEndings()
{
    FakeFileSystem fileSystem;
    fileSystem.readContents = QStringLiteral("survey cave\r\n# comment\npoint 1 2 station\rendsurvey\n");
    ControllerFixture fixture;
    TextEditorDocumentController controller(fileSystem, fixture.context());

    QString error;
    QVERIFY(controller.loadFile(QStringLiteral("mixed.th"), &error));
    QCOMPARE(fixture.editor.toPlainText(), QStringLiteral("survey cave\n# comment\npoint 1 2 station\nendsurvey\n"));
    QVERIFY(controller.save(&error));
    QCOMPARE(fileSystem.writtenContents, fileSystem.readContents);
}

void TextEditorDocumentControllerQTest::fileRoundTripPreservesMixedLineEndings()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString filePath = temporaryDirectory.filePath(QStringLiteral("mixed.th"));
    const QByteArray originalBytes = QByteArrayLiteral("survey cave\r\n# comment\npoint 1 2 station\rendsurvey\n");
    QFile sourceFile(filePath);
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    QCOMPARE(sourceFile.write(originalBytes), originalBytes.size());
    sourceFile.close();

    QtFileSystem fileSystem;
    ControllerFixture fixture;
    TextEditorDocumentController controller(fileSystem, fixture.context());

    QString error;
    QVERIFY(controller.loadFile(filePath, &error));
    QVERIFY(controller.save(&error));

    QFile savedFile(filePath);
    QVERIFY(savedFile.open(QIODevice::ReadOnly));
    QCOMPARE(savedFile.readAll(), originalBytes);
}

void TextEditorDocumentControllerQTest::editedSavePreservesExistingLineEndings()
{
    FakeFileSystem fileSystem;
    fileSystem.readContents = QStringLiteral("survey cave\r\npoint 1 2 station\nendsurvey\r\n");
    ControllerFixture fixture;
    TextEditorDocumentController controller(fileSystem, fixture.context());

    QString error;
    QVERIFY(controller.loadFile(QStringLiteral("mixed.th"), &error));
    fixture.editor.setPlainText(QStringLiteral("survey cave\npoint 3 2 station\nendsurvey\n"));
    QVERIFY(controller.save(&error));
    QCOMPARE(fileSystem.writtenContents,
             QStringLiteral("survey cave\r\npoint 3 2 station\nendsurvey\r\n"));
}

void TextEditorDocumentControllerQTest::insertedLineKeepsUnchangedLineEndings()
{
    FakeFileSystem fileSystem;
    fileSystem.readContents = QStringLiteral("first\r\nsecond\nthird\r\n");
    ControllerFixture fixture;
    TextEditorDocumentController controller(fileSystem, fixture.context());

    QString error;
    QVERIFY(controller.loadFile(QStringLiteral("mixed.th"), &error));
    QTextCursor cursor = fixture.editor.textCursor();
    cursor.setPosition(QStringLiteral("first\n").size());
    cursor.insertText(QStringLiteral("inserted\n"));
    QVERIFY(controller.save(&error));
    QCOMPARE(fileSystem.writtenContents, QStringLiteral("first\r\ninserted\r\nsecond\nthird\r\n"));
}

void TextEditorDocumentControllerQTest::insertedDuplicateLineKeepsMixedLineEndings()
{
    FakeFileSystem fileSystem;
    fileSystem.readContents = QStringLiteral("duplicate\r\nduplicate\nend\r\n");
    ControllerFixture fixture;
    TextEditorDocumentController controller(fileSystem, fixture.context());

    QString error;
    QVERIFY(controller.loadFile(QStringLiteral("mixed.th"), &error));
    fixture.editor.moveCursor(QTextCursor::Start);
    fixture.editor.insertPlainText(QStringLiteral("duplicate\n"));
    QVERIFY(controller.save(&error));
    QCOMPARE(fileSystem.writtenContents,
             QStringLiteral("duplicate\r\nduplicate\r\nduplicate\nend\r\n"));
}

void TextEditorDocumentControllerQTest::undoRestoresOriginalMixedLineEndings()
{
    FakeFileSystem fileSystem;
    fileSystem.readContents = QStringLiteral("first\r\nsecond\nthird\r\n");
    ControllerFixture fixture;
    TextEditorDocumentController controller(fileSystem, fixture.context());

    QString error;
    QVERIFY(controller.loadFile(QStringLiteral("mixed.th"), &error));
    fixture.editor.moveCursor(QTextCursor::Start);
    fixture.editor.insertPlainText(QStringLiteral("temporary\n"));
    fixture.editor.undo();
    QVERIFY(controller.save(&error));
    QCOMPARE(fileSystem.writtenContents, fileSystem.readContents);
}

void TextEditorDocumentControllerQTest::saveAndReopenPreservesEditedLineEndings()
{
    FakeFileSystem fileSystem;
    fileSystem.readContents = QStringLiteral("first\r\nsecond\nthird\r\n");
    ControllerFixture firstFixture;
    TextEditorDocumentController firstController(fileSystem, firstFixture.context());

    QString error;
    QVERIFY(firstController.loadFile(QStringLiteral("mixed.th"), &error));
    firstFixture.editor.setPlainText(QStringLiteral("first\nupdated\nthird\n"));
    QVERIFY(firstController.save(&error));
    const QString savedContents = fileSystem.writtenContents;

    fileSystem.readContents = savedContents;
    ControllerFixture reopenedFixture;
    TextEditorDocumentController reopenedController(fileSystem, reopenedFixture.context());
    QVERIFY(reopenedController.loadFile(QStringLiteral("mixed.th"), &error));
    QVERIFY(reopenedController.save(&error));
    QCOMPARE(fileSystem.writtenContents, savedContents);
}
}

QTEST_MAIN(TextEditorDocumentControllerQTest)

#include "TextEditorDocumentControllerQTestMain.moc"
