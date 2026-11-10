#include "main_window.hpp"
#include "graph_placeholder.hpp"
#include <headerscope/demo/fixture.hpp>
#include <QDir>
#include <QGraphicsScene>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
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
        auto* dependents = tree->topLevelItem(1);
        QCOMPARE(dependents->childCount(), 2);
        QStringList names;
        for (int i = 0; i < dependents->childCount(); ++i) names << dependents->child(i)->text(0);
        QVERIFY(names.contains(QStringLiteral("a.h")));
        QVERIFY(names.contains(QStringLiteral("main.cpp")));
        QVERIFY(dependents->child(1)->toolTip(0).contains(QStringLiteral("a.h")));
        window.findChild<QLineEdit*>("fileSearch")->setText(QStringLiteral("main"));
        QVERIFY(matches.front()->isHidden());
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
    void cycleCanBeDisplayed() {
        MainWindow window;
        auto result = chain();
        const auto root = result.files.front().parent_path();
        result.edges.push_back({root / "b.h", root / "a.h", 4});
        window.showResult(result);
        QCOMPARE(leafCount(window.findChild<QTreeWidget*>("filesTree")), 3);
        QVERIFY(!window.findChild<GraphPlaceholder*>()->scene()->items().empty());
    }
};
QTEST_MAIN(WindowTests)
#include "tests.moc"
