#include "main_window.hpp"
#include "graph_placeholder.hpp"
#include "path_utils.hpp"
#include "theme.hpp"
#include "language.hpp"
#include <QComboBox>
#include <QCheckBox>
#include <QEvent>
#include <QSettings>
#include <headerscope/export.hpp>
#if HEADERSCOPE_HAS_SCANNER
#include <headerscope/scanner.hpp>
#endif
#include <QtConcurrent/QtConcurrentRun>
#include <QAbstractButton>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHash>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTextLayout>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>
#include <set>
#include <stdexcept>

namespace {
enum CycleRowRole { CycleFiles = Qt::UserRole, CycleColor };

class CycleRowDelegate final : public QStyledItemDelegate {
public:
    explicit CycleRowDelegate(QTreeWidget* tree) : QStyledItemDelegate(tree), tree_(tree) {}

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        auto row = option;
        initStyleOption(&row, index);
        const int indent = tree_->indentation() * (index.parent().isValid() ? 2 : 1);
        const int width = tree_->columnWidth(0) - indent - textInset(row.icon) - padding;
        QTextLayout layout(row.text, row.font);
        layout.setTextOption(wrapping());
        qreal height = 0;
        layout.beginLayout();
        for (auto line = layout.createLine(); line.isValid(); line = layout.createLine()) {
            line.setLineWidth(std::max(1, width));
            height += line.height();
        }
        layout.endLayout();
        return {0, std::max(markerSize, int(std::ceil(height))) + 2 * padding};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        auto row = option;
        initStyleOption(&row, index);
        const auto text = row.text;
        const auto icon = row.icon;
        row.text.clear();
        row.icon = QIcon{};
        tree_->style()->drawControl(QStyle::CE_ItemViewItem, &row, painter, tree_);

        painter->save();
        painter->setClipRect(row.rect, Qt::IntersectClip);
        const QRect marker(row.rect.x() + padding, row.rect.y() + padding + 2, markerSize, markerSize);
        icon.paint(painter, marker);
        painter->setFont(row.font);
        painter->setPen(QColor(theme().text));
        const auto textRect = row.rect.adjusted(textInset(icon), padding, -padding, -padding);
        painter->drawText(QRectF(textRect), text, wrapping());
        painter->restore();
    }

private:
    static constexpr int padding = 6;
    static constexpr int markerSize = 12;

    static int textInset(const QIcon& icon) {
        return icon.isNull() ? padding : 2 * padding + markerSize;
    }
    static QTextOption wrapping() {
        QTextOption option(Qt::AlignLeft | Qt::AlignTop);
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        return option;
    }

    const QTreeWidget* tree_;
};

// iPhone-style on/off switch: a pill track with a knob that slides to the checked side.
class ThemeSwitch final : public QAbstractButton {
public:
    ThemeSwitch() {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(sizeHint());
        slide_.setDuration(150);
        slide_.setEasingCurve(QEasingCurve::OutCubic);
        connect(&slide_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            knob_ = value.toReal(); update();
        });
        connect(this, &QAbstractButton::toggled, this, [this](bool on) {
            slide_.stop();
            if (!isVisible()) { knob_ = on; update(); return; }
            slide_.setStartValue(knob_); slide_.setEndValue(on ? 1.0 : 0.0); slide_.start();
        });
    }
    QSize sizeHint() const override { return {44, 24}; }

protected:
    void paintEvent(QPaintEvent*) override {
        const Theme& t = theme();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF track = QRectF(rect()).adjusted(1, 1, -1, -1);
        painter.setPen(hasFocus() ? QPen(QColor(t.hover), 2) : Qt::NoPen);
        painter.setBrush(QColor(isChecked() ? t.accent : t.edge));
        painter.drawRoundedRect(track, track.height() / 2, track.height() / 2);
        const qreal size = track.height() - 4;
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawEllipse(QRectF(track.x() + 2 + knob_ * (track.width() - size - 4), track.y() + 2, size, size));
    }

private:
    QVariantAnimation slide_;
    qreal knob_ = 0;
};

QColor cycleColor(std::size_t index) {
    static const QColor palette[] = {
        QColor("#3478e5"), QColor("#d9790d"), QColor("#a34bd9"),
        QColor("#078a63"), QColor("#d94779"), QColor("#008f9f")
    };
    if (index < std::size(palette)) return palette[index];
    return QColor::fromHsv(int((index % 360 * 137) % 360), 190, 205);
}

headerscope::ScanResult failure(const QString& message) {
    headerscope::ScanResult result;
    result.diagnostics.push_back({headerscope::DiagnosticSeverity::error,
        message.toUtf8().toStdString(), std::nullopt, std::nullopt});
    return result;
}
QString relativeName(const std::filesystem::path& file, const QString& root) {
    return QDir(root).relativeFilePath(pathText(file));
}
// Multi-source reverse traversal: every file that reaches at least one selected target.
std::set<std::filesystem::path> dependentClosure(const headerscope::ScanResult& scan,
    const std::set<std::filesystem::path>& targets) {
    std::map<std::filesystem::path, headerscope::FileChain> reverse;
    for (const auto& edge : scan.edges) reverse[edge.included].push_back(edge.includer);
    auto visited = targets;
    std::vector<std::filesystem::path> pending(targets.begin(), targets.end());
    for (std::size_t i = 0; i < pending.size(); ++i)
        for (const auto& next : reverse[pending[i]]) if (visited.insert(next).second) pending.push_back(next);
    return visited;
}
// Hides leaves not matching `text`, and directories left without visible children. Returns true if visible.
bool filterItem(QTreeWidgetItem* item, const QString& text) {
    bool visible = false;
    if (!item->childCount()) visible = item->data(0, Qt::UserRole + 1).toString().contains(text, Qt::CaseInsensitive);
    for (int i = 0; i < item->childCount(); ++i) visible |= filterItem(item->child(i), text);
    item->setHidden(!visible);
    return visible;
}
QWidget* panel(const char* title, QWidget* content, QWidget* extra = nullptr) {
    auto* widget = new QWidget;
    widget->setObjectName(QStringLiteral("panel"));
    auto* layout = new QVBoxLayout(widget);
    auto* label = new QLabel(language::text(title));
    label->setProperty("translationSource", QString::fromUtf8(title));
    label->setObjectName(QStringLiteral("panelTitle"));
    layout->addWidget(label);
    if (extra) layout->addWidget(extra);
    layout->addWidget(content, 1);
    return widget;
}
}

void populateFileTree(QTreeWidget* tree, const std::vector<std::filesystem::path>& files, const QString& root) {
    QHash<QString, QTreeWidgetItem*> directories;
    const QFileIconProvider icons;
    const QIcon folderIcon = icons.icon(QAbstractFileIconProvider::Folder);
    const QIcon fileIcon = icons.icon(QAbstractFileIconProvider::File);
    for (const auto& file : files) {
        const QStringList parts = relativeName(file, root).split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QTreeWidgetItem* parent = nullptr;
        QString key;
        for (qsizetype i = 0; i + 1 < parts.size(); ++i) {
            key += parts[i] + QLatin1Char('/');
            auto*& directory = directories[key];
            if (!directory) {
                directory = parent ? new QTreeWidgetItem(parent, {parts[i]}) : new QTreeWidgetItem(tree, {parts[i]});
                directory->setToolTip(0, key);
                directory->setData(0, Qt::UserRole + 2, QDir::cleanPath(QDir(root).absoluteFilePath(key)));
                directory->setIcon(0, folderIcon);
            }
            parent = directory;
        }
        const QString name = parts.isEmpty() ? pathText(file) : parts.back();
        // Search matches the relative path, so typing a directory name finds the files inside it.
        auto* leaf = parent ? new QTreeWidgetItem(parent, {name}) : new QTreeWidgetItem(tree, {name});
        leaf->setData(0, Qt::UserRole, pathText(file)); leaf->setToolTip(0, pathText(file));
        leaf->setIcon(0, fileIcon);
        leaf->setData(0, Qt::UserRole + 1, parts.join(QLatin1Char('/')));
    }
    tree->expandAll();
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("HeaderScope"));
    resize(1280, 800);
    setStyleSheet(themeStyleSheet());
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this] { setStyleSheet(themeStyleSheet()); });
    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("central"));
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(18, 18, 18, 12);
    layout->setSpacing(12);
    heading_ = new QLabel(tr("HeaderScope · зависимости C/C++"));
    QFont titleFont = heading_->font(); titleFont.setPointSize(18); titleFont.setBold(true);
    heading_->setFont(titleFont);
    auto* titleRow = new QHBoxLayout;
    titleRow->addWidget(heading_); titleRow->addStretch();
    language_ = new QComboBox;
    language_->setObjectName(QStringLiteral("languageSelector"));
    language_->addItem(QStringLiteral("Русский"), QStringLiteral("ru"));
    language_->addItem(QStringLiteral("English"), QStringLiteral("en"));
    titleRow->addWidget(language_);
    layout->addLayout(titleRow);

    auto* controls = new QHBoxLayout;
    project_ = new QLineEdit;
    project_->setObjectName(QStringLiteral("projectPath"));
    project_->setPlaceholderText(tr("Папка проекта или compile_commands.json"));
    project_->setAccessibleName(tr("Путь к проекту или базе команд"));
    browse_ = new QPushButton(tr("Папка…"));
    database_ = new QPushButton(tr("compile_commands.json…"));
    controls->addWidget(project_, 1); controls->addWidget(browse_); controls->addWidget(database_);
    layout->addLayout(controls);

    auto* actions = new QHBoxLayout;
    analyze_ = new QPushButton(tr("Анализировать"));
    analyze_->setObjectName(QStringLiteral("analyzeButton"));
    analyze_->setDefault(true);
    export_ = new QPushButton(tr("Экспорт графа…"));
    export_->setObjectName(QStringLiteral("exportButton"));
    export_->setEnabled(false);
    fit_ = new QPushButton(tr("Уместить граф"));
    actions->addWidget(analyze_); actions->addWidget(export_); actions->addStretch(); actions->addWidget(fit_);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // Overriding the scheme re-emits colorSchemeChanged, which already restyles the window and graph.
    auto* themeToggle = new ThemeSwitch;
    themeToggle->setObjectName(QStringLiteral("themeToggle"));
    themeToggle->setAccessibleName(tr("Тёмная тема"));
    auto* hints = QGuiApplication::styleHints();
    const auto sync = [themeToggle, hints] { themeToggle->setChecked(hints->colorScheme() == Qt::ColorScheme::Dark); };
    sync();
    connect(hints, &QStyleHints::colorSchemeChanged, themeToggle, sync);
    // clicked, not toggled: following a system change must not pin an override.
    connect(themeToggle, &QAbstractButton::clicked, this, [hints](bool dark) {
        hints->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    });
    actions->addSpacing(12);
    auto* themeLabel = new QLabel(tr("Тёмная тема"));
    themeLabel->setObjectName(QStringLiteral("themeLabel"));
    themeLabel->setProperty("translationSource", QStringLiteral("Тёмная тема"));
    actions->addWidget(themeLabel);
    actions->addWidget(themeToggle);
#endif
    layout->addLayout(actions);
    summary_ = new QLabel(tr("Выберите папку проекта и нажмите «Анализировать»."));
    summary_->setObjectName(QStringLiteral("analysisSummary"));
    summary_->setWordWrap(true); layout->addWidget(summary_);

    auto* horizontal = new QSplitter(Qt::Horizontal);
    files_ = new QTreeWidget;
    files_->setObjectName(QStringLiteral("filesTree"));
    files_->setHeaderHidden(true);
    files_->setAccessibleName(tr("Файлы проекта"));
    search_ = new QLineEdit;
    search_->setObjectName(QStringLiteral("fileSearch"));
    search_->setPlaceholderText(tr("Найти файл…")); search_->setClearButtonEnabled(true);
    horizontal->addWidget(panel("ФАЙЛЫ", files_, search_));
    graph_ = new GraphPlaceholder;
    tabs_ = new QTabBar;
    tabs_->setObjectName(QStringLiteral("viewTabs"));
    tabs_->setDrawBase(false); tabs_->setExpanding(false);
    tabs_->addTab(tr("Граф")); tabs_->addTab(tr("Циклы")); tabs_->addTab(tr("Проверка #include"));
    auto* center = panel("A → B означает: A подключает B", graph_);
    groupFolders_ = new QCheckBox(tr("Группировать по папкам"));
    groupFolders_->setObjectName(QStringLiteral("groupFolders"));
    groupFolders_->setChecked(true);
    collapseFolders_ = new QPushButton(tr("Свернуть всё"));
    collapseFolders_->setObjectName(QStringLiteral("collapseFolders"));
    auto* graphControls = new QWidget;
    auto* graphControlsLayout = new QHBoxLayout(graphControls);
    graphControlsLayout->setContentsMargins(0, 0, 0, 0);
    graphControlsLayout->addWidget(groupFolders_);
    graphControlsLayout->addStretch();
    graphControlsLayout->addWidget(collapseFolders_);
    static_cast<QVBoxLayout*>(center->layout())->insertWidget(0, graphControls);
    static_cast<QVBoxLayout*>(center->layout())->insertWidget(0, tabs_, 0, Qt::AlignLeft);
    horizontal->addWidget(center);

    dependencies_ = new QTreeWidget;
    dependencies_->setObjectName(QStringLiteral("dependenciesTree"));
    dependencies_->setColumnCount(2); dependencies_->setHeaderHidden(true);
    dependencies_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    dependencies_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    dependencies_->header()->setStretchLastSection(false);
    dependencies_->setRootIsDecorated(false); dependencies_->setIndentation(0);
    dependencies_->setMinimumWidth(260);
    dependencies_->setAccessibleName(tr("Подключения и зависимые файлы"));
    selected_ = new QLabel(tr("Выберите файл")); selected_->setWordWrap(true);
    selected_->setObjectName(QStringLiteral("selectedFile"));
    selected_->setTextFormat(Qt::PlainText);
    selectedPath_ = new QLabel; selectedPath_->setWordWrap(true);
    selectedPath_->setObjectName(QStringLiteral("selectedPath"));
    selectedPath_->setTextFormat(Qt::PlainText);
    auto* header = new QWidget;
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0); headerLayout->setSpacing(2);
    headerLayout->addWidget(selected_); headerLayout->addWidget(selectedPath_);
    cycles_ = new QTreeWidget;
    cycles_->setObjectName(QStringLiteral("cyclesTree"));
    cycles_->setColumnCount(1);
    cycles_->setHeaderHidden(true);
    cycles_->header()->setSectionResizeMode(QHeaderView::Stretch);
    cycles_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    cycles_->setIndentation(14);
    cycles_->setItemDelegate(new CycleRowDelegate(cycles_));
    cycles_->setAccessibleName(tr("Циклические группы"));
    allCycles_ = new QPushButton(tr("Показать все циклы"));
    allCycles_->setObjectName(QStringLiteral("allCyclesButton"));
    allCycles_->setEnabled(false);
    includer_ = new QComboBox;
    includer_->setObjectName(QStringLiteral("checkIncluder"));
    header_ = new QComboBox;
    header_->setObjectName(QStringLiteral("checkHeader"));
    checkResult_ = new QLabel;
    checkResult_->setObjectName(QStringLiteral("checkResult"));
    checkResult_->setWordWrap(true); checkResult_->setTextFormat(Qt::PlainText);
    auto* check = new QWidget;
    auto* checkLayout = new QVBoxLayout(check);
    checkLayout->setContentsMargins(0, 0, 0, 0);
    auto* includerLabel = new QLabel;
    includerLabel->setProperty("translationSource", QStringLiteral("Файл"));
    checkLayout->addWidget(includerLabel);
    checkLayout->addWidget(includer_);
    auto* headerLabel = new QLabel;
    headerLabel->setProperty("translationSource", QStringLiteral("Добавить #include заголовка"));
    checkLayout->addWidget(headerLabel);
    checkLayout->addWidget(header_);
    checkLayout->addSpacing(8);
    checkLayout->addWidget(checkResult_);
    checkLayout->addStretch();
    // The right column describes whatever the tab above the graph shows.
    details_ = new QStackedWidget;
    details_->setObjectName(QStringLiteral("details"));
    details_->addWidget(panel("ЗАВИСИМОСТИ", dependencies_, header));
    details_->addWidget(panel("ЦИКЛИЧЕСКИЕ ЗАВИСИМОСТИ", cycles_, allCycles_));
    details_->addWidget(panel("ПРОВЕРКА НОВОГО #INCLUDE", check));
    horizontal->addWidget(details_);
    horizontal->setStretchFactor(0, 1); horizontal->setStretchFactor(1, 3); horizontal->setStretchFactor(2, 2);
    horizontal->setSizes({220, 580, 360}); horizontal->setChildrenCollapsible(false);

    diagnostics_ = new QTreeWidget;
    diagnostics_->setObjectName(QStringLiteral("diagnosticsTree"));
    diagnostics_->setHeaderLabels({tr("Уровень"), tr("Сообщение"), tr("Файл"), tr("Строка")});
    diagnostics_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    diagnostics_->setColumnWidth(0, 110); diagnostics_->setColumnWidth(2, 260);
    diagnostics_->setRootIsDecorated(false);
    auto* vertical = new QSplitter(Qt::Vertical);
    vertical->addWidget(horizontal); vertical->addWidget(panel("ДИАГНОСТИКА АНАЛИЗА", diagnostics_));
    vertical->setSizes({490, 160}); vertical->setStretchFactor(0, 1);
    layout->addWidget(vertical, 1); setCentralWidget(central);
    progress_ = new QProgressBar;
    progress_->setMaximumWidth(180); progress_->setTextVisible(false); progress_->hide();
    statusBar()->addPermanentWidget(progress_);
    statusBar()->showMessage(tr("Готово"));

    connect(browse_, &QPushButton::clicked, this, &MainWindow::chooseProject);
    connect(database_, &QPushButton::clicked, this, &MainWindow::chooseDatabase);
    connect(analyze_, &QPushButton::clicked, this, &MainWindow::runAnalysis);
    connect(export_, &QPushButton::clicked, this, &MainWindow::exportGraph);
    connect(project_, &QLineEdit::returnPressed, this, &MainWindow::runAnalysis);
    connect(&watcher_, &QFutureWatcher<AnalysisResult>::finished, this, &MainWindow::finishAnalysis);
    connect(files_, &QTreeWidget::currentItemChanged, this, &MainWindow::updateSelection);
    connect(files_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        const auto file = item->data(0, Qt::UserRole);
        if (file.isValid()) graph_->selectFile(nativePath(file.toString()));
    });
    connect(search_, &QLineEdit::textChanged, this, &MainWindow::filterFiles);
    connect(fit_, &QPushButton::clicked, graph_, &GraphPlaceholder::fitGraph);
    connect(groupFolders_, &QCheckBox::toggled, graph_, &GraphPlaceholder::setGrouped);
    connect(collapseFolders_, &QPushButton::clicked, this, [this] {
        groupFolders_->setChecked(true);
        graph_->collapseFolders();
    });
    connect(graph_, &GraphPlaceholder::fileActivated, this, &MainWindow::selectPath);
    connect(graph_, &GraphPlaceholder::folderActivated, this, &MainWindow::selectFolder);
    connect(dependencies_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (tabs_->currentIndex() == 0) showChain(item);
    });
    connect(cycles_->header(), &QHeaderView::sectionResized, cycles_, &QTreeWidget::doItemsLayout, Qt::QueuedConnection);
    connect(cycles_, &QTreeWidget::itemSelectionChanged, this, &MainWindow::updateCycleFocus);
    connect(allCycles_, &QPushButton::clicked, cycles_->selectionModel(), &QItemSelectionModel::clear);
    connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
        details_->setCurrentIndex(index);
        if (index == 0) showChain(dependencies_->currentItem());
        graph_->showCycleHighlights(index == 1);
        if (index == 2) checkInclude(); else graph_->showProposedInclude({}, {}, false);
    });
    for (auto* combo : {includer_, header_})
        connect(combo, &QComboBox::currentIndexChanged, this, &MainWindow::checkInclude);
    const auto activate = [this](QTreeWidgetItem* item) {
        if (item->data(0, Qt::UserRole).isValid()) selectPath(item->data(0, Qt::UserRole).toString());
    };
    for (auto* tree : {dependencies_, diagnostics_}) connect(tree, &QTreeWidget::itemActivated, this, activate);
    connect(language_, &QComboBox::currentIndexChanged, this, [this](int index) {
        const QString code = language_->itemData(index).toString();
        QSettings().setValue(QStringLiteral("ui/language"), code);
        language::setEnglish(code == QStringLiteral("en"));
        retranslateUi();
    });
    const QString code = QSettings().value(QStringLiteral("ui/language"), QStringLiteral("ru")).toString();
    language_->setCurrentIndex(code == QStringLiteral("en") ? 1 : 0);
    language::setEnglish(code == QStringLiteral("en"));
    retranslateUi();
}
MainWindow::~MainWindow() = default;

void MainWindow::exportGraph() {
    const auto jsonFilter = tr("JSON (*.json)");
    const auto mermaidFilter = tr("Mermaid (*.mmd)");
    QFileDialog dialog(this, tr("Экспорт графа"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilters({jsonFilter, mermaidFilter});
    dialog.setDefaultSuffix(QStringLiteral("json"));
    connect(&dialog, &QFileDialog::filterSelected, &dialog, [&dialog, mermaidFilter](const QString& filter) {
        dialog.setDefaultSuffix(filter == mermaidFilter ? QStringLiteral("mmd") : QStringLiteral("json"));
    });
    if (dialog.exec() != QDialog::Accepted) return;
    const auto filename = dialog.selectedFiles().value(0);
    if (filename.isEmpty()) return;
    try {
        const auto text = dialog.selectedNameFilter() == mermaidFilter
            ? headerscope::exportGraphMermaid(result_, displayedRoot_)
            : headerscope::exportGraphJson(result_, displayedRoot_);
        QSaveFile output(filename);
        const auto size = static_cast<qint64>(text.size());
        if (!output.open(QIODevice::WriteOnly) || output.write(text.data(), size) != size || !output.commit())
            throw std::runtime_error(output.errorString().toStdString());
        statusBar()->showMessage(tr("Граф экспортирован: %1").arg(filename));
    } catch (const std::exception& error) {
        statusBar()->showMessage(tr("Ошибка экспорта графа"));
        QMessageBox::warning(this, tr("Ошибка экспорта"), QString::fromUtf8(error.what()));
    }
}
void MainWindow::chooseProject() {
    const auto path = QFileDialog::getExistingDirectory(this, tr("Выберите папку проекта"), project_->text());
    if (!path.isEmpty()) { project_->setText(path); runAnalysis(); }
}
void MainWindow::chooseDatabase() {
    const auto path = QFileDialog::getOpenFileName(this, tr("База команд компиляции"), project_->text(),
                                                  QStringLiteral("compile_commands.json (compile_commands.json)"));
    if (!path.isEmpty()) { project_->setText(path); runAnalysis(); }
}
void MainWindow::setBusy(bool busy) {
    export_->setEnabled(!busy && result_.status != headerscope::ScanStatus::failed);
    project_->setEnabled(!busy); browse_->setEnabled(!busy);
    database_->setEnabled(!busy); analyze_->setEnabled(!busy);
    progress_->setRange(0, busy ? 0 : 1); progress_->setVisible(busy);
    analyze_->setText(busy ? tr("Анализ…") : tr("Анализировать"));
}
void MainWindow::runAnalysis() {
    if (!analyze_->isEnabled()) return;
    const QString input = project_->text().trimmed();
    scanInput_ = input;
    setBusy(true);
    // Clear the old view so a failed scan cannot leave a stale graph on screen.
    selectedFolder_.clear(); selectedTargets_.clear();
    hasResult_ = false; groupCount_ = 0;
    result_ = {}; dependencyGraph_.reset(); files_->clear(); dependencies_->clear(); diagnostics_->clear(); cycles_->clear();
    updateCycleFocus();
    selected_->setText(tr("Выберите файл")); selectedPath_->clear(); tabs_->setTabText(1, tr("Циклы"));
    graph_->setResult(result_);
    includer_->clear(); header_->clear(); checkInclude();
    summary_->setText(tr("Анализ проекта…"));
    statusBar()->showMessage(tr("Анализ выполняется в фоне"));
    watcher_.setFuture(QtConcurrent::run([input]() -> AnalysisResult {
        const auto failed = [](const QString& message) { return AnalysisResult{failure(message), {}, {}}; };
        try {
            if (input.isEmpty()) return failed(QString::fromUtf8("Укажите папку проекта или compile_commands.json."));
            QFileInfo info(input);
            if (!info.exists()) return failed(QString::fromUtf8("Указанный путь не существует: %1").arg(input));
            QString database = info.isDir() ? QDir(input).filePath(QStringLiteral("compile_commands.json")) : info.absoluteFilePath();
            if (!QFileInfo(database).isFile())
                return failed(QString::fromUtf8("Не найден compile_commands.json. Выберите его в каталоге сборки кнопкой выше."));
#if HEADERSCOPE_HAS_SCANNER
            return prepareAnalysis(headerscope::scanIncludes(nativePath(database)));
#else
            return failed(QString::fromUtf8("Сборщик Clang не включён. Соберите с HEADERSCOPE_BUILD_SCANNER=ON."));
#endif
        } catch (const std::exception& error) {
            return failed(QString::fromUtf8("Ошибка анализа: %1").arg(QString::fromUtf8(error.what())));
        } catch (...) { return failed(QString::fromUtf8("Неизвестная ошибка анализа.")); }
    }));
}
void MainWindow::finishAnalysis() {
    setBusy(false);
    showAnalysis(watcher_.future().takeResult());
}
void MainWindow::showResult(headerscope::ScanResult result) {
    scanInput_ = project_->text().trimmed();
    showAnalysis(prepareAnalysis(std::move(result)));
}
MainWindow::AnalysisResult MainWindow::prepareAnalysis(headerscope::ScanResult scan) {
    AnalysisResult analysis{std::move(scan), {}, {}};
    if (analysis.scan.status != headerscope::ScanStatus::failed) {
        analysis.graph = std::make_shared<headerscope::DependencyGraph>(analysis.scan);
        for (const auto& group : analysis.graph->cycles()) {
            analysis.groups.push_back({group.members.size(), analysis.graph->simpleCycles(group)});
        }
    }
    return analysis;
}
void MainWindow::showAnalysis(AnalysisResult analysis) {
    hasResult_ = true; groupCount_ = analysis.groups.size();
    result_ = std::move(analysis.scan);
    // Normalize the file list at the UI boundary, including edge endpoints.
    std::set<std::filesystem::path> unique(result_.files.begin(), result_.files.end());
    for (const auto& edge : result_.edges) { unique.insert(edge.includer); unique.insert(edge.included); }
    result_.files.assign(unique.begin(), unique.end());
    for (const auto& diagnostic : result_.diagnostics) {
        auto* row = new QTreeWidgetItem(diagnostics_, {diagnostic.severity == headerscope::DiagnosticSeverity::error
            ? tr("Ошибка") : tr("Предупреждение"), QString::fromUtf8(diagnostic.message.c_str()),
            diagnostic.file ? pathText(*diagnostic.file) : QString(),
            diagnostic.line ? QString::number(*diagnostic.line) : QString()});
        language::setItemText(row, 0, diagnostic.severity == headerscope::DiagnosticSeverity::error ? "Ошибка" : "Предупреждение");
        row->setData(1, Qt::UserRole + 42, QString::fromUtf8(diagnostic.message.c_str()));
        row->setText(1, language::diagnosticText(QString::fromUtf8(diagnostic.message.c_str())));
        if (diagnostic.file) row->setData(0, Qt::UserRole, pathText(*diagnostic.file));
        row->setForeground(0, QColor(diagnostic.severity == headerscope::DiagnosticSeverity::error ? "#ef4444" : "#d97706"));
        row->setToolTip(1, row->text(1)); row->setToolTip(2, row->text(2));
    }
    if (result_.status == headerscope::ScanStatus::failed) {
      export_->setEnabled(false);
      updateSummary();
      emit analysisFinished();
      return;
    }
    dependencyGraph_ = std::move(analysis.graph);
    const QFileInfo input(scanInput_);
    QString root;
    if (input.exists()) {
        const auto canonicalPath = input.canonicalFilePath();
        const QFileInfo resolved(canonicalPath.isEmpty() ? input.absoluteFilePath() : canonicalPath);
        root = resolved.isDir() ? resolved.absoluteFilePath() : resolved.absolutePath();
    } else if (!result_.files.empty()) {
        root = pathText(result_.files.front().parent_path());
    }
    displayedRoot_ = nativePath(root);
    export_->setEnabled(true);
    showingResult_ = true;
    populateFileTree(files_, result_.files, root);
    graph_->setResult(result_, displayedRoot_);
    showCycles(analysis.groups);
    for (const auto& file : result_.files) {
        for (auto* combo : {includer_, header_}) combo->addItem(relativeName(file, root), pathText(file));
    }
    header_->setCurrentIndex(-1);
    filterFiles(search_->text());
    if (!files_->currentItem())
        for (QTreeWidgetItemIterator it(files_); *it; ++it)
            if ((*it)->data(0, Qt::UserRole).isValid()) { files_->setCurrentItem(*it); break; }
    showingResult_ = false;
    updateSummary(); emit analysisFinished();
}
void MainWindow::filterFiles(const QString& text) {
    for (int i = 0; i < files_->topLevelItemCount(); ++i) filterItem(files_->topLevelItem(i), text);
}
void MainWindow::selectPath(const QString& path) {
    for (QTreeWidgetItemIterator it(files_); *it; ++it) {
        if ((*it)->data(0, Qt::UserRole).toString() == path) {
            search_->clear(); files_->setCurrentItem(*it); files_->scrollToItem(*it); return;
        }
    }
}
void MainWindow::updateSelection() {
    dependencies_->clear();
    const auto* row = files_->currentItem();
    if (!row || !dependencyGraph_) return;
    if (!row->data(0, Qt::UserRole).isValid()) {
        const auto folder = row->data(0, Qt::UserRole + 2).toString();
        if (!folder.isEmpty()) showFolderDependencies(folder);
        return;
    }
    const auto file = nativePath(row->data(0, Qt::UserRole).toString());
    selectedFolder_.clear(); selectedTargets_ = {file};
    selected_->setText(pathText(file.filename())); graph_->selectFile(file, !showingResult_);
    selectedPath_->setText(row->data(0, Qt::UserRole + 1).toString());
    auto* incoming = new QTreeWidgetItem(dependencies_, {tr("Зависимые файлы")});
    auto* outgoing = new QTreeWidgetItem(dependencies_, {tr("Включает напрямую")});
    for (const auto& dependent : dependencyGraph_->dependents(file)) {
        auto* item = new QTreeWidgetItem(incoming, {relativeName(dependent, pathText(displayedRoot_))});
        item->setData(0, Qt::UserRole, pathText(dependent));
        const bool direct = std::any_of(result_.edges.begin(), result_.edges.end(), [&](const auto& edge) {
            return edge.includer == dependent && edge.included == file;
        });
        language::setItemText(item, 1, direct ? "прямо" : "косвенно");
        QStringList chain;
        if (const auto path = dependencyGraph_->shortestPath(dependent, file))
            for (const auto& step : *path) chain << pathText(step);
        item->setToolTip(0, chain.join(QStringLiteral(" → ")));
    }
    for (const auto& edge : result_.edges) {
        if (edge.includer != file) continue;
        auto* item = new QTreeWidgetItem(outgoing, {relativeName(edge.included, pathText(displayedRoot_)), tr("стр. %1").arg(edge.line)});
        language::setItemText(item, 1, "стр. %1", {QString::number(edge.line)});
        item->setData(0, Qt::UserRole, pathText(edge.included)); item->setToolTip(0, pathText(edge.included));
    }
    for (auto* group : {incoming, outgoing}) {
        const int count = group->childCount();
        language::setItemText(group, 0, group == incoming ? "Зависимые файлы · %1" : "Включает напрямую · %1", {QString::number(count)});
        if (!count) language::setItemText(new QTreeWidgetItem(group), 0, group == incoming ? "Нет зависимых файлов" : "Нет подключений");
        QFont bold = group->font(0); bold.setBold(true); group->setFont(0, bold);
        group->setFirstColumnSpanned(true); group->setFlags(Qt::ItemIsEnabled);
        for (int i = 0; i < group->childCount(); ++i) {
            group->child(i)->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            group->child(i)->setForeground(1, QColor(theme().muted));
        }
    }
    dependencies_->expandAll();
    // The check starts from the file chosen in the tree; the header stays the user's pick.
    includer_->setCurrentIndex(includer_->findData(pathText(file)));
    if (tabs_->currentIndex() == 2) checkInclude();
}
// A new includer → header edge closes a cycle exactly when header already reaches includer.
void MainWindow::checkInclude() {
    const auto from = includer_->currentData().toString();
    const auto to = header_->currentData().toString();
    std::set<std::pair<QString, QString>> chain;
    QString message;
    QString style;
    bool showProposal = false;
    bool closesCycle = false;
    if (from.isEmpty() || to.isEmpty() || !dependencyGraph_) {
        message = tr("Выберите файл и заголовок, чтобы узнать, замкнётся ли цикл.");
    } else {
        const auto includer = nativePath(from);
        const auto header = nativePath(to);
        const auto directIncludes = dependencyGraph_->includes(includer);
        if (std::ranges::find(directIncludes, header) != directIncludes.end()) {
            message = tr("Подключение уже существует.");
        } else if (const auto path = dependencyGraph_->shortestPath(header, includer)) {
            if (includer == header) {
                message = tr("Цикл замкнётся: файл подключит сам себя.");
            } else {
                QStringList names{pathText(includer.filename())};
                for (std::size_t i = 0; i < path->size(); ++i) {
                    names << pathText((*path)[i].filename());
                    if (i + 1 < path->size()) chain.emplace(pathText((*path)[i]), pathText((*path)[i + 1]));
                }
                message = tr("Цикл замкнётся: %1\nПричина: %2 уже подключает %3%4.")
                    .arg(names.join(QStringLiteral(" → ")), names[1], names[0],
                         path->size() > 2 ? tr(" косвенно") : QString());
            }
            style = QStringLiteral("color: #ef4444;");
            showProposal = true;
            closesCycle = true;
        } else if (result_.status == headerscope::ScanStatus::partial) {
            message = tr("В доступных данных цикл не найден. Анализ завершился частично.");
        } else {
            message = tr("Цикл не возникнет: %1 не подключает %2 ни прямо, ни косвенно.")
                .arg(pathText(header.filename()), pathText(includer.filename()));
            style = QStringLiteral("color: #16a34a;");
            showProposal = true;
        }
    }
    checkResult_->setText(message);
    checkResult_->setStyleSheet(style);
    if (tabs_->currentIndex() != 2) return;
    graph_->highlightChain(std::move(chain));
    if (showProposal) graph_->showProposedInclude(from, to, closesCycle);
    else graph_->showProposedInclude({}, {}, false);
}
// Select a collapsed graph folder without pretending it is a file.
void MainWindow::selectFolder(const QString& folder) {
    if (!dependencyGraph_) return;
    QTreeWidgetItem* matching = nullptr;
    for (QTreeWidgetItemIterator it(files_); *it; ++it)
        if ((*it)->data(0, Qt::UserRole + 2).toString() == folder) { matching = *it; break; }
    if (matching && files_->currentItem() != matching) {
        search_->clear(); files_->setCurrentItem(matching); files_->scrollToItem(matching);
    } else {
        if (!matching) files_->setCurrentItem(nullptr);
        showFolderDependencies(folder);
    }
}
void MainWindow::updateFolderHeading() {
    selected_->setText(selectedFolder_ == QStringLiteral(":root-files:") ? tr("Файлы в корне")
        : relativeName(nativePath(selectedFolder_), pathText(displayedRoot_)) + QLatin1Char('/'));
    selectedPath_->setText(tr("Папка · %1 файлов").arg(selectedTargets_.size()));
}
void MainWindow::showFolderDependencies(const QString& folder) {
    dependencies_->clear(); selectedFolder_ = folder; selectedTargets_.clear();
    auto members = graph_->folderMembers(folder);
    if (members.isEmpty()) for (const auto& file : result_.files) {
        const auto relative = file.lexically_relative(nativePath(folder));
        if (!relative.empty() && *relative.begin() != "..") members << pathText(file);
    }
    for (const auto& member : members) selectedTargets_.insert(nativePath(member));
    updateFolderHeading(); graph_->selectFolder(folder);
    auto* incoming = new QTreeWidgetItem(dependencies_);
    auto* outgoing = new QTreeWidgetItem(dependencies_);
    const auto reached = dependentClosure(result_, selectedTargets_);
    std::set<std::filesystem::path> direct, included;
    std::map<std::filesystem::path, QStringList> includeDetails;
    for (const auto& edge : result_.edges) {
        if (selectedTargets_.contains(edge.included) && !selectedTargets_.contains(edge.includer)) direct.insert(edge.includer);
        if (selectedTargets_.contains(edge.includer) && !selectedTargets_.contains(edge.included)) {
            included.insert(edge.included);
            includeDetails[edge.included] << pathText(edge.includer) + QStringLiteral(":%1 → ").arg(edge.line) + pathText(edge.included);
        }
    }
    for (const auto& file : reached) if (!selectedTargets_.contains(file)) {
        auto* item = new QTreeWidgetItem(incoming, {relativeName(file, pathText(displayedRoot_))});
        item->setData(0, Qt::UserRole, pathText(file)); item->setToolTip(0, pathText(file));
        language::setItemText(item, 1, direct.contains(file) ? "прямо" : "косвенно");
    }
    for (const auto& file : included) {
        auto* item = new QTreeWidgetItem(outgoing, {relativeName(file, pathText(displayedRoot_))});
        item->setData(0, Qt::UserRole, pathText(file)); item->setToolTip(0, includeDetails[file].join(QLatin1Char('\n')));
    }
    for (auto* group : {incoming, outgoing}) {
        const int count = group->childCount();
        language::setItemText(group, 0, group == incoming ? "Зависимые файлы · %1" : "Включает напрямую · %1", {QString::number(count)});
        if (!count) language::setItemText(new QTreeWidgetItem(group), 0, group == incoming ? "Нет зависимых файлов" : "Нет подключений");
        auto bold = group->font(0); bold.setBold(true); group->setFont(0, bold);
        group->setFirstColumnSpanned(true); group->setFlags(Qt::ItemIsEnabled);
    }
    dependencies_->expandAll();
    includer_->setCurrentIndex(-1);
    if (tabs_->currentIndex() == 2) checkInclude();
}
// Highlight incoming paths or direct outgoing connections for the selected file/folder.
void MainWindow::showChain(QTreeWidgetItem* dependent) {
    std::set<std::pair<QString, QString>> chain;
    if (dependent && dependencyGraph_ && dependent->data(0, Qt::UserRole).isValid() && !selectedTargets_.empty()) {
        const auto endpoint = nativePath(dependent->data(0, Qt::UserRole).toString());
        if (dependent->parent() == dependencies_->topLevelItem(0)) {
            const auto reachesTarget = dependentClosure(result_, selectedTargets_);
            std::vector<std::filesystem::path> queue{endpoint};
            std::set<std::filesystem::path> seen{endpoint};
            for (std::size_t i = 0; i < queue.size(); ++i) {
                if (selectedTargets_.contains(queue[i])) continue;
                for (const auto& next : dependencyGraph_->includes(queue[i])) {
                    if (!reachesTarget.contains(next)) continue;
                    chain.emplace(pathText(queue[i]), pathText(next));
                    if (seen.insert(next).second) queue.push_back(next);
                }
            }
        } else if (dependent->parent() == dependencies_->topLevelItem(1)) {
            for (const auto& edge : result_.edges)
                if (selectedTargets_.contains(edge.includer) && edge.included == endpoint)
                    chain.emplace(pathText(edge.includer), pathText(endpoint));
        }
    }
    graph_->highlightChain(std::move(chain));
}
void MainWindow::showCycles(const std::vector<CycleDetails>& groups) {
    cycles_->clear();
    updateCycleFocus();
    GraphPlaceholder::EdgeColors edgeColors;
    cycleCount_ = 0;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const auto& [fileCount, chains] = groups[i];
        auto* group = new QTreeWidgetItem(cycles_, {tr("Группа %1 · файлов: %2 · циклов: %3")
                                                   .arg(i + 1).arg(fileCount).arg(chains.size())});
        language::setItemText(group, 0, "Группа %1 · файлов: %2 · циклов: %3",
            {QString::number(i + 1), QString::number(fileCount), QString::number(chains.size())});
        group->setFlags(Qt::ItemIsEnabled);
        auto groupFont = group->font(0);
        groupFont.setBold(true);
        group->setFont(0, groupFont);
        for (const auto& chain : chains) {
            const auto color = cycleColor(cycleCount_++);
            QStringList names, paths;
            for (const auto& step : chain) {
                names << pathText(step.filename());
                paths << pathText(step);
            }
            auto* cycle = new QTreeWidgetItem(group, {names.join(QStringLiteral(" → "))});
            QPixmap marker(12, 12);
            marker.fill(color);
            cycle->setIcon(0, QIcon(marker));
            cycle->setToolTip(0, paths.join(QStringLiteral(" → ")));
            cycle->setData(0, CycleFiles, paths);
            cycle->setData(0, CycleColor, color);
            for (qsizetype k = 0; k + 1 < paths.size(); ++k) {
                edgeColors[{paths[k], paths[k + 1]}].push_back(color);
            }
        }
    }
    if (groups.empty()) {
        auto* empty = new QTreeWidgetItem(cycles_, {tr("Циклов не найдено")});
        language::setItemText(empty, 0, "Циклов не найдено");
        empty->setFlags(Qt::ItemIsEnabled);
    }
    cycles_->expandAll();
    tabs_->setTabText(1, tr("Циклы (%1)").arg(cycleCount_));
    graph_->setCycleHighlights(std::move(edgeColors));
}
void MainWindow::updateCycleFocus() {
    const auto selection = cycles_->selectedItems();
    const auto* item = selection.isEmpty() ? nullptr : selection.front();
    const auto files = item ? item->data(0, CycleFiles).toStringList() : QStringList{};
    const auto color = item ? item->data(0, CycleColor).value<QColor>() : QColor{};
    allCycles_->setEnabled(!files.isEmpty());
    graph_->focusCycle(files, color);
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) retranslateUi();
}
void MainWindow::updateSummary() {
    if (!analyze_->isEnabled()) {
        summary_->setText(tr("Анализ проекта…"));
        statusBar()->showMessage(tr("Анализ выполняется в фоне"));
    } else if (!hasResult_) {
        summary_->setText(tr("Выберите папку проекта и нажмите «Анализировать»."));
        statusBar()->showMessage(tr("Готово"));
    } else if (result_.status == headerscope::ScanStatus::failed) {
        summary_->setText(tr("Анализ не выполнен. Подробности в диагностике."));
        statusBar()->showMessage(tr("Ошибка анализа"));
    } else {
        const QString status = result_.status == headerscope::ScanStatus::partial ? tr("Частичный анализ") : tr("Анализ завершён");
        summary_->setText(tr("%1 · %2 файлов · %3 директив #include · %4 циклических групп · %5 сообщений")
            .arg(status).arg(result_.files.size()).arg(result_.edges.size()).arg(groupCount_).arg(result_.diagnostics.size()));
        statusBar()->showMessage(status);
    }
}
void MainWindow::retranslateUi() {
    heading_->setText(tr("HeaderScope · зависимости C/C++"));
    language_->setAccessibleName(tr("Язык")); language_->setToolTip(tr("Язык"));
    project_->setPlaceholderText(tr("Папка проекта или compile_commands.json"));
    project_->setAccessibleName(tr("Путь к проекту или базе команд"));
    browse_->setText(tr("Папка…"));
    analyze_->setText(analyze_->isEnabled() ? tr("Анализировать") : tr("Анализ…"));
    export_->setText(tr("Экспорт графа…"));
    fit_->setText(tr("Уместить граф"));
    groupFolders_->setText(tr("Группировать по папкам"));
    groupFolders_->setToolTip(tr("Двойной щелчок раскрывает папку. Правая кнопка — свернуть родительскую папку."));
    collapseFolders_->setText(tr("Свернуть всё"));
    files_->setAccessibleName(tr("Файлы проекта"));
    search_->setPlaceholderText(tr("Найти файл…"));
    tabs_->setTabText(0, tr("Граф"));
    tabs_->setTabText(1, dependencyGraph_ ? tr("Циклы (%1)").arg(cycleCount_) : tr("Циклы"));
    tabs_->setTabText(2, tr("Проверка #include"));
    includer_->setPlaceholderText(tr("Выберите файл"));
    includer_->setAccessibleName(tr("Файл, в который добавляется #include"));
    header_->setPlaceholderText(tr("Выберите заголовок"));
    header_->setAccessibleName(tr("Подключаемый заголовок"));
    dependencies_->setAccessibleName(tr("Подключения и зависимые файлы"));
    cycles_->setAccessibleName(tr("Циклические группы"));
    allCycles_->setText(tr("Показать все циклы"));
    if (auto* toggle = findChild<QAbstractButton*>(QStringLiteral("themeToggle"))) toggle->setAccessibleName(tr("Тёмная тема"));
    diagnostics_->setHeaderLabels({tr("Уровень"), tr("Сообщение"), tr("Файл"), tr("Строка")});
    const auto* current = files_->currentItem();
    if (!selectedFolder_.isEmpty()) updateFolderHeading();
    else if (!current || !current->data(0, Qt::UserRole).isValid()) selected_->setText(tr("Выберите файл"));
    for (auto* label : findChildren<QLabel*>()) {
        const auto source = label->property("translationSource");
        if (source.isValid()) label->setText(language::text(source.toString().toUtf8().constData()));
    }
    for (auto* tree : {dependencies_, cycles_, diagnostics_})
        for (QTreeWidgetItemIterator it(tree); *it; ++it) language::retranslateItem(*it);
    for (int i = 0; i < diagnostics_->topLevelItemCount(); ++i) {
        auto* row = diagnostics_->topLevelItem(i);
        row->setText(1, language::diagnosticText(row->data(1, Qt::UserRole + 42).toString()));
        row->setToolTip(1, row->text(1));
    }
    checkInclude();
    updateSummary();
}
