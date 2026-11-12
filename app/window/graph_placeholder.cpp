#include "graph_placeholder.hpp"
#include "path_utils.hpp"
#include "theme.hpp"
#include <QEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QQueue>
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
// Draws a → b ending at b's box border; `shifted` moves it aside so an opposite edge stays visible.
std::pair<QGraphicsLineItem*, QGraphicsPolygonItem*> addArrow(QGraphicsScene* scene, QPointF a, QPointF b,
                                                              bool shifted, const QPen& pen) {
    const QPointF delta = b - a;
    const QPointF unit = delta / std::hypot(delta.x(), delta.y());
    const double boundary = std::min(94.0 / std::max(0.001, std::abs(unit.x())),
                                     25.0 / std::max(0.001, std::abs(unit.y())));
    const QPointF normal(-unit.y(), unit.x());
    const QPointF offset = shifted ? normal * 5 : QPointF{};
    const QPointF end = b - unit * boundary + offset;
    auto* line = scene->addLine(QLineF(a + offset, end), pen);
    auto* arrow = scene->addPolygon(QPolygonF{end, end - unit * 12 + normal * 5, end - unit * 12 - normal * 5},
                                    QPen(Qt::NoPen), pen.brush());
    return {line, arrow};
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
void GraphPlaceholder::setResult(const headerscope::ScanResult& result, const std::filesystem::path& root) {
    selected_.clear(); selectedGroupFiles_.clear(); chain_.clear(); chainFiles_.clear();
    cycleEdges_.clear(); cycleFiles_.clear(); focusedEdges_.clear(); focusedFiles_.clear();
    proposedFrom_.clear(); proposedTo_.clear();
    files_.clear(); edges_.clear(); foldersForFile_.clear(); folderFiles_.clear(); folderParents_.clear();
    expandedFolders_.clear();
    for (const auto& file : result.files) files_.insert(pathText(file));
    for (const auto& edge : result.edges) {
        const auto from = pathText(edge.includer), to = pathText(edge.included);
        files_.insert(from); files_.insert(to); edges_.emplace(from, to);
    }
    root_ = root.lexically_normal();
    if (!root_.empty() && !std::any_of(files_.begin(), files_.end(), [&](const auto& file) {
        const auto relative = nativePath(file).lexically_relative(root_);
        return !relative.empty() && *relative.begin() != "..";
    })) root_.clear(); // A database inside build/ may have no source files beneath it.
    if (root_.empty() && !files_.empty()) {
        root_ = nativePath(*files_.begin()).parent_path();
        for (const auto& file : files_) {
            while (!root_.empty()) {
                const auto relative = nativePath(file).lexically_relative(root_);
                if (!relative.empty() && *relative.begin() != "..") break;
                const auto parent = root_.parent_path();
                if (parent == root_) { root_.clear(); break; }
                root_ = parent;
            }
        }
    }
    QStringList rootFiles;
    for (const auto& file : files_) {
        const auto path = nativePath(file);
        const auto relative = root_.empty() ? std::filesystem::path{} : path.lexically_relative(root_);
        QStringList folders;
        if (!relative.empty() && *relative.begin() != "..") {
            auto directory = root_;
            QString parent;
            for (const auto& part : relative.parent_path()) {
                if (part == ".") continue;
                directory /= part;
                const auto key = pathText(directory);
                folderParents_[key] = parent;
                folders << key;
                parent = key;
            }
        } else {
            // Files outside the selected project stay in separate, explicitly labelled folders.
            const auto key = pathText(path.parent_path());
            folderParents_[key] = {};
            folders << key;
        }
        if (folders.empty()) rootFiles << file;
        foldersForFile_[file] = folders;
        for (const auto& folder : folders) folderFiles_[folder] << file;
    }
    // Large flat projects also need an overview, without changing small examples.
    if (rootFiles.size() > 12) {
        const QString key = QStringLiteral(":root-files:");
        folderParents_[key] = {};
        folderFiles_[key] = rootFiles;
        for (const auto& file : rootFiles) foldersForFile_[file] = {key};
    }
    rebuildGraph();
}
void GraphPlaceholder::setGrouped(bool grouped) {
    if (grouped_ == grouped) return;
    grouped_ = grouped;
    rebuildGraph();
}
void GraphPlaceholder::collapseFolders() {
    if (!grouped_ || expandedFolders_.empty()) return;
    expandedFolders_.clear();
    rebuildGraph();
}
void GraphPlaceholder::expandFolder(const QString& folder) {
    if (!grouped_ || !folderFiles_.contains(folder)) return;
    QString current = folder;
    while (!current.isEmpty()) {
        expandedFolders_.insert(current);
        current = folderParents_.at(current);
    }
    rebuildGraph();
}
void GraphPlaceholder::collapseFolder(const QString& folder) {
    if (expandedFolders_.erase(folder)) rebuildGraph();
}
bool GraphPlaceholder::revealFiles(const std::set<QString>& files) {
    bool changed = false;
    if (grouped_) for (const auto& file : files) {
        const auto found = foldersForFile_.find(file);
        if (found != foldersForFile_.end()) for (const auto& folder : found->second)
            changed |= expandedFolders_.insert(folder).second;
    }
    return changed;
}
void GraphPlaceholder::rebuildGraph() {
    scene_->clear(); positions_.clear(); visibleMembers_.clear(); visibleEdges_.clear(); visibleFile_.clear();
    if (files_.empty()) {
        scene_->addText(tr("Нет файлов для отображения"))->setData(9, QStringLiteral("Нет файлов для отображения"));
        scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-30, -30, 30, 30));
        resetTransform(); centerOn(scene_->sceneRect().center());
        recolor(); return;
    }
    for (const auto& file : files_) {
        QString representative = file;
        if (grouped_) for (const auto& folder : foldersForFile_.at(file)) {
            if (!expandedFolders_.contains(folder)) { representative = folder; break; }
        }
        visibleFile_[file] = representative;
        visibleMembers_[representative] << file;
    }
    std::map<QString, int> internalEdges;
    for (const auto& edge : edges_) {
        const Edge visible{visibleFile_.at(edge.first), visibleFile_.at(edge.second)};
        if (visible.first == visible.second && folderFiles_.contains(visible.first)) {
            ++internalEdges[visible.first];
        } else visibleEdges_[visible].push_back(edge);
    }
    // Shortest distance from entry points makes the overview flow left to right.
    // Visited nodes bound the traversal even when the graph contains cycles.
    std::map<QString, QStringList> outgoing;
    std::map<QString, int> indegree, level;
    for (const auto& [node, members] : visibleMembers_) indegree[node] = 0;
    for (const auto& [edge, originals] : visibleEdges_) if (edge.first != edge.second) {
        outgoing[edge.first] << edge.second; ++indegree[edge.second];
    }
    QQueue<QString> queue;
    for (const auto& [node, degree] : indegree) if (!degree) { level[node] = 0; queue.enqueue(node); }
    const auto visit = [&] {
        while (!queue.isEmpty()) {
            const auto node = queue.dequeue();
            for (const auto& next : outgoing[node]) if (!level.contains(next)) {
                level[next] = level.at(node) + 1; queue.enqueue(next);
            }
        }
    };
    visit();
    for (const auto& [node, degree] : indegree) if (!level.contains(node)) {
        level[node] = 0; queue.enqueue(node); visit();
    }
    const int columnLimit = std::clamp(int(std::ceil(std::sqrt(double(visibleMembers_.size())))), 2, 6);
    std::vector<QString> ordered;
    for (const auto& [node, members] : visibleMembers_) ordered.push_back(node);
    std::sort(ordered.begin(), ordered.end(), [&](const auto& a, const auto& b) {
        return std::pair{level.at(a), a} < std::pair{level.at(b), b};
    });
    std::map<int, int> rows;
    for (const auto& node : ordered) {
        const int column = level.at(node) % columnLimit;
        positions_[node] = QPointF(column * 280, rows[column]++ * 110);
    }
    for (const auto& [edge, originals] : visibleEdges_) {
        const QPointF a = positions_.at(edge.first), b = positions_.at(edge.second);
        std::vector<QGraphicsItem*> items;
        if (a == b) items.push_back(scene_->addEllipse(a.x() - 25, a.y() - 55, 50, 50, QPen(QColor("#64748b"), 2)));
        else {
            const auto [line, arrow] = addArrow(scene_, a, b, visibleEdges_.contains({edge.second, edge.first}), QPen(QColor("#64748b"), 2));
            items = {line, arrow};
        }
        for (auto* item : items) {
            item->setData(2, edge.first); item->setData(3, edge.second);
            item->setData(10, qulonglong(originals.size()));
        }
        if (originals.size() > 1) {
            auto* count = scene_->addText(QString::number(originals.size()));
            count->setPos((a + b) / 2 + QPointF(0, -22)); count->setZValue(0.8);
        }
    }
    for (const auto& [node, point] : positions_) {
        const bool folder = grouped_ && folderFiles_.contains(node);
        QPainterPath shape; shape.addRoundedRect(QRectF(-94, -25, 188, 50), 10, 10);
        auto* box = scene_->addPath(shape);
        box->setData(0, node); box->setData(5, folder);
        box->setData(1, !folder && nativePath(node).extension().string().starts_with(".c"));
        box->setData(8, visibleMembers_.at(node).size()); box->setData(11, internalEdges[node]);
        box->setPos(point); box->setZValue(1);
        auto* label = new QGraphicsTextItem(box);
        label->setPos(-87, folder ? -24 : -15);
        if (!folder) {
            const auto relative = root_.empty() ? nativePath(node) : nativePath(node).lexically_relative(root_);
            label->setPlainText(fontMetrics().elidedText(pathText(relative.empty() ? nativePath(node) : relative), Qt::ElideMiddle, 170));
            box->setToolTip(node);
        } else {
            QFont compact = label->font(); compact.setPointSize(9); label->setFont(compact);
        }
    }
    updateFolderLabels();
    drawProposedInclude();
    scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-55, -60, 55, 60));
    recolor(); fitGraph();
    // Do not make a large overview unreadably small just to fit every node.
    // The explicit Fit button still gives the user a complete overview.
    const qreal currentScale = transform().m11();
    const qreal readableScale = std::clamp(currentScale, qreal(0.85), qreal(1.2));
    scale(readableScale / currentScale, readableScale / currentScale);
    if (currentScale < 0.85) {
        const auto visible = visibleFile_.find(selected_);
        if (visible != visibleFile_.end()) centerOn(positions_.at(visible->second));
        else centerOn(scene_->sceneRect().left() + viewport()->width() / (2 * readableScale),
                      scene_->sceneRect().top() + viewport()->height() / (2 * readableScale));
    }
}
void GraphPlaceholder::updateFolderLabels() {
    for (auto* item : scene_->items()) {
        if (item->data(5).toBool()) {
            const auto key = item->data(0).toString();
            const auto relative = nativePath(key).lexically_relative(root_);
            const QString title = key == QStringLiteral(":root-files:") ? tr("Файлы в корне")
                : pathText(relative.empty() ? nativePath(key) : relative) + QLatin1Char('/');
            const auto count = item->data(8).toInt(), internal = item->data(11).toInt();
            item->setToolTip(tr("%1\nФайлов: %2 · связей внутри: %3\nДвойной щелчок — раскрыть папку").arg(title).arg(count).arg(internal));
            for (auto* child : item->childItems()) if (auto* label = qgraphicsitem_cast<QGraphicsTextItem*>(child))
                label->setPlainText(QStringLiteral("▸ ") + QFontMetrics(label->font()).elidedText(title, Qt::ElideMiddle, 155)
                    + QLatin1Char('\n') + QFontMetrics(label->font()).elidedText(
                        tr("%1 файлов · %2 связей внутри").arg(count).arg(internal), Qt::ElideRight, 170));
        } else if (item->data(10).isValid()) {
            const Edge edge{item->data(2).toString(), item->data(3).toString()};
            QString tooltip = tr("Связей #include: %1").arg(item->data(10).toInt());
            const auto found = visibleEdges_.find(edge);
            if (found != visibleEdges_.end()) {
                int shown = 0;
                for (const auto& [from, to] : found->second) {
                    if (shown++ == 8) { tooltip += QStringLiteral("\n…"); break; }
                    tooltip += QLatin1Char('\n') + from + QStringLiteral(" → ") + to;
                }
            }
            item->setToolTip(tooltip);
        }
    }
}
void GraphPlaceholder::selectFile(const std::filesystem::path& file, bool reveal) {
    selected_ = pathText(file); selectedGroupFiles_.clear();
    chain_.clear(); chainFiles_.clear();
    if (reveal && revealFiles({selected_})) rebuildGraph(); else recolor();
}
QStringList GraphPlaceholder::folderMembers(const QString& folder) const {
    const auto found = folderFiles_.find(folder);
    return found == folderFiles_.end() ? QStringList{} : found->second;
}
void GraphPlaceholder::selectFolder(const QString& folder) {
    selected_.clear(); chain_.clear(); chainFiles_.clear();
    const auto members = folderMembers(folder);
    selectedGroupFiles_ = std::set<QString>(members.begin(), members.end());
    recolor();
}
void GraphPlaceholder::changeEvent(QEvent* event) {
    QGraphicsView::changeEvent(event);
    if (event->type() != QEvent::LanguageChange) return;
    setAccessibleName(tr("Граф подключений"));
    updateFolderLabels();
    for (auto* item : scene_->items()) {
        if (auto* label = qgraphicsitem_cast<QGraphicsTextItem*>(item); label && label->data(9).isValid())
            label->setPlainText(tr(label->data(9).toString().toUtf8().constData()));
    }
}
void GraphPlaceholder::highlightChain(std::set<Edge> edges) {
    chain_ = std::move(edges);
    chainFiles_.clear();
    for (const auto& [from, to] : chain_) { chainFiles_.insert(from); chainFiles_.insert(to); }
    if (revealFiles(chainFiles_)) rebuildGraph(); else recolor();
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
    if (show && revealFiles(focusedFiles_)) rebuildGraph(); else recolor();
}
void GraphPlaceholder::focusCycle(const QStringList& files, const QColor& color) {
    focusedColor_ = color;
    focusedEdges_.clear();
    focusedFiles_ = std::set<QString>(files.begin(), files.end());
    for (qsizetype i = 0; i + 1 < files.size(); ++i) {
        focusedEdges_.emplace(files[i], files[i + 1]);
    }
    if (showCycles_) {
        if (revealFiles(focusedFiles_)) rebuildGraph(); else recolor();
    }
}
// The single place that colours the scene, so a light/dark switch can repaint it.
void GraphPlaceholder::recolor() {
    const Theme& t = theme();
    setBackgroundBrush(QColor(t.graphBg));
    const bool focus = showCycles_ && !focusedEdges_.empty();
    const auto& highlightedFiles = focus ? focusedFiles_ : showCycles_ ? cycleFiles_ : chainFiles_;
    for (auto* item : scene_->items()) {
        if (item->data(ProposedRole).toBool()) continue;
        if (auto* box = qgraphicsitem_cast<QGraphicsPathItem*>(item)) {
            const auto file = box->data(0).toString();
            const auto& members = visibleMembers_.at(file);
            const bool onChain = std::any_of(members.begin(), members.end(), [&](const auto& member) { return highlightedFiles.contains(member); });
            const bool selected = (members.contains(selected_) || std::any_of(members.begin(), members.end(),
                [&](const auto& member) { return selectedGroupFiles_.contains(member); })) && (!focus || onChain);
            box->setBrush(QColor(selected ? t.sel : (box->data(1).toBool() || box->data(5).toBool()) ? t.sourceNode : t.node));
            const QColor outline = focus && onChain ? focusedColor_ : QColor(selected || onChain ? t.accent : t.edge);
            box->setPen(QPen(outline, selected || onChain ? 2 : 1));
        } else if (auto* text = qgraphicsitem_cast<QGraphicsTextItem*>(item)) {
            text->setDefaultTextColor(QColor(t.text));
        } else {
            const Edge edge{item->data(2).toString(), item->data(3).toString()};
            std::vector<QColor> colors;
            bool on = false;
            const auto found = visibleEdges_.find(edge);
            if (found != visibleEdges_.end()) for (const auto& original : found->second) {
                if (showCycles_ && !focus) {
                    const auto cycle = cycleEdges_.find(original);
                    if (cycle != cycleEdges_.end()) for (const auto& color : cycle->second)
                        if (std::find(colors.begin(), colors.end(), color) == colors.end()) colors.push_back(color);
                }
                on |= focus ? focusedEdges_.contains(original) : showCycles_ ? !colors.empty() : chain_.contains(original);
            }
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
void GraphPlaceholder::showProposedInclude(const QString& from, const QString& to, bool cycle) {
    proposedFrom_ = from; proposedTo_ = to; proposedCycle_ = cycle;
    if (!from.isEmpty() && !to.isEmpty() && revealFiles({from, to})) rebuildGraph();
    else drawProposedInclude();
}
void GraphPlaceholder::drawProposedInclude() {
    for (auto* item : scene_->items()) if (item->data(ProposedRole).toBool()) delete item;
    const auto& from = proposedFrom_; const auto& to = proposedTo_;
    if (from.isEmpty() || to.isEmpty() || !positions_.contains(from) || !positions_.contains(to)) return;
    const auto& ends = positions_;
    const bool cycle = proposedCycle_;
    QPen pen(QColor(cycle ? "#ef4444" : "#16a34a"), 3, Qt::DashLine);
    std::vector<QGraphicsItem*> items;
    if (from == to) {
        const auto point = ends.at(from);
        items.push_back(scene_->addEllipse(point.x() - 30, point.y() - 62, 60, 60, pen));
    } else {
        const auto [line, arrow] = addArrow(scene_, ends.at(from), ends.at(to), true, pen);
        items = {line, arrow};
    }
    for (auto* item : items) {
        item->setData(ProposedRole, true);
        item->setZValue(0.75); // Above highlighted edges, still under the boxes.
        item->setToolTip(tr("Новое подключение: %1 → %2").arg(from, to));
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
    if (item && event->button() == Qt::LeftButton) {
        if (item->data(5).toBool()) emit folderActivated(item->data(0).toString());
        else emit fileActivated(item->data(0).toString());
    }
    QGraphicsView::mousePressEvent(event);
}

void GraphPlaceholder::mouseDoubleClickEvent(QMouseEvent* event) {
    auto* item = itemAt(event->position().toPoint());
    while (item && !item->data(0).isValid()) item = item->parentItem();
    if (item && item->data(5).toBool() && event->button() == Qt::LeftButton) {
        const auto folder = item->data(0).toString();
        expandFolder(folder); event->accept(); return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}
void GraphPlaceholder::contextMenuEvent(QContextMenuEvent* event) {
    auto* item = itemAt(event->pos());
    while (item && !item->data(0).isValid()) item = item->parentItem();
    QMenu menu(this);
    if (item && grouped_) {
        const auto key = item->data(0).toString();
        if (item->data(5).toBool()) menu.addAction(tr("Раскрыть папку"), this, [this, key] { expandFolder(key); });
        const auto parent = item->data(5).toBool() ? folderParents_.at(key)
            : foldersForFile_.at(key).isEmpty() ? QString() : foldersForFile_.at(key).back();
        if (!parent.isEmpty()) menu.addAction(tr("Свернуть родительскую папку"), this, [this, parent] { collapseFolder(parent); });
    }
    if (grouped_) menu.addAction(tr("Свернуть все папки"), this, &GraphPlaceholder::collapseFolders);
    menu.addAction(tr("Уместить граф"), this, &GraphPlaceholder::fitGraph);
    menu.exec(event->globalPos());
}
