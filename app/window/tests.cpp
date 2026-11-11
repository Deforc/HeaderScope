#include "main_window.hpp"
#include "graph_placeholder.hpp"
#include "language.hpp"
#include <QComboBox>
#include <QSettings>
#include <QStatusBar>
#include <QGraphicsTextItem>
#include "path_utils.hpp"
#include <headerscope/demo/fixture.hpp>
#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStyleHints>
#include <QStackedWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
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
const QGraphicsLineItem* proposedIncludeLine(const MainWindow& window) {
    for (auto* item : window.findChild<GraphPlaceholder*>()->scene()->items()) {
        if (const auto* line = qgraphicsitem_cast<QGraphicsLineItem*>(item); line && line->pen().style() == Qt::DashLine)
            return line;
    }
    return nullptr;
}
QColor markerColor(const QTreeWidgetItem* cycle) {
    return cycle->icon(0).pixmap(12, 12).toImage().pixelColor(6, 6);
}
}

class WindowTests : public QObject {
    Q_OBJECT
    QTemporaryDir settingsDirectory_;
private slots:
    void initTestCase() {
        QVERIFY(settingsDirectory_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("HeaderScopeTests"));
        QCoreApplication::setApplicationName(QStringLiteral("WindowTests"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory_.path());
    }
    void cleanup() {
        language::setEnglish(false);
        QSettings().clear();
    }
    void languageSwitchPreservesGraphAndSelection() {
        MainWindow window;
        auto result = chain();
        const auto root = result.files.front().parent_path();
        result.edges.push_back({root / "b.h", root / "a.h", 4});
        window.showResult(result);
        auto* files = window.findChild<QTreeWidget*>("filesTree");
        files->setCurrentItem(files->findItems(QStringLiteral("b.h"), Qt::MatchExactly | Qt::MatchRecursive).front());
        auto* selected = files->currentItem();
        auto* dependencies = window.findChild<QTreeWidget*>("dependenciesTree");
        auto* dependent = dependencies->topLevelItem(0)->child(0);
        dependencies->setCurrentItem(dependent);
        auto* cycles = window.findChild<QTreeWidget*>("cyclesTree");
        auto* cycle = cycles->topLevelItem(0)->child(0);
        cycles->setCurrentItem(cycle);
        window.findChild<QTabBar*>("viewTabs")->setCurrentIndex(1);
        auto* graph = window.findChild<GraphPlaceholder*>();
        const auto before = graph->scene()->items();
        auto* selector = window.findChild<QComboBox*>("languageSelector");
        QVERIFY(selector);
        selector->setCurrentIndex(1);
        QCoreApplication::processEvents();
        QCOMPARE(window.findChild<QPushButton*>("analyzeButton")->text(), QStringLiteral("Analyze"));
        QCOMPARE(window.findChild<QPushButton*>("exportButton")->text(), QStringLiteral("Export graph…"));
        QCOMPARE(window.findChild<QTabBar*>("viewTabs")->tabText(1), QStringLiteral("Cycles (1)"));
        QCOMPARE(window.findChild<QTabBar*>("viewTabs")->tabText(2), QStringLiteral("#include check"));
        QCOMPARE(dependencies->topLevelItem(0)->text(0), QStringLiteral("Dependent files · 2"));
        QVERIFY(cycles->topLevelItem(0)->text(0).startsWith(QStringLiteral("Group 1")));
        QCOMPARE(files->currentItem(), selected);
        QCOMPARE(dependencies->currentItem(), dependent);
        QCOMPARE(cycles->currentItem(), cycle);
        QCOMPARE(graph->scene()->items(), before);
        QVERIFY(window.findChild<QLabel*>("analysisSummary")->text().startsWith(QStringLiteral("Analysis complete")));
        selector->setCurrentIndex(0);
        QCoreApplication::processEvents();
        QCOMPARE(window.findChild<QPushButton*>("analyzeButton")->text(), QStringLiteral("Анализировать"));
        QCOMPARE(window.findChild<QPushButton*>("exportButton")->text(), QStringLiteral("Экспорт графа…"));
        QCOMPARE(dependencies->topLevelItem(0)->text(0), QStringLiteral("Зависимые файлы · 2"));
        QCOMPARE(window.findChild<QTabBar*>("viewTabs")->tabText(1), QStringLiteral("Циклы (1)"));
        QCOMPARE(files->currentItem(), selected);
    }
    void languageSwitchDuringAnalysisAndDiagnostics() {
        MainWindow window;
        QSignalSpy done(&window, &MainWindow::analysisFinished);
        window.runAnalysis();
        auto* selector = window.findChild<QComboBox*>("languageSelector");
        selector->setCurrentIndex(1);
        QCOMPARE(window.findChild<QPushButton*>("analyzeButton")->text(), QStringLiteral("Analyzing…"));
        QTRY_COMPARE(done.count(), 1);
        auto* row = window.findChild<QTreeWidget*>("diagnosticsTree")->topLevelItem(0);
        QCOMPARE(row->text(0), QStringLiteral("Error"));
        QCOMPARE(row->text(1), QStringLiteral("Specify a project folder or compile_commands.json."));
        selector->setCurrentIndex(0);
        QCOMPARE(row->text(0), QStringLiteral("Ошибка"));
        QCOMPARE(row->text(1), QStringLiteral("Укажите папку проекта или compile_commands.json."));
    }
    void languagePreferenceAndEmptyGraph() {
        MainWindow first;
        first.findChild<QComboBox*>("languageSelector")->setCurrentIndex(1);
        QCoreApplication::processEvents();
        QCOMPARE(QSettings().value(QStringLiteral("ui/language")).toString(), QStringLiteral("en"));
        bool translated = false;
        for (auto* item : first.findChild<GraphPlaceholder*>()->scene()->items())
            if (auto* text = qgraphicsitem_cast<QGraphicsTextItem*>(item))
                translated |= text->toPlainText() == QStringLiteral("Run analysis to see dependencies");
        QVERIFY(translated);
        MainWindow next;
        QCOMPARE(next.findChild<QComboBox*>("languageSelector")->currentData().toString(), QStringLiteral("en"));
        QCOMPARE(next.findChild<QPushButton*>("analyzeButton")->text(), QStringLiteral("Analyze"));
    }
    void busyBlocksSecondRun() {
        MainWindow window;
        QSignalSpy done(&window, &MainWindow::analysisFinished);
        window.runAnalysis();
        QVERIFY(!window.findChild<QPushButton*>("analyzeButton")->isEnabled());
        QVERIFY(!window.findChild<QPushButton*>("exportButton")->isEnabled());
        window.runAnalysis(); // A second request must not start another worker.
        QTRY_COMPARE(done.count(), 1);
        QVERIFY(window.findChild<QPushButton*>("analyzeButton")->isEnabled());
        QVERIFY(!window.findChild<QPushButton*>("exportButton")->isEnabled());
    }
    void exportsDisplayedPartialGraph() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir(temp.path()).mkpath(QStringLiteral("nested/project")));
        const auto root = std::filesystem::canonical(nativePath(temp.filePath(QStringLiteral("nested/project"))));
        auto selectedRoot = pathText(root);
#ifndef _WIN32
        const auto alias = temp.filePath(QStringLiteral("project-alias"));
        std::filesystem::create_directory_symlink(root, nativePath(alias));
        selectedRoot = alias;
#endif
        MainWindow window;
        auto* project = window.findChild<QLineEdit*>("projectPath");
        project->setText(selectedRoot);
        auto result = headerscope::demo::makeIncludeChain(root);
        result.status = headerscope::ScanStatus::partial;
        window.showResult(result);
        project->setText(temp.path());

        auto* exportButton = window.findChild<QPushButton*>("exportButton");
        QVERIFY(exportButton->isEnabled());
        window.findChild<QComboBox*>("languageSelector")->setCurrentIndex(1);
        const auto previous = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, &window, [] {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->reject();
        });
        timeout.start(5000);
        QString dialogTitle;
        QTimer::singleShot(0, &window, [&] {
            if (auto* dialog = window.findChild<QFileDialog*>()) {
                dialogTitle = dialog->windowTitle();
                dialog->setDirectory(temp.path());
                dialog->selectFile(QStringLiteral("graph"));
                static_cast<QDialog*>(dialog)->accept();
            }
        });
        exportButton->click();
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous);

        QCOMPARE(dialogTitle, QStringLiteral("Export graph"));
        QVERIFY(window.statusBar()->currentMessage().startsWith(QStringLiteral("Graph exported: ")));
        QFile exported(temp.filePath(QStringLiteral("graph.json")));
        QVERIFY(exported.open(QIODevice::ReadOnly));
        const auto graph = QJsonDocument::fromJson(exported.readAll()).object();
        QCOMPARE(graph.value(QStringLiteral("status")).toString(), QStringLiteral("partial"));
        const auto nodes = graph.value(QStringLiteral("nodes")).toArray();
        QCOMPARE(nodes.size(), 3);
        QCOMPARE(nodes.at(0).toObject().value(QStringLiteral("path")).toString(), QStringLiteral("a.h"));
        QCOMPARE(graph.value(QStringLiteral("edges")).toArray().size(), 2);
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
        QCOMPARE(window.findChild<QTabBar*>("viewTabs")->tabText(1), QStringLiteral("Циклы (2)"));
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
    void proposedIncludeShowsCycleOnGraph() {
        MainWindow window;
        window.showResult(chain()); // main.cpp → a.h → b.h
        window.findChild<QTabBar*>("viewTabs")->setCurrentIndex(2);
        QCOMPARE(window.findChild<QStackedWidget*>("details")->currentIndex(), 2);
        auto* includer = window.findChild<QComboBox*>("checkIncluder");
        auto* header = window.findChild<QComboBox*>("checkHeader");
        auto* answer = window.findChild<QLabel*>("checkResult");
        QCOMPARE(header->currentIndex(), -1);
        QVERIFY(answer->text().startsWith(QStringLiteral("Выберите")));
        const auto pick = [](QComboBox* combo, const QString& name) {
            combo->setCurrentIndex(combo->findText(name));
        };
        pick(includer, "b.h"); pick(header, "main.cpp");
        QCOMPARE(answer->text().section(QLatin1Char('\n'), 0, 0),
                 QStringLiteral("Цикл замкнётся: b.h → main.cpp → a.h → b.h"));
        const auto* ma = edgeLine(window, "main.cpp", "a.h");
        const auto* ab = edgeLine(window, "a.h", "b.h");
        QVERIFY(ma && ab);
        QCOMPARE(ma->pen().width(), 3);
        QCOMPARE(ab->pen().width(), 3);
        QVERIFY(proposedIncludeLine(window));
        QCOMPARE(proposedIncludeLine(window)->pen().color(), QColor("#ef4444"));

        auto* selector = window.findChild<QComboBox*>("languageSelector");
        selector->setCurrentIndex(1);
        QCOMPARE(answer->text(), QStringLiteral("This include would create a cycle: b.h → main.cpp → a.h → b.h\n"
                                                "main.cpp already includes b.h indirectly."));
        QCOMPARE(includer->currentText(), QStringLiteral("b.h"));
        QCOMPARE(header->currentText(), QStringLiteral("main.cpp"));
        QCOMPARE(header->placeholderText(), QStringLiteral("Select a header"));
        QVERIFY(proposedIncludeLine(window));
        QVERIFY(proposedIncludeLine(window)->toolTip().startsWith(QStringLiteral("Proposed include:")));
        selector->setCurrentIndex(0);
        QVERIFY(answer->text().startsWith(QStringLiteral("Цикл замкнётся")));

        pick(includer, "main.cpp"); pick(header, "b.h");
        QVERIFY(answer->text().startsWith(QStringLiteral("Цикл не возникнет")));
        QCOMPARE(ab->pen().width(), 2);
        QVERIFY(proposedIncludeLine(window));
        QCOMPARE(proposedIncludeLine(window)->pen().color(), QColor("#16a34a"));

        window.findChild<QTabBar*>("viewTabs")->setCurrentIndex(0);
        QVERIFY(!proposedIncludeLine(window)); // The suggestion is drawn only on its own tab.
    }
    void proposedIncludeRecognizesExistingEdge() {
        MainWindow window;
        auto result = chain();
        const auto root = result.files.front().parent_path();
        result.edges.push_back({root / "b.h", root / "a.h", 4});
        window.showResult(result);
        window.findChild<QTabBar*>("viewTabs")->setCurrentIndex(2);
        auto* includer = window.findChild<QComboBox*>("checkIncluder");
        auto* header = window.findChild<QComboBox*>("checkHeader");
        includer->setCurrentIndex(includer->findText("b.h"));
        header->setCurrentIndex(header->findText("main.cpp"));
        QVERIFY(proposedIncludeLine(window));

        includer->setCurrentIndex(includer->findText("a.h"));
        header->setCurrentIndex(header->findText("b.h"));
        QCOMPARE(window.findChild<QLabel*>("checkResult")->text(), QStringLiteral("Подключение уже существует."));
        QVERIFY(!proposedIncludeLine(window));
    }
    void proposedIncludeUsesAvailablePartialGraph() {
        MainWindow window;
        auto result = chain();
        result.status = headerscope::ScanStatus::partial;
        window.showResult(result);
        window.findChild<QTabBar*>("viewTabs")->setCurrentIndex(2);
        auto* includer = window.findChild<QComboBox*>("checkIncluder");
        auto* header = window.findChild<QComboBox*>("checkHeader");
        auto* answer = window.findChild<QLabel*>("checkResult");

        includer->setCurrentIndex(includer->findText("b.h"));
        header->setCurrentIndex(header->findText("main.cpp"));
        QVERIFY(answer->text().startsWith(QStringLiteral("Цикл замкнётся")));
        QVERIFY(proposedIncludeLine(window));
        QCOMPARE(proposedIncludeLine(window)->pen().color(), QColor("#ef4444"));

        includer->setCurrentIndex(includer->findText("main.cpp"));
        header->setCurrentIndex(header->findText("b.h"));
        QCOMPARE(answer->text(), QStringLiteral("В доступных данных цикл не найден. Анализ завершился частично."));
        QVERIFY(!proposedIncludeLine(window));
    }
    void themeToggleSwitchesScheme() {
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
        QSKIP("Overriding the colour scheme needs Qt 6.8");
#else
        MainWindow window;
        auto* toggle = window.findChild<QAbstractButton*>("themeToggle");
        QVERIFY(toggle);
        auto* selector = window.findChild<QComboBox*>("languageSelector");
        selector->setCurrentIndex(1);
        QCOMPARE(window.findChild<QLabel*>("themeLabel")->text(), QStringLiteral("Dark theme"));
        QCOMPARE(toggle->accessibleName(), QStringLiteral("Dark theme"));
        selector->setCurrentIndex(0);
        QCOMPARE(window.findChild<QLabel*>("themeLabel")->text(), QStringLiteral("Тёмная тема"));
        auto* hints = QGuiApplication::styleHints();
        const auto before = hints->colorScheme();
        toggle->click();
        if (hints->colorScheme() == before) QSKIP("Platform ignores colour scheme requests");
        const bool dark = hints->colorScheme() == Qt::ColorScheme::Dark;
        QCOMPARE(toggle->isChecked(), dark);
        QVERIFY(window.styleSheet().contains(QLatin1String(dark ? "#0f172a" : "#f1f5f9")));
        hints->unsetColorScheme();
#endif
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
