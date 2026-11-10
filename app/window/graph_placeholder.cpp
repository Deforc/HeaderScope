#include "graph_placeholder.hpp"
#include "path_utils.hpp"
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

GraphPlaceholder::GraphPlaceholder(QWidget* parent) : QGraphicsView(parent), scene_(new QGraphicsScene(this)) {
    setScene(scene_);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(ScrollHandDrag);
    setTransformationAnchor(AnchorUnderMouse);
    setBackgroundBrush(QColor("#f5f7fb"));
    setMinimumSize(280, 220);
    setAccessibleName(tr("Граф подключений"));
    scene_->addText(tr("Запустите анализ, чтобы увидеть связи"));
}
void GraphPlaceholder::setResult(const headerscope::ScanResult& result) {
    scene_->clear();
    resetTransform();
    std::set<std::filesystem::path> files(result.files.begin(), result.files.end());
    for (const auto& edge : result.edges) {
        files.insert(edge.includer);
        files.insert(edge.included);
    }
    if (files.empty()) { scene_->addText(tr("Нет файлов для отображения")); return; }
    const int columns = std::max(1, int(std::ceil(std::sqrt(double(files.size())))));
    std::map<std::filesystem::path, QPointF> positions;
    int index = 0;
    for (const auto& file : files) {
        positions[file] = QPointF((index % columns) * 250, (index / columns) * 140);
        ++index;
    }
    std::set<std::pair<std::filesystem::path, std::filesystem::path>> drawn;
    for (const auto& edge : result.edges) {
        if (!drawn.emplace(edge.includer, edge.included).second) continue;
        const QPointF a = positions.at(edge.includer), b = positions.at(edge.included);
        if (a == b) {
            scene_->addEllipse(a.x() - 25, a.y() - 55, 50, 50, QPen(QColor("#64748b"), 2));
            continue;
        }
        const QPointF delta = b - a;
        const double length = std::hypot(delta.x(), delta.y());
        const QPointF unit = delta / length;
        const double boundary = std::min(94.0 / std::max(0.001, std::abs(unit.x())),
                                         25.0 / std::max(0.001, std::abs(unit.y())));
        const QPointF end = b - unit * boundary;
        auto* line = scene_->addLine(QLineF(a, end), QPen(QColor("#64748b"), 2));
        line->setToolTip(pathText(edge.includer) + QStringLiteral(" → ") + pathText(edge.included));
        const QPointF normal(-unit.y(), unit.x());
        scene_->addPolygon(QPolygonF{end, end - unit * 12 + normal * 5, end - unit * 12 - normal * 5},
                           QPen(Qt::NoPen), QBrush(QColor("#64748b")));
    }
    for (const auto& [file, point] : positions) {
        auto* box = scene_->addRect(QRectF(-94, -25, 188, 50), QPen(QColor("#cbd5e1")), QBrush(Qt::white));
        box->setPos(point);
        box->setZValue(1);
        box->setData(0, pathText(file));
        box->setToolTip(pathText(file));
        auto* label = new QGraphicsTextItem(box);
        label->setPlainText(fontMetrics().elidedText(pathText(file.filename()), Qt::ElideMiddle, 170));
        label->setDefaultTextColor(QColor("#172554"));
        label->setPos(-87, -15);
    }
    scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-50, -50, 50, 50));
    fitGraph();
}
void GraphPlaceholder::selectFile(const std::filesystem::path& file) {
    for (auto* item : scene_->items()) {
        if (auto* box = qgraphicsitem_cast<QGraphicsRectItem*>(item))
            box->setBrush(box->data(0).toString() == pathText(file) ? QColor("#dbeafe") : QColor(Qt::white));
    }
}
void GraphPlaceholder::fitGraph() { fitInView(scene_->sceneRect(), Qt::KeepAspectRatio); }
void GraphPlaceholder::wheelEvent(QWheelEvent* event) {
    const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    if (const double next = transform().m11() * factor; next > 0.03 && next < 5.0) scale(factor, factor);
    event->accept();
}
void GraphPlaceholder::mousePressEvent(QMouseEvent* event) {
    auto* item = itemAt(event->position().toPoint());
    while (item && !item->data(0).isValid()) item = item->parentItem();
    if (item && event->button() == Qt::LeftButton) emit fileActivated(item->data(0).toString());
    QGraphicsView::mousePressEvent(event);
}
