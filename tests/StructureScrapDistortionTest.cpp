#include "../src/app/StructureScrapDistortion.h"
#include "../src/app/MainWindowStructureRoles.h"

#include <QStandardItemModel>
#include <QPainter>
#include <QStyleOptionViewItem>
#include <QTreeView>
#include <QtTest/QtTest>

#include <limits>

using namespace TherionStudio;

class StructureScrapDistortionTest final : public QObject
{
    Q_OBJECT

private slots:
    void classifiesMaximumNotAverage_data()
    {
        QTest::addColumn<double>("maximum");
        QTest::addColumn<int>("state");
        using State = StructureScrapDistortion::State;
        QTest::newRow("zero") << 0.0 << int(State::Low);
        QTest::newRow("under-warning") << 9.99 << int(State::Low);
        QTest::newRow("warning") << 10.0 << int(State::Moderate);
        QTest::newRow("under-high") << 24.99 << int(State::Moderate);
        QTest::newRow("high") << 25.0 << int(State::High);
        QTest::newRow("very-high") << 150.0 << int(State::High);
        QTest::newRow("negative") << -1.0 << int(State::Unavailable);
        QTest::newRow("nan") << std::numeric_limits<double>::quiet_NaN() << int(State::Unavailable);
        QTest::newRow("infinite") << std::numeric_limits<double>::infinity() << int(State::Unavailable);
    }

    void classifiesMaximumNotAverage()
    {
        QFETCH(double, maximum);
        QFETCH(int, state);
        TherionScrapFigures scrap{0.1, maximum};
        QCOMPARE(int(StructureScrapDistortion::classify(&scrap)), state);
        scrap.maximumDistortion.reset();
        QCOMPARE(StructureScrapDistortion::classify(&scrap), StructureScrapDistortion::State::Unavailable);
        QCOMPARE(StructureScrapDistortion::classify(nullptr), StructureScrapDistortion::State::Unavailable);
    }

    void updatesRepeatedScrapsByOwningSurveyAndRestoresOnDisable()
    {
        ProjectStructureEntry survey;
        survey.kind = ProjectStructureEntryKind::Survey;
        survey.objectId = QStringLiteral("survey");
        survey.name = QStringLiteral("flat");
        survey.namespacePath = QStringLiteral("cave");
        survey.createsNamespace = false;
        ProjectStructureEntry scrap;
        scrap.kind = ProjectStructureEntryKind::Scrap;
        scrap.objectId = QStringLiteral("scrap");
        scrap.parentObjectId = survey.objectId;
        scrap.name = QStringLiteral("s");
        scrap.namespacePath = QStringLiteral("cave");

        TherionCompileFigures figures;
        figures.compile.finishedAt = QDateTime::currentDateTime();
        figures.database.databasePath = QStringLiteral("/compile/data.sql");
        figures.database.scrapsByKey.insert(QStringLiteral("s@flat.cave"), {1.0, 30.0});
        figures.database.scrapsByKey.insert(QStringLiteral("s@cave"), {0.0, 0.0});

        QStandardItemModel model;
        auto *parent = new QStandardItem(QStringLiteral("map"));
        model.appendRow(parent);
        auto *first = new QStandardItem(QStringLiteral("s"));
        auto *second = new QStandardItem(QStringLiteral("s"));
        for (auto *item : {first, second}) {
            item->setData(scrap.objectId, ObjectIdRole);
            item->setToolTip(QStringLiteral("source.th2:12"));
        }
        parent->appendRow(first);
        model.appendRow(second);
        const auto update = [&](bool enabled, bool stale) {
            StructureScrapDistortion::updateModel(&model, {survey, scrap}, figures, enabled, stale);
        };
        update(true, false);
        for (auto *item : {first, second}) {
            QCOMPARE(item->data(ScrapDistortionStateRole).toInt(), int(StructureScrapDistortion::State::High));
            QVERIFY(item->toolTip().contains(QStringLiteral("30.00")));
            QVERIFY(item->toolTip().startsWith(QStringLiteral("source.th2:12\n")));
        }
        QVERIFY(!parent->data(ScrapDistortionStateRole).isValid());
        update(true, false);
        QCOMPARE(first->toolTip().count(QStringLiteral("30.00")), 1);
        first->setToolTip(QStringLiteral("source.th2:20"));
        update(true, false);
        QVERIFY(first->toolTip().startsWith(QStringLiteral("source.th2:20\n")));
        QVERIFY(!first->data(ScrapDistortionOutdatedRole).isValid());
        update(true, true);
        QCOMPARE(first->data(ScrapDistortionStateRole).toInt(), int(StructureScrapDistortion::State::High));
        QVERIFY(first->data(ScrapDistortionOutdatedRole).toBool());
        QCOMPARE(first->toolTip().count(QStringLiteral("30.00")), 1);
        QVERIFY(first->toolTip().contains(QStringLiteral("Out of date")));
        figures.laterCompileFailed = true;
        update(true, false);
        QCOMPARE(first->data(ScrapDistortionStateRole).toInt(), int(StructureScrapDistortion::State::High));
        QVERIFY(first->data(ScrapDistortionOutdatedRole).toBool());
        figures.laterCompileFailed = false;
        update(true, false);
        QVERIFY(!first->data(ScrapDistortionOutdatedRole).isValid());
        QVERIFY(!first->toolTip().contains(QStringLiteral("Out of date")));
        figures.database.scrapsByKey.clear();
        update(true, true);
        QCOMPARE(first->data(ScrapDistortionStateRole).toInt(), int(StructureScrapDistortion::State::Unavailable));
        QVERIFY(!first->data(ScrapDistortionOutdatedRole).isValid());
        update(false, false);
        QVERIFY(!first->data(ScrapDistortionStateRole).isValid());
        QVERIFY(!first->data(ScrapDistortionOutdatedRole).isValid());
        QVERIFY(!second->data(ScrapDistortionStateRole).isValid());
        QCOMPARE(first->toolTip(), QStringLiteral("source.th2:20"));
        QCOMPARE(second->toolTip(), QStringLiteral("source.th2:12"));
    }

    void rendersIconBackgroundAndKeepsTreeState()
    {
        StructureScrapDistortion delegate;
        QStandardItemModel model;
        auto *item = new QStandardItem(QStringLiteral("scrap"));
        QPixmap glyph(16, 16);
        glyph.fill(Qt::black);
        item->setIcon(QIcon(glyph));
        model.appendRow(item);
        item->appendRow(new QStandardItem(QStringLiteral("child")));
        QTreeView tree;
        tree.setModel(&model);
        tree.setCurrentIndex(item->index());
        tree.expand(item->index());
        const auto render = [&](const QStyledItemDelegate &renderer) {
            QImage image(120, 24, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image);
            QStyleOptionViewItem option;
            option.widget = &tree;
            option.rect = image.rect();
            option.decorationSize = QSize(16, 16);
            option.state = QStyle::State_Enabled;
            renderer.paint(&painter, option, item->index());
            return image;
        };

        const QList<QPair<StructureScrapDistortion::State, QColor>> colors = {
            {StructureScrapDistortion::State::Low, QColor(37, 145, 70)},
            {StructureScrapDistortion::State::Moderate, QColor(193, 111, 0)},
            {StructureScrapDistortion::State::High, QColor(197, 45, 54)},
            {StructureScrapDistortion::State::Unavailable, QColor(110, 110, 110)},
        };
        for (const auto &[state, color] : colors) {
            item->setData(int(state), ScrapDistortionStateRole);
            const QImage rendered = render(delegate);
            int backgroundPixels = 0;
            for (int y = 0; y < rendered.height(); ++y) {
                for (int x = 0; x < rendered.width(); ++x) {
                    backgroundPixels += rendered.pixelColor(x, y) == color ? 1 : 0;
                }
            }
            QVERIFY(backgroundPixels > 0);
        }
        item->setData(int(StructureScrapDistortion::State::High), ScrapDistortionStateRole);
        item->setData(true, ScrapDistortionOutdatedRole);
        const QImage faded = render(delegate);
        int fullPixels = 0;
        int fadedPixels = 0;
        for (int y = 0; y < faded.height(); ++y) {
            for (int x = 0; x < faded.width(); ++x) {
                const QColor pixel = faded.pixelColor(x, y);
                fullPixels += pixel == QColor(197, 45, 54) ? 1 : 0;
                fadedPixels += pixel.red() > 200 && pixel.green() < 200 && pixel.green() > 60 ? 1 : 0;
            }
        }
        QCOMPARE(fullPixels, 0);
        QVERIFY(fadedPixels > 0);
        item->setData(QVariant(), ScrapDistortionOutdatedRole);
        item->setData(QVariant(), ScrapDistortionStateRole);
        QCOMPARE(render(delegate), render(QStyledItemDelegate()));
        QCOMPARE(tree.currentIndex(), item->index());
        QVERIFY(tree.isExpanded(item->index()));
    }
};

int runStructureScrapDistortionTest(int argc, char **argv)
{
    StructureScrapDistortionTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "StructureScrapDistortionTest.moc"
