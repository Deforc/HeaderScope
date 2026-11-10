#include "main_window.hpp"
#include "graph_placeholder.hpp"
#include "path_utils.hpp"
#include "theme.hpp"
#include <headerscope/demo/fixture.hpp>
#include <headerscope/graph.hpp>
#if HEADERSCOPE_HAS_SCANNER
#include <headerscope/scanner.hpp>
#endif
#include <QtConcurrent/QtConcurrentRun>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <algorithm>
#include <exception>
#include <set>

namespace {
headerscope::ScanResult failure(const QString& message) {
    headerscope::ScanResult result;
    result.diagnostics.push_back({headerscope::DiagnosticSeverity::error,
        message.toUtf8().toStdString(), std::nullopt, std::nullopt});
    return result;
}
QString relativeName(const std::filesystem::path& file, const QString& root) {
    return QDir(root).relativeFilePath(pathText(file));
}
// Hides leaves not matching `text`, and directories left without visible children. Returns true if visible.
bool filterItem(QTreeWidgetItem* item, const QString& text) {
    bool visible = false;
    if (!item->childCount()) visible = item->data(0, Qt::UserRole + 1).toString().contains(text, Qt::CaseInsensitive);
    for (int i = 0; i < item->childCount(); ++i) visible |= filterItem(item->child(i), text);
    item->setHidden(!visible);
    return visible;
}
QWidget* panel(const QString& title, QWidget* content, QWidget* extra = nullptr) {
    auto* widget = new QWidget;
    widget->setObjectName(QStringLiteral("panel"));
    auto* layout = new QVBoxLayout(widget);
    auto* label = new QLabel(title);
    label->setObjectName(QStringLiteral("panelTitle"));
    layout->addWidget(label);
    if (extra) layout->addWidget(extra);
    layout->addWidget(content, 1);
    return widget;
}
}

void populateFileTree(QTreeWidget* tree, const std::vector<std::filesystem::path>& files, const QString& root) {
    QHash<QString, QTreeWidgetItem*> directories;
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
            }
            parent = directory;
        }
        const QString name = parts.isEmpty() ? pathText(file) : parts.back();
        // Search matches the relative path, so typing a directory name finds the files inside it.
        auto* leaf = parent ? new QTreeWidgetItem(parent, {name}) : new QTreeWidgetItem(tree, {name});
        leaf->setData(0, Qt::UserRole, pathText(file)); leaf->setToolTip(0, pathText(file));
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
    auto* heading = new QLabel(tr("HeaderScope · зависимости C/C++"));
    QFont titleFont = heading->font(); titleFont.setPointSize(18); titleFont.setBold(true);
    heading->setFont(titleFont);
    layout->addWidget(heading);

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
    mode_ = new QComboBox;
    mode_->setObjectName(QStringLiteral("analysisMode"));
    mode_->setAccessibleName(tr("Источник данных"));
    mode_->addItems({tr("Демо: цепочка"), tr("Демо: цикл"), tr("Демо: ошибка заголовка"), tr("Проект · Clang")});
#if !HEADERSCOPE_HAS_SCANNER
    mode_->setItemData(3, tr("Нужна сборка с HEADERSCOPE_BUILD_SCANNER=ON"), Qt::ToolTipRole);
#endif
    analyze_ = new QPushButton(tr("Анализировать"));
    analyze_->setObjectName(QStringLiteral("analyzeButton"));
    analyze_->setDefault(true);
    auto* fit = new QPushButton(tr("Уместить граф"));
    actions->addWidget(mode_); actions->addWidget(analyze_); actions->addStretch(); actions->addWidget(fit);
    layout->addLayout(actions);
    summary_ = new QLabel(tr("Выберите проект или запустите демо без настройки Clang."));
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
    horizontal->addWidget(panel(tr("ФАЙЛЫ"), files_, search_));
    graph_ = new GraphPlaceholder;
    horizontal->addWidget(panel(tr("ГРАФ · A → B означает: A подключает B"), graph_));
    dependencies_ = new QTreeWidget;
    dependencies_->setObjectName(QStringLiteral("dependenciesTree"));
    dependencies_->setHeaderLabels({tr("Файл / группа"), tr("Связь / строка")});
    dependencies_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    dependencies_->setMinimumWidth(260);
    dependencies_->setAccessibleName(tr("Подключения и зависимые файлы"));
    selected_ = new QLabel(tr("Выберите файл")); selected_->setWordWrap(true);
    selected_->setObjectName(QStringLiteral("selectedFile"));
    selected_->setTextFormat(Qt::PlainText);
    horizontal->addWidget(panel(tr("ЗАВИСИМОСТИ"), dependencies_, selected_));
    horizontal->setStretchFactor(0, 1); horizontal->setStretchFactor(1, 3); horizontal->setStretchFactor(2, 2);
    horizontal->setSizes({220, 580, 360}); horizontal->setChildrenCollapsible(false);

    diagnostics_ = new QTreeWidget;
    diagnostics_->setObjectName(QStringLiteral("diagnosticsTree"));
    diagnostics_->setHeaderLabels({tr("Уровень"), tr("Сообщение"), tr("Файл"), tr("Строка")});
    diagnostics_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    diagnostics_->setColumnWidth(0, 110); diagnostics_->setColumnWidth(2, 260);
    diagnostics_->setRootIsDecorated(false);
    auto* vertical = new QSplitter(Qt::Vertical);
    vertical->addWidget(horizontal); vertical->addWidget(panel(tr("ДИАГНОСТИКА АНАЛИЗА"), diagnostics_));
    vertical->setSizes({490, 160}); vertical->setStretchFactor(0, 1);
    layout->addWidget(vertical, 1); setCentralWidget(central);
    progress_ = new QProgressBar;
    progress_->setMaximumWidth(180); progress_->setTextVisible(false); progress_->hide();
    statusBar()->addPermanentWidget(progress_);
    statusBar()->showMessage(tr("Готово · демоданные"));

    connect(browse_, &QPushButton::clicked, this, &MainWindow::chooseProject);
    connect(database_, &QPushButton::clicked, this, &MainWindow::chooseDatabase);
    connect(analyze_, &QPushButton::clicked, this, &MainWindow::runAnalysis);
    connect(project_, &QLineEdit::returnPressed, this, &MainWindow::runAnalysis);
    connect(&watcher_, &QFutureWatcher<headerscope::ScanResult>::finished, this, &MainWindow::finishAnalysis);
    connect(files_, &QTreeWidget::currentItemChanged, this, &MainWindow::updateSelection);
    connect(search_, &QLineEdit::textChanged, this, &MainWindow::filterFiles);
    connect(fit, &QPushButton::clicked, graph_, &GraphPlaceholder::fitGraph);
    connect(graph_, &GraphPlaceholder::fileActivated, this, &MainWindow::selectPath);
    connect(dependencies_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        if (item->data(0, Qt::UserRole).isValid()) selectPath(item->data(0, Qt::UserRole).toString());
    });
    connect(diagnostics_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        if (item->data(0, Qt::UserRole).isValid()) selectPath(item->data(0, Qt::UserRole).toString());
    });
}
MainWindow::~MainWindow() = default;

void MainWindow::chooseProject() {
    const auto path = QFileDialog::getExistingDirectory(this, tr("Выберите папку проекта"), project_->text());
    if (!path.isEmpty()) { project_->setText(path); mode_->setCurrentIndex(3); }
}
void MainWindow::chooseDatabase() {
    const auto path = QFileDialog::getOpenFileName(this, tr("База команд компиляции"), project_->text(),
                                                  QStringLiteral("compile_commands.json (compile_commands.json)"));
    if (!path.isEmpty()) { project_->setText(path); mode_->setCurrentIndex(3); }
}
void MainWindow::setBusy(bool busy) {
    project_->setEnabled(!busy); mode_->setEnabled(!busy); browse_->setEnabled(!busy);
    database_->setEnabled(!busy); analyze_->setEnabled(!busy);
    progress_->setRange(0, busy ? 0 : 1); progress_->setVisible(busy);
    analyze_->setText(busy ? tr("Анализ…") : tr("Анализировать"));
}
void MainWindow::runAnalysis() {
    if (!analyze_->isEnabled()) return;
    const int mode = mode_->currentIndex();
    const QString input = project_->text().trimmed();
    setBusy(true);
    // Clear the old view so a failed scan cannot leave a stale graph on screen.
    result_ = {}; dependencyGraph_.reset(); files_->clear(); dependencies_->clear(); diagnostics_->clear();
    selected_->setText(tr("Выберите файл")); graph_->setResult(result_);
    summary_->setText(mode == 3 ? tr("Анализ проекта…") : tr("Анализ демонстрационных данных…"));
    statusBar()->showMessage(tr("Анализ выполняется в фоне"));
    watcher_.setFuture(QtConcurrent::run([mode, input]() -> headerscope::ScanResult {
        try {
            if (mode != 3) {
                // Fixture paths are synthetic and do not require files on disk.
                auto result = headerscope::demo::makeIncludeChain(nativePath(QDir::tempPath() + QStringLiteral("/HeaderScope-demo")));
                const auto root = result.files.front().parent_path();
                if (mode == 1) result.edges.push_back({root / "b.h", root / "a.h", 4});
                if (mode == 2) {
                    result.status = headerscope::ScanStatus::partial;
                    result.diagnostics.push_back({headerscope::DiagnosticSeverity::error,
                        "Demo: 'missing.h' file not found", root / "a.h", 8});
                }
                return result;
            }
            if (input.isEmpty()) return failure(tr("Укажите папку проекта или compile_commands.json."));
            QFileInfo info(input);
            if (!info.exists()) return failure(tr("Указанный путь не существует: %1").arg(input));
            QString database = info.isDir() ? QDir(input).filePath(QStringLiteral("compile_commands.json")) : info.absoluteFilePath();
            if (!QFileInfo(database).isFile())
                return failure(tr("Не найден compile_commands.json. Выберите его в каталоге сборки кнопкой выше."));
#if HEADERSCOPE_HAS_SCANNER
            return headerscope::scanIncludes(nativePath(database));
#else
            return failure(tr("Сборщик Clang не включён. Соберите с HEADERSCOPE_BUILD_SCANNER=ON или выберите деморежим."));
#endif
        } catch (const std::exception& error) {
            return failure(tr("Ошибка анализа: %1").arg(QString::fromUtf8(error.what())));
        } catch (...) { return failure(tr("Неизвестная ошибка анализа.")); }
    }));
}
void MainWindow::finishAnalysis() {
    result_ = watcher_.result();
    setBusy(false);
    // Normalize the file list at the UI boundary, including edge endpoints.
    std::set<std::filesystem::path> unique(result_.files.begin(), result_.files.end());
    for (const auto& edge : result_.edges) { unique.insert(edge.includer); unique.insert(edge.included); }
    result_.files.assign(unique.begin(), unique.end());
    for (const auto& diagnostic : result_.diagnostics) {
        auto* row = new QTreeWidgetItem(diagnostics_, {diagnostic.severity == headerscope::DiagnosticSeverity::error
            ? tr("Ошибка") : tr("Предупреждение"), QString::fromUtf8(diagnostic.message.c_str()),
            diagnostic.file ? pathText(*diagnostic.file) : QString(),
            diagnostic.line ? QString::number(*diagnostic.line) : QString()});
        if (diagnostic.file) row->setData(0, Qt::UserRole, pathText(*diagnostic.file));
        row->setForeground(0, QColor(diagnostic.severity == headerscope::DiagnosticSeverity::error ? "#ef4444" : "#d97706"));
        row->setToolTip(1, row->text(1)); row->setToolTip(2, row->text(2));
    }
    if (result_.status == headerscope::ScanStatus::failed) {
        summary_->setText(tr("Анализ не выполнен. Подробности в диагностике."));
        statusBar()->showMessage(tr("Ошибка анализа")); emit analysisFinished(); return;
    }
    dependencyGraph_ = std::make_unique<headerscope::DependencyGraph>(result_);
    const QString root = mode_->currentIndex() == 3
        ? (QFileInfo(project_->text()).isDir() ? project_->text() : QFileInfo(project_->text()).absolutePath())
        : pathText(result_.files.empty() ? std::filesystem::path{} : result_.files.front().parent_path());
    populateFileTree(files_, result_.files, root);
    graph_->setResult(result_);
    filterFiles(search_->text());
    if (!files_->currentItem())
        for (QTreeWidgetItemIterator it(files_); *it; ++it)
            if ((*it)->data(0, Qt::UserRole).isValid()) { files_->setCurrentItem(*it); break; }
    const QString status = result_.status == headerscope::ScanStatus::partial ? tr("Частичный анализ") : tr("Анализ завершён");
    summary_->setText(tr("%1 · %2 файлов · %3 директив #include · %4 сообщений%5")
        .arg(status).arg(result_.files.size()).arg(result_.edges.size()).arg(result_.diagnostics.size())
        .arg(mode_->currentIndex() == 3 ? QString() : tr(" · ДЕМОДАННЫЕ")));
    statusBar()->showMessage(status); emit analysisFinished();
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
    if (!row || !row->data(0, Qt::UserRole).isValid() || !dependencyGraph_) return;
    const auto file = nativePath(row->data(0, Qt::UserRole).toString());
    selected_->setText(pathText(file)); graph_->selectFile(file);
    auto* outgoing = new QTreeWidgetItem(dependencies_, {tr("Прямые подключения")});
    auto* incoming = new QTreeWidgetItem(dependencies_, {tr("Зависимые файлы · прямые и косвенные")});
    for (const auto& edge : result_.edges) {
        if (edge.includer != file) continue;
        auto* item = new QTreeWidgetItem(outgoing, {pathText(edge.included.filename()), tr("строка %1").arg(edge.line)});
        item->setData(0, Qt::UserRole, pathText(edge.included)); item->setToolTip(0, pathText(edge.included));
    }
    for (const auto& dependent : dependencyGraph_->dependents(file)) {
        auto* item = new QTreeWidgetItem(incoming, {pathText(dependent.filename())});
        item->setData(0, Qt::UserRole, pathText(dependent));
        const bool direct = std::any_of(result_.edges.begin(), result_.edges.end(), [&](const auto& edge) {
            return edge.includer == dependent && edge.included == file;
        });
        item->setText(1, direct ? tr("Прямая") : tr("Косвенная"));
        QStringList chain;
        if (const auto path = dependencyGraph_->shortestPath(dependent, file))
            for (const auto& step : *path) chain << pathText(step);
        item->setToolTip(0, chain.join(QStringLiteral(" → ")));
    }
    if (!outgoing->childCount()) new QTreeWidgetItem(outgoing, {tr("Нет подключений")});
    if (!incoming->childCount()) new QTreeWidgetItem(incoming, {tr("Нет зависимых файлов")});
    dependencies_->expandAll();
}
