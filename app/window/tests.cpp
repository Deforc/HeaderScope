#include "main_window.hpp"
#include "graph_placeholder.hpp"
#include <QComboBox>
#include <QGraphicsScene>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

class WindowTests : public QObject {
    Q_OBJECT
private slots:
    void demoDependencies() {
        MainWindow window;
        QSignalSpy done(&window, &MainWindow::analysisFinished);
        window.runAnalysis();
        QVERIFY(!window.findChild<QPushButton*>("analyzeButton")->isEnabled());
        window.runAnalysis(); // A second request must not start another worker.
        QTRY_COMPARE(done.count(), 1);
        QVERIFY(window.findChild<QPushButton*>("analyzeButton")->isEnabled());
        auto* files = window.findChild<QListWidget*>("filesList");
        QCOMPARE(files->count(), 3);
        const auto matches = files->findItems(QStringLiteral("b.h"), Qt::MatchExactly);
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
    void partialScanKeepsGraph() {
        MainWindow window;
        window.findChild<QComboBox*>("analysisMode")->setCurrentIndex(2);
        QSignalSpy done(&window, &MainWindow::analysisFinished);
        window.runAnalysis();
        QTRY_COMPARE(done.count(), 1);
        QCOMPARE(window.findChild<QListWidget*>("filesList")->count(), 3);
        auto* diagnostics = window.findChild<QTreeWidget*>("diagnosticsTree");
        QCOMPARE(diagnostics->topLevelItemCount(), 1);
        QCOMPARE(diagnostics->topLevelItem(0)->text(3), QStringLiteral("8"));
        QVERIFY(diagnostics->topLevelItem(0)->text(2).endsWith(QStringLiteral("a.h")));
    }
    void failureClearsPreviousResult() {
        MainWindow window;
        QSignalSpy done(&window, &MainWindow::analysisFinished);
        window.runAnalysis();
        QTRY_COMPARE(done.count(), 1);
        window.findChild<QComboBox*>("analysisMode")->setCurrentIndex(3);
        window.findChild<QLineEdit*>("projectPath")->clear();
        window.runAnalysis();
        QTRY_COMPARE(done.count(), 2);
        QCOMPARE(window.findChild<QListWidget*>("filesList")->count(), 0);
        QCOMPARE(window.findChild<QTreeWidget*>("dependenciesTree")->topLevelItemCount(), 0);
        QCOMPARE(window.findChild<QTreeWidget*>("diagnosticsTree")->topLevelItemCount(), 1);
        QVERIFY(window.findChild<QPushButton*>("analyzeButton")->isEnabled());
    }
    void missingDatabase() {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        MainWindow window;
        window.findChild<QComboBox*>("analysisMode")->setCurrentIndex(3);
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
        window.findChild<QComboBox*>("analysisMode")->setCurrentIndex(1);
        QSignalSpy done(&window, &MainWindow::analysisFinished);
        window.runAnalysis();
        QTRY_COMPARE(done.count(), 1);
        QCOMPARE(window.findChild<QListWidget*>("filesList")->count(), 3);
        QVERIFY(!window.findChild<GraphPlaceholder*>()->scene()->items().empty());
    }
};
QTEST_MAIN(WindowTests)
#include "tests.moc"
