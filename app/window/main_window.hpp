#pragma once
#include <headerscope/analysis.hpp>
#include <headerscope/graph.hpp>
#include <QFutureWatcher>
#include <QMainWindow>
#include <filesystem>
#include <memory>
#include <vector>

class GraphPlaceholder;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTabBar;
class QTreeWidget;
class QTreeWidgetItem;

// Fills `tree` with one node per directory (relative to `root`) and file leaves holding the full path in Qt::UserRole.
void populateFileTree(QTreeWidget* tree, const std::vector<std::filesystem::path>& files, const QString& root);

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
public slots:
    void runAnalysis();
    // Displays a finished scan; analysis runs call this, tests may feed results directly.
    void showResult(headerscope::ScanResult result);
signals:
    void analysisFinished();
private:
    struct CycleDetails {
        std::size_t fileCount;
        std::vector<headerscope::FileChain> chains;
    };
    struct AnalysisResult {
        headerscope::ScanResult scan;
        std::shared_ptr<const headerscope::DependencyGraph> graph;
        std::vector<CycleDetails> groups;
    };
    static AnalysisResult prepareAnalysis(headerscope::ScanResult scan);
    void showAnalysis(AnalysisResult analysis);
    void chooseProject();
    void chooseDatabase();
    void finishAnalysis();
    void updateSelection();
    void showChain(QTreeWidgetItem* dependent);
    void showCycles(const std::vector<CycleDetails>& groups);
    void updateCycleFocus();
    void filterFiles(const QString& text);
    void selectPath(const QString& path);
    void setBusy(bool busy);
    QLineEdit* project_;
    QLineEdit* search_;
    QPushButton* browse_;
    QPushButton* database_;
    QPushButton* analyze_;
    QPushButton* allCycles_;
    QTreeWidget* files_;
    QTreeWidget* dependencies_;
    QTreeWidget* diagnostics_;
    QTreeWidget* cycles_;
    GraphPlaceholder* graph_;
    QLabel* summary_;
    QLabel* selected_;
    QLabel* selectedPath_;
    QTabBar* tabs_;
    QLabel* cycleBadge_;
    QStackedWidget* details_;
    QProgressBar* progress_;
    QFutureWatcher<AnalysisResult> watcher_;
    headerscope::ScanResult result_;
    std::shared_ptr<const headerscope::DependencyGraph> dependencyGraph_;
};
