#pragma once
#include <headerscope/analysis.hpp>
#include <QGraphicsView>
#include <set>
#include <utility>

// Temporary adapter until HeaderScope::graph_view supplies the team's full widget.
class GraphPlaceholder final : public QGraphicsView {
    Q_OBJECT
public:
    explicit GraphPlaceholder(QWidget* parent = nullptr);
    void setResult(const headerscope::ScanResult& result);
    void selectFile(const std::filesystem::path& file);
    void fitGraph();
    // Highlights these includer → included edges (path text) and their endpoints; empty clears.
    void highlightChain(std::set<std::pair<QString, QString>> edges);
signals:
    void fileActivated(const QString& path);
protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
private:
    void recolor();
    QGraphicsScene* scene_;
    QString selected_;
    std::set<std::pair<QString, QString>> chain_;
    std::set<QString> chainFiles_;
};
