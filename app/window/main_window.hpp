#pragma once
#include <headerscope/analysis.hpp>
#include <headerscope/graph.hpp>
#include <QFutureWatcher>
#include <QMainWindow>
#include <QString>
#include <filesystem>
#include <memory>
#include <set>
#include <vector>

class GraphPlaceholder;
class QComboBox;
class QCheckBox;
class QEvent;
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
protected:
    void changeEvent(QEvent* event) override;
private:
    void retranslateUi();
    void updateSummary();
    bool hasResult_ = false;
    std::size_t groupCount_ = 0;
    std::size_t cycleCount_ = 0;
    QComboBox* language_;
    QLabel* heading_;
    QPushButton* fit_;
    QCheckBox* groupFolders_;
    QPushButton* collapseFolders_;
    bool showingResult_ = false;
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
    void exportGraph();
    void chooseProject();
    void chooseDatabase();
    void finishAnalysis();
    void updateSelection();
    void selectFolder(const QString& folder);
    void showFolderDependencies(const QString& folder);
    void updateFolderHeading();
    QString selectedFolder_;
    std::set<std::filesystem::path> selectedTargets_;
    void showChain(QTreeWidgetItem* dependent);
    void showCycles(const std::vector<CycleDetails>& groups);
    void updateCycleFocus();
    void checkInclude();
    void filterFiles(const QString& text);
    void selectPath(const QString& path);
    void setBusy(bool busy);
    QLineEdit* project_;
    QLineEdit* search_;
    QPushButton* browse_;
    QPushButton* database_;
    QPushButton* analyze_;
    QPushButton* export_;
    QPushButton* allCycles_;
    QTreeWidget* files_;
    QTreeWidget* dependencies_;
    QTreeWidget* diagnostics_;
    QTreeWidget* cycles_;
    QComboBox* includer_;
    QComboBox* header_;
    QLabel* checkResult_;
    GraphPlaceholder* graph_;
    QLabel* summary_;
    QLabel* selected_;
    QLabel* selectedPath_;
    QTabBar* tabs_;
    QStackedWidget* details_;
    QProgressBar* progress_;
    QFutureWatcher<AnalysisResult> watcher_;
    QString scanInput_;
    std::filesystem::path displayedRoot_;
    headerscope::ScanResult result_;
    std::shared_ptr<const headerscope::DependencyGraph> dependencyGraph_;
};
