#include "graph_placeholder.hpp"
#include "path_utils.hpp"
#include "theme.hpp"
#include <QEvent>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QLinearGradient>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace {
QBrush edgeBrush(const std::vector<QColor>& colors, const QColor& fallback,
                 const QPointF& start, const QPointF& end) {
    if (colors.empty()) return QBrush(fallback);
    if (colors.size() == 1) return QBrush(colors.front());

    // Paint one band per cycle across the width of the edge.
    QLinearGradient bands(start, end);
    for (std::size_t i = 0; i < colors.size(); ++i) {
        bands.setColorAt(qreal(i) / colors.size(), colors[i]);
        bands.setColorAt((qreal(i) + 0.99) / colors.size(), colors[i]);
    }
    return QBrush(bands);
}
}

GraphPlaceholder::GraphPlaceholder(QWidget* parent) : QGraphicsView(parent), scene_(new QGraphicsScene(this)) {
    setScene(scene_);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(ScrollHandDrag);
    setTransformationAnchor(AnchorUnderMouse);
    setMinimumSize(280, 220);
    setAccessibleName(tr("Граф подключений"));
    scene_->addText(tr("Запустите анализ, чтобы увидеть связи"))->setData(9, QStringLiteral("Запустите анализ, чтобы увидеть связи"));
    recolor();
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &GraphPlaceholder::recolor);
}
void GraphPlaceholder::setResult(const headerscope::ScanResult& result) {
    scene_->clear();
    selected_.clear();
    chain_.clear(); chainFiles_.clear();
    cycleEdges_.clear(); cycleFiles_.clear();
    focusedEdges_.clear(); focusedFiles_.clear();
    resetTransform();
    std::set<std::filesystem::path> files(result.files.begin(), result.files.end());
    for (const auto& edge : result.edges) {
        files.insert(edge.includer);
        files.insert(edge.included);
    }
    if (files.empty()) {
        scene_->addText(tr("Нет файлов для отображения"))->setData(9, QStringLiteral("Нет файлов для отображения"));
        recolor(); return;
    }
    const int columns = std::max(1, int(std::ceil(std::sqrt(double(files.size())))));
    std::map<std::filesystem::path, QPointF> positions;
    int index = 0;
    for (const auto& file : files) {
        positions[file] = QPointF((index % columns) * 250, (index / columns) * 140);
        ++index;
    }
    std::set<std::pair<std::filesystem::path, std::filesystem::path>> edges;
    for (const auto& edge : result.edges) {
        edges.emplace(edge.includer, edge.included);
    }
    for (const auto& [from, to] : edges) {
        const QPointF a = positions.at(from), b = positions.at(to);
        if (a == b) {
            auto* loop = scene_->addEllipse(a.x() - 25, a.y() - 55, 50, 50, QPen(QColor("#64748b"), 2));
            loop->setData(2, pathText(from)); loop->setData(3, pathText(to));
            continue;
        }
        const QPointF delta = b - a;
        const double length = std::hypot(delta.x(), delta.y());
        const QPointF unit = delta / length;
        const double boundary = std::min(94.0 / std::max(0.001, std::abs(unit.x())),
                                         25.0 / std::max(0.001, std::abs(unit.y())));
        const QPointF normal(-unit.y(), unit.x());
        const QPointF offset = edges.contains({to, from}) ? normal * 5 : QPointF{};
        const QPointF end = b - unit * boundary + offset;
        auto* line = scene_->addLine(QLineF(a + offset, end), QPen(QColor("#64748b"), 2));
        line->setToolTip(pathText(from) + QStringLiteral(" → ") + pathText(to));
        auto* arrow = scene_->addPolygon(QPolygonF{end, end - unit * 12 + normal * 5, end - unit * 12 - normal * 5},
                                         QPen(Qt::NoPen), QBrush(QColor("#64748b")));
        for (QGraphicsItem* item : {static_cast<QGraphicsItem*>(line), static_cast<QGraphicsItem*>(arrow)}) {
            item->setData(2, pathText(from));
            item->setData(3, pathText(to));
        }
    }
    for (const auto& [file, point] : positions) {
        QPainterPath shape;
        shape.addRoundedRect(QRectF(-94, -25, 188, 50), 10, 10);
        auto* box = scene_->addPath(shape);
        box->setData(1, QString::fromStdString(file.extension().string()).startsWith(QStringLiteral(".c")));
        box->setPos(point);
        box->setZValue(1);
        box->setData(0, pathText(file));
        box->setToolTip(pathText(file));
        auto* label = new QGraphicsTextItem(box);
        label->setPlainText(fontMetrics().elidedText(pathText(file.filename()), Qt::ElideMiddle, 170));
        label->setPos(-87, -15);
    }
    scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-50, -50, 50, 50));
    recolor();
    fitGraph();
}
void GraphPlaceholder::selectFile(const std::filesystem::path& file) {
    selected_ = pathText(file);
    chain_.clear(); chainFiles_.clear();
    recolor();
}
void GraphPlaceholder::changeEvent(QEvent* event) {
    QGraphicsView::changeEvent(event);
    if (event->type() != QEvent::LanguageChange) return;
    setAccessibleName(tr("Граф подключений"));
    for (auto* item : scene_->items()) {
        if (auto* label = qgraphicsitem_cast<QGraphicsTextItem*>(item); label && label->data(9).isValid())
            label->setPlainText(tr(label->data(9).toString().toUtf8().constData()));
    }
}
void GraphPlaceholder::highlightChain(std::set<Edge> edges) {
    chain_ = std::move(edges);
    chainFiles_.clear();
    for (const auto& [from, to] : chain_) { chainFiles_.insert(from); chainFiles_.insert(to); }
    recolor();
}
void GraphPlaceholder::setCycleHighlights(EdgeColors edges) {
    cycleEdges_ = std::move(edges);
    cycleFiles_.clear();
    for (const auto& [edge, colors] : cycleEdges_) {
        cycleFiles_.insert(edge.first);
        cycleFiles_.insert(edge.second);
    }
    if (showCycles_) recolor();
}
void GraphPlaceholder::showCycleHighlights(bool show) {
    showCycles_ = show;
    recolor();
}
void GraphPlaceholder::focusCycle(const QStringList& files, const QColor& color) {
    focusedColor_ = color;
    focusedEdges_.clear();
    focusedFiles_ = std::set<QString>(files.begin(), files.end());
    for (qsizetype i = 0; i + 1 < files.size(); ++i) {
        focusedEdges_.emplace(files[i], files[i + 1]);
    }
    if (showCycles_) recolor();
}
// The single place that colours the scene, so a light/dark switch can repaint it.
void GraphPlaceholder::recolor() {
    const Theme& t = theme();
    setBackgroundBrush(QColor(t.graphBg));
    const bool focus = showCycles_ && !focusedEdges_.empty();
    const auto& highlightedFiles = focus ? focusedFiles_ : showCycles_ ? cycleFiles_ : chainFiles_;
    const std::vector<QColor> emptyColors;
    for (auto* item : scene_->items()) {
        if (auto* box = qgraphicsitem_cast<QGraphicsPathItem*>(item)) {
            const auto file = box->data(0).toString();
            const bool onChain = highlightedFiles.contains(file);
            const bool selected = file == selected_ && (!focus || onChain);
            box->setBrush(QColor(selected ? t.sel : box->data(1).toBool() ? t.sourceNode : t.node));
            const QColor outline = focus && onChain ? focusedColor_ : QColor(selected || onChain ? t.accent : t.edge);
            box->setPen(QPen(outline, selected || onChain ? 2 : 1));
        } else if (auto* text = qgraphicsitem_cast<QGraphicsTextItem*>(item)) {
            text->setDefaultTextColor(QColor(t.text));
        } else {
            const Edge edge{item->data(2).toString(), item->data(3).toString()};
            const auto found = showCycles_ && !focus ? cycleEdges_.find(edge) : cycleEdges_.end();
            const auto& colors = found == cycleEdges_.end() ? emptyColors : found->second;
            const bool on = focus ? focusedEdges_.contains(edge) : showCycles_ ? !colors.empty() : chain_.contains(edge);
            const QColor fallback = on && focus ? focusedColor_ : QColor(on ? t.accent : t.edge);
            if (auto* line = qgraphicsitem_cast<QGraphicsLineItem*>(item)) {
                const qreal width = !on ? 2 : focus ? 5 : 1 + 2 * std::clamp<std::size_t>(colors.size(), 1, 4);
                const auto geometry = line->line();
                const QPointF normal(-geometry.dy() / geometry.length(), geometry.dx() / geometry.length());
                const auto halfWidth = normal * (width / 2);
                line->setPen(QPen(edgeBrush(colors, fallback, geometry.p1() - halfWidth, geometry.p1() + halfWidth), width));
            } else if (auto* loop = qgraphicsitem_cast<QGraphicsEllipseItem*>(item)) {
                loop->setPen(QPen(colors.empty() ? fallback : colors.front(), !on ? 2 : focus ? 5 : 3));
            } else if (auto* arrow = qgraphicsitem_cast<QGraphicsPolygonItem*>(item)) {
                const auto points = arrow->polygon();
                arrow->setBrush(edgeBrush(colors, fallback, points[2], points[1]));
            }
            item->setZValue(on ? 0.5 : 0); // Above other edges, still under the boxes.
        }
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
