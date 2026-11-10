#include "main_window.hpp"
#include "graph_placeholder.hpp"
#include <headerscope/demo/fixture.hpp>
#include <QDir>
#include <QFileInfo>
#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

namespace {
int leafCount(QTreeWidget* tree) {
    int count = 0;
    for (QTreeWidgetItemIterator it(tree); *it; ++it) count += (*it)->childCount() == 0;
    return count;
}
headerscope::ScanResult chain() {
    return headerscope::demo::makeIncludeChain((QDir::tempPath() + QStringLiteral("/HeaderScope-test")).toStdString());
}
const QGraphicsLineItem* edgeLine(const MainWindow& window, const QString& from, const QString& to) {
    for (auto* item : window.findChild<GraphPlaceholder*>()->scene()->items()) {
        if (const auto* line = qgraphicsitem_cast<QGraphicsLineItem*>(item);
            line && QFileInfo(line->data(2).toString()).fileName() == from
                 && QFileInfo(line->data(3).toString()).fileName() == to) {
            return line;
        }
    }
    return nullptr;
}
QColor markerColor(const QTreeWidgetItem* cycle) {
    return cycle->icon(0).pixmap(12, 12).toImage().pixelColor(6, 6);
}
}

class WindowTests : public QObject {
    Q_OBJECT
private slots:
    void busyBlocksSecondRun() {
        MainWindow window;
        QSignalSpy done(&window, &MainWindow::analysisFinished);
        window.runAnalysis();
        QVERIFY(!window.findChild<QPushButton*>("analyzeButton")->isEnabled());
        window.runAnalysis(); // A second request must not start another worker.
        QTRY_COMPARE(done.count(), 1);
        QVERIFY(window.findChild<QPushButton*>("analyzeButton")->isEnabled());
    }
    void dependencies() {
        MainWindow window;
        window.showResult(chain());
        auto* files = window.findChild<QTreeWidget*>("filesTree");
        QCOMPARE(leafCount(files), 3);
        const auto matches = files->findItems(QStringLiteral("b.h"), Qt::MatchExactly | Qt::MatchRecursive);
        QCOMPARE(matches.size(), 1);
        files->setCurrentItem(matches.front());
        auto* tree = window.findChild<QTreeWidget*>("dependenciesTree");
        auto* dependents = tree->topLevelItem(0);
        QCOMPARE(dependents->childCount(), 2);
        QStringList names;
        for (int i = 0; i < dependents->childCount(); ++i) names << dependents->child(i)->text(0);
        QVERIFY(names.contains(QStringLiteral("a.h")));
        QVERIFY(names.contains(QStringLiteral("main.cpp")));
        QVERIFY(dependents->child(1)->toolTip(0).contains(QStringLiteral("a.h")));
        window.findChild<QLineEdit*>("fileSearch")->setText(QStringLiteral("main"));
        QVERIFY(matches.front()->isHidden());
    }
    void dependentChainIsHighlighted() {
        MainWindow window;
        // main.cpp → a.h → b.h, plus a.h → extra.h which is off the chain.
        window.showResult(headerscope::demo::makeIncludeChain(
            (QDir::tempPath() + QStringLiteral("/HeaderScope-test")).toStdString(), true));
        auto* files = window.findChild<QTreeWidget*>("filesTree");
        files->setCurrentItem(files->findItems(QStringLiteral("b.h"), Qt::MatchExactly | Qt::MatchRecursive).front());
        auto* dependents = window.findChild<QTreeWidget*>("dependenciesTree")->topLevelItem(0);
        const auto highlighted = [&] {
            int count = 0;
            for (auto* item : window.findChild<GraphPlaceholder*>()->scene()->items())
                if (auto* line = qgraphicsitem_cast<QGraphicsLineItem*>(item)) count += line->pen().width() == 3;
            return count;
        };
        QCOMPARE(highlighted(), 0);
        for (int i = 0; i < dependents->childCount(); ++i)
            if (dependents->child(i)->text(0) == QStringLiteral("main.cpp"))
                window.findChild<QTreeWidget*>("dependenciesTree")->setCurrentItem(dependents->child(i));
        QCOMPARE(highlighted(), 2);
        files->setCurrentItem(files->findItems(QStringLiteral("a.h"), Qt::MatchExactly | Qt::MatchRecursive).front());
        QCOMPARE(highlighted(), 0);
    }
    void fileTreeGroupsByDirectory() {
        QTreeWidget tree;
        populateFileTree(&tree, {"/p/main.cpp", "/p/src/a.h", "/p/src/net/b.h", "/p/src/c.h"}, QStringLiteral("/p"));
        QCOMPARE(tree.topLevelItemCount(), 2); // main.cpp, src/
        const auto src = tree.findItems(QStringLiteral("src"), Qt::MatchExactly);
        QCOMPARE(src.size(), 1);
        QVERIFY(!src.front()->data(0, Qt::UserRole).isValid());
        QCOMPARE(src.front()->childCount(), 3); // a.h, net/, c.h
        const auto b = tree.findItems(QStringLiteral("b.h"), Qt::MatchExactly | Qt::MatchRecursive);
        QCOMPARE(b.size(), 1);
        QCOMPARE(b.front()->parent()->text(0), QStringLiteral("net"));
        QCOMPARE(b.front()->data(0, Qt::UserRole).toString(), QStringLiteral("/p/src/net/b.h"));
        QCOMPARE(leafCount(&tree), 4);
    }
    void searchHidesEmptyDirectories() {
        MainWindow window;
        window.showResult(chain());
        auto* search = window.findChild<QLineEdit*>("fileSearch");
        search->setText(QStringLiteral("no-such-file"));
        auto* files = window.findChild<QTreeWidget*>("filesTree");
        for (QTreeWidgetItemIterator it(files); *it; ++it) QVERIFY((*it)->isHidden());
        search->clear();
        for (QTreeWidgetItemIterator it(files); *it; ++it) QVERIFY(!(*it)->isHidden());
    }
    void partialScanKeepsGraph() {
        MainWindow window;
        auto result = chain();
        result.status = headerscope::ScanStatus::partial;
        result.diagnostics.push_back({headerscope::DiagnosticSeverity::error,
            "'missing.h' file not found", result.files.front().parent_path() / "a.h", 8});
        window.showResult(result);
        QCOMPARE(leafCount(window.findChild<QTreeWidget*>("filesTree")), 3);
        auto* diagnostics = window.findChild<QTreeWidget*>("diagnosticsTree");
        QCOMPARE(diagnostics->topLevelItemCount(), 1);
        QCOMPARE(diagnostics->topLevelItem(0)->text(3), QStringLiteral("8"));
        QVERIFY(diagnostics->topLevelItem(0)->text(2).endsWith(QStringLiteral("a.h")));
    }
    void failureClearsPreviousResult() {
        MainWindow window;
        window.showResult(chain());
        QSignalSpy done(&window, &MainWindow::analysisFinished);
        window.runAnalysis();
        QTRY_COMPARE(done.count(), 1);
        QCOMPARE(leafCount(window.findChild<QTreeWidget*>("filesTree")), 0);
        QCOMPARE(window.findChild<QTreeWidget*>("dependenciesTree")->topLevelItemCount(), 0);
        QCOMPARE(window.findChild<QTreeWidget*>("diagnosticsTree")->topLevelItemCount(), 1);
        QVERIFY(window.findChild<QPushButton*>("analyzeButton")->isEnabled());
    }
    void missingDatabase() {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        MainWindow window;
        window.findChild<QLineEdit*>("projectPath")->setText(project.path());
        QSignalSpy done(&window, &MainWindow::analysisFinished);
        window.runAnalysis();
        QTRY_COMPARE(done.count(), 1);
        auto* diagnostic = window.findChild<QTreeWidget*>("diagnosticsTree")->topLevelItem(0);
        QVERIFY(diagnostic);
        QVERIFY(diagnostic->text(1).contains(QStringLiteral("compile_commands.json")));
    }
    void allCyclesInOneGroupAreHighlighted() {
        MainWindow window;
        window.show();
        auto result = chain();
        const auto root = result.files.front().parent_path();
        result.edges.push_back({root / "b.h", root / "a.h", 4});
        result.edges.push_back({root / "b.h", root / "extra.h", 5});
        result.edges.push_back({root / "extra.h", root / "new.h", 2});
        result.edges.push_back({root / "new.h", root / "b.h", 2});
        window.showResult(result);
        QCOMPARE(leafCount(window.findChild<QTreeWidget*>("filesTree")), 5);
        QVERIFY(!window.findChild<GraphPlaceholder*>()->scene()->items().empty());
        auto* cycles = window.findChild<QTreeWidget*>("cyclesTree");
        QCOMPARE(cycles->topLevelItemCount(), 1);
        auto* group = cycles->topLevelItem(0);
        QCOMPARE(group->childCount(), 2);
        QCOMPARE(group->child(0)->text(0), QStringLiteral("a.h → b.h → a.h"));
        QCOMPARE(group->child(1)->text(0), QStringLiteral("b.h → extra.h → new.h → b.h"));
        const auto first = markerColor(group->child(0));
        const auto second = markerColor(group->child(1));
        QVERIFY(first.isValid() && second.isValid() && first != second);
        const auto* ab = edgeLine(window, "a.h", "b.h");
        const auto* ba = edgeLine(window, "b.h", "a.h");
        const auto* be = edgeLine(window, "b.h", "extra.h");
        const auto* en = edgeLine(window, "extra.h", "new.h");
        const auto* nb = edgeLine(window, "new.h", "b.h");
        const auto* main = edgeLine(window, "main.cpp", "a.h");
        QVERIFY(ab && ba && be && en && nb && main);
        const auto normalColor = ab->pen().color();
        QCOMPARE(window.findChild<QLabel*>("tabBadge")->text(), QStringLiteral("2"));
        auto* details = window.findChild<QStackedWidget*>("details");
        QCOMPARE(details->currentIndex(), 0);
        auto* tabs = window.findChild<QTabBar*>("viewTabs");
        tabs->setCurrentIndex(1);
        QCOMPARE(details->currentIndex(), 1);
        QCOMPARE(ab->pen().color(), first);
        QCOMPARE(ba->pen().color(), first);
        QCOMPARE(be->pen().color(), second);
        QCOMPARE(en->pen().color(), second);
        QCOMPARE(nb->pen().color(), second);
        QCOMPARE(main->pen().color(), normalColor);
        auto* reset = window.findChild<QPushButton*>("allCyclesButton");
        QVERIFY(!reset->isEnabled());
        QTRY_VERIFY(cycles->visualItemRect(group->child(1)).isValid());
        QTest::mouseClick(cycles->viewport(), Qt::LeftButton, Qt::NoModifier,
                         cycles->visualItemRect(group->child(1)).center());
        QVERIFY(reset->isEnabled());
        QCOMPARE(ab->pen().color(), normalColor);
        QCOMPARE(be->pen().color(), second);
        QCOMPARE(be->pen().width(), 5);
        QTest::mouseClick(reset, Qt::LeftButton);
        QVERIFY(!reset->isEnabled());
        QCOMPARE(ab->pen().color(), first);
        QCOMPARE(be->pen().color(), second);
        tabs->setCurrentIndex(0);
        QCOMPARE(ab->pen().color(), normalColor);
        QCOMPARE(be->pen().color(), normalColor);
    }
    void longCycleWrapsWhenThePanelNarrows() {
        MainWindow window;
        window.show();
        auto result = chain();
        const auto root = result.files.front().parent_path();
        const auto longHeader = root / "very_long_configuration_header_for_dashboard_reporting_and_export.h";
        result.edges.push_back({root / "b.h", longHeader, 2});
        result.edges.push_back({longHeader, root / "a.h", 2});
        window.showResult(result);
        window.findChild<QTabBar*>("viewTabs")->setCurrentIndex(1);
        auto* cycles = window.findChild<QTreeWidget*>("cyclesTree");
        const auto* cycle = cycles->topLevelItem(0)->child(0);
        QVERIFY(cycle);
        QCOMPARE(cycles->columnCount(), 1);
        cycles->setFixedWidth(420);
        QTRY_VERIFY(cycles->visualItemRect(cycle).height() > 0);
        const int wideHeight = cycles->visualItemRect(cycle).height();
        cycles->setFixedWidth(185);
        QTRY_VERIFY(cycles->visualItemRect(cycle).height() > wideHeight);
    }
    void sharedEdgeUsesBothCycleColors() {
        MainWindow window;
        auto result = chain();
        const auto root = result.files.front().parent_path();
        result.edges.push_back({root / "b.h", root / "a.h", 4});
        result.edges.push_back({root / "b.h", root / "extra.h", 5});
        result.edges.push_back({root / "extra.h", root / "a.h", 2});
        window.showResult(result);
        window.findChild<QTabBar*>("viewTabs")->setCurrentIndex(1);
        const auto* group = window.findChild<QTreeWidget*>("cyclesTree")->topLevelItem(0);
        const auto* shared = edgeLine(window, "a.h", "b.h");
        const auto* reverse = edgeLine(window, "b.h", "a.h");
        QVERIFY(group && shared && reverse);
        const auto brush = shared->pen().brush();
        QVERIFY(brush.gradient());
        const auto stops = brush.gradient()->stops();
        QCOMPARE(stops.front().second, markerColor(group->child(0)));
        QCOMPARE(stops.back().second, markerColor(group->child(1)));
        QCOMPARE(reverse->pen().color(), markerColor(group->child(0)));
        QVERIFY(shared->line().p1().y() != reverse->line().p1().y());
    }
    void acyclicHasNoCycleGroups() {
        MainWindow window;
        window.showResult(chain());
        auto* cycles = window.findChild<QTreeWidget*>("cyclesTree");
        QCOMPARE(cycles->topLevelItemCount(), 1);
        QCOMPARE(cycles->topLevelItem(0)->childCount(), 0); // "Циклов не найдено"
    }
};
QTEST_MAIN(WindowTests)
#include "tests.moc"
