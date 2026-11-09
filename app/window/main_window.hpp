#pragma once
#include <headerscope/analysis.hpp>
#include <QFutureWatcher>
#include <QMainWindow>
#include <memory>

namespace headerscope { class DependencyGraph; }
class GraphPlaceholder;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTreeWidget;

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
    QListWidget* files_;
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
