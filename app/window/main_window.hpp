#pragma once
#include <headerscope/analysis.hpp>
#include <QFutureWatcher>
#include <QMainWindow>
#include <filesystem>
#include <memory>
#include <vector>

namespace headerscope { class DependencyGraph; }
class GraphPlaceholder;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTreeWidget;

// Fills `tree` with one node per directory (relative to `root`) and file leaves holding the full path in Qt::UserRole.
void populateFileTree(QTreeWidget* tree, const std::vector<std::filesystem::path>& files, const QString& root);

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
public slots:
    void runAnalysis();
signals:
    void analysisFinished();
private:
    void chooseProject();
    void chooseDatabase();
    void finishAnalysis();
    void updateSelection();
    void filterFiles(const QString& text);
    void selectPath(const QString& path);
    void setBusy(bool busy);
    QLineEdit* project_;
    QLineEdit* search_;
    QComboBox* mode_;
    QPushButton* browse_;
    QPushButton* database_;
    QPushButton* analyze_;
    QTreeWidget* files_;
    QTreeWidget* dependencies_;
    QTreeWidget* diagnostics_;
    GraphPlaceholder* graph_;
    QLabel* summary_;
    QLabel* selected_;
    QProgressBar* progress_;
    QFutureWatcher<headerscope::ScanResult> watcher_;
    headerscope::ScanResult result_;
    std::unique_ptr<headerscope::DependencyGraph> dependencyGraph_;
};
