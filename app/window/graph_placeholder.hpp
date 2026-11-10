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
class GraphPlaceholder final : public QGraphicsView {
    Q_OBJECT
public:
    using Edge = std::pair<QString, QString>;
    using EdgeColors = std::map<Edge, std::vector<QColor>>;

    explicit GraphPlaceholder(QWidget* parent = nullptr);
    void setResult(const headerscope::ScanResult& result);
    void selectFile(const std::filesystem::path& file);
    void fitGraph();
    // Highlights these includer → included edges (path text) and their endpoints; empty clears.
    void highlightChain(std::set<Edge> edges);
    void setCycleHighlights(EdgeColors edges);
    void showCycleHighlights(bool show);
    void focusCycle(const QStringList& files, const QColor& color);
signals:
    void fileActivated(const QString& path);
protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
private:
    void recolor();
    QGraphicsScene* scene_;
    QString selected_;
    std::set<Edge> chain_;
    std::set<QString> chainFiles_;
    EdgeColors cycleEdges_;
    std::set<QString> cycleFiles_;
    std::set<Edge> focusedEdges_;
    std::set<QString> focusedFiles_;
    QColor focusedColor_;
    bool showCycles_ = false;
};
