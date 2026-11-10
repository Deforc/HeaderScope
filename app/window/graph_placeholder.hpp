#pragma once
#include <headerscope/analysis.hpp>
#include <QGraphicsView>

// Temporary adapter until HeaderScope::graph_view supplies the team's full widget.
class GraphPlaceholder final : public QGraphicsView {
    Q_OBJECT
public:
    explicit GraphPlaceholder(QWidget* parent = nullptr);
    void setResult(const headerscope::ScanResult& result);
    void selectFile(const std::filesystem::path& file);
    void fitGraph();
signals:
    void fileActivated(const QString& path);
protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
private:
    void recolor();
    QGraphicsScene* scene_;
    QString selected_;
};
