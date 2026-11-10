#pragma once
#include <QGuiApplication>
#include <QString>
#include <QStyleHints>

// Look only: every colour of the window lives here, in a light and a dark variant.
struct Theme {
    const char *bg, *card, *line, *text, *muted, *accent, *hover, *field, *sel;
    const char *graphBg, *edge, *node, *sourceNode;
};

inline const Theme& theme() {
    static const Theme light{"#f1f5f9", "#ffffff", "#e2e8f0", "#0f172a", "#64748b", "#2563eb", "#1d4ed8",
                             "#ffffff", "#dbeafe", "#f8fafc", "#94a3b8", "#ffffff", "#eef2ff"};
    static const Theme dark{"#0f172a", "#1e293b", "#334155", "#e2e8f0", "#94a3b8", "#3b82f6", "#60a5fa",
                            "#0f172a", "#1e3a8a", "#0b1220", "#64748b", "#1e293b", "#312e81"};
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark ? dark : light;
}

inline QString themeStyleSheet() {
    QString sheet = QStringLiteral(R"(
QMainWindow, QWidget#central, QStatusBar { background: @bg; }
QLabel { color: @text; background: transparent; }
QLabel#panelTitle { color: @muted; font-size: 11px; font-weight: 700; }
QLabel#analysisSummary, QLabel#selectedFile, QStatusBar { color: @muted; }
QWidget#panel { background: @card; border: 1px solid @line; border-radius: 10px; }
QLineEdit, QComboBox { background: @field; color: @text; border: 1px solid @line; border-radius: 6px;
    padding: 6px 10px; selection-background-color: @accent; selection-color: white; }
QLineEdit:focus, QComboBox:focus { border-color: @accent; }
QLineEdit:disabled, QComboBox:disabled { color: @muted; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox::down-arrow { width: 0; height: 0; border-left: 4px solid @field; border-right: 4px solid @field;
    border-top: 5px solid @muted; margin-right: 8px; }
QComboBox QAbstractItemView { background: @card; color: @text; border: 1px solid @line;
    selection-background-color: @sel; selection-color: @text; }
QPushButton { background: @card; color: @text; border: 1px solid @line; border-radius: 6px; padding: 6px 14px; }
QPushButton:hover { border-color: @accent; }
QPushButton:pressed { background: @sel; }
QPushButton:disabled { color: @muted; }
QPushButton#analyzeButton { background: @accent; border-color: @accent; color: white; font-weight: 600; }
QPushButton#analyzeButton:hover { background: @hover; border-color: @hover; }
QPushButton#analyzeButton:disabled { background: @line; border-color: @line; color: @muted; }
QListWidget, QTreeWidget { background: transparent; color: @text; border: none; outline: 0; }
QListWidget::item, QTreeWidget::item { padding: 4px 6px; }
QListWidget::item { border-radius: 4px; }
QListWidget::item:hover, QTreeWidget::item:hover { background: @bg; }
QListWidget::item:selected, QTreeWidget::item:selected { background: @sel; color: @text; }
QHeaderView { background: transparent; }
QHeaderView::section { background: transparent; color: @muted; border: none;
    border-bottom: 1px solid @line; padding: 4px 6px; }
QGraphicsView { border: 1px solid @line; border-radius: 6px; }
QSplitter::handle { background: transparent; }
QProgressBar { background: @line; border: none; border-radius: 3px; max-height: 6px; }
QProgressBar::chunk { background: @accent; border-radius: 3px; }
)");
    const Theme& t = theme();
    const std::pair<const char*, const char*> colours[] = {{"@bg", t.bg}, {"@card", t.card}, {"@line", t.line},
        {"@text", t.text}, {"@muted", t.muted}, {"@accent", t.accent}, {"@hover", t.hover},
        {"@field", t.field}, {"@sel", t.sel}};
    for (const auto& [name, value] : colours) sheet.replace(QLatin1String(name), QLatin1String(value));
    return sheet;
}
