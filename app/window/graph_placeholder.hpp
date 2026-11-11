#pragma once
#include <headerscope/analysis.hpp>
#include <QColor>
#include <QGraphicsView>
#include <QStringList>
#include <map>
#include <set>
#include <utility>
#include <vector>

// Temporary adapter until HeaderScope::graph_view supplies the team's full widget.
class QContextMenuEvent;

class GraphPlaceholder final : public QGraphicsView {
    Q_OBJECT
public:
    using Edge = std::pair<QString, QString>;
    using EdgeColors = std::map<Edge, std::vector<QColor>>;

    explicit GraphPlaceholder(QWidget* parent = nullptr);
    void setResult(const headerscope::ScanResult& result, const std::filesystem::path& root = {});
    void setGrouped(bool grouped);
    void collapseFolders();
    void expandFolder(const QString& folder);
    void collapseFolder(const QString& folder);
    void selectFile(const std::filesystem::path& file, bool reveal = true);
    void fitGraph();
    QStringList folderMembers(const QString& folder) const;
    void selectFolder(const QString& folder);
    // Highlights these includer → included edges (path text) and their endpoints; empty clears.
    void highlightChain(std::set<Edge> edges);
    void setCycleHighlights(EdgeColors edges);
    void showCycleHighlights(bool show);
    void focusCycle(const QStringList& files, const QColor& color);
    // Draws a dashed includer → header arrow (red if it closes a cycle); an empty path clears it.
    void showProposedInclude(const QString& from, const QString& to, bool cycle);
signals:
    void fileActivated(const QString& path);
    void folderActivated(const QString& path);
protected:
    void changeEvent(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
private:
    static constexpr int ProposedRole = 4;
    void recolor();
    void rebuildGraph();
    void drawProposedInclude();
    bool revealFiles(const std::set<QString>& files);
    void updateFolderLabels();
    std::filesystem::path root_;
    std::set<QString> files_;
    std::set<Edge> edges_;
    std::map<QString, QStringList> foldersForFile_;
    std::map<QString, QStringList> folderFiles_;
    std::map<QString, QString> folderParents_;
    std::map<QString, QStringList> visibleMembers_;
    std::map<Edge, std::vector<Edge>> visibleEdges_;
    std::map<QString, QString> visibleFile_;
    std::set<QString> expandedFolders_;
    std::map<QString, QPointF> positions_;
    bool grouped_ = true;
    QString proposedFrom_, proposedTo_;
    bool proposedCycle_ = false;
    QGraphicsScene* scene_;
    QString selected_;
    std::set<QString> selectedGroupFiles_;
    std::set<Edge> chain_;
    std::set<QString> chainFiles_;
    EdgeColors cycleEdges_;
    std::set<QString> cycleFiles_;
    std::set<Edge> focusedEdges_;
    std::set<QString> focusedFiles_;
    QColor focusedColor_;
    bool showCycles_ = false;
};
