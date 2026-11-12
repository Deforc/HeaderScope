#include "language.hpp"
#include <QCoreApplication>
#include <QHash>
#include <QTranslator>
#include <QTreeWidgetItem>

namespace {
const QHash<QString, QString>& translations() {
    static const QHash<QString, QString> values = {
        {QStringLiteral("Папка · %1 файлов"), QStringLiteral("Folder · %1 files")},
        {QStringLiteral("Группировать по папкам"), QStringLiteral("Group by folder")},
        {QStringLiteral("Свернуть всё"), QStringLiteral("Collapse all")},
        {QStringLiteral("Двойной щелчок раскрывает папку. Правая кнопка — свернуть родительскую папку."), QStringLiteral("Double-click to expand a folder. Right-click to collapse its parent folder.")},
        {QStringLiteral("Файлы в корне"), QStringLiteral("Root files")},
        {QStringLiteral("%1\nФайлов: %2 · связей внутри: %3\nДвойной щелчок — раскрыть папку"), QStringLiteral("%1\nFiles: %2 · internal links: %3\nDouble-click to expand the folder")},
        {QStringLiteral("%1 файлов · %2 связей внутри"), QStringLiteral("%1 files · %2 internal links")},
        {QStringLiteral("Связей #include: %1"), QStringLiteral("#include links: %1")},
        {QStringLiteral("Раскрыть папку"), QStringLiteral("Expand folder")},
        {QStringLiteral("Свернуть родительскую папку"), QStringLiteral("Collapse parent folder")},
        {QStringLiteral("Свернуть все папки"), QStringLiteral("Collapse all folders")},
        {QStringLiteral("Язык"), QStringLiteral("Language")},
        {QStringLiteral("HeaderScope · зависимости C/C++"), QStringLiteral("HeaderScope · C/C++ dependencies")},
        {QStringLiteral("Папка проекта или compile_commands.json"), QStringLiteral("Project folder or compile_commands.json")},
        {QStringLiteral("Путь к проекту или базе команд"), QStringLiteral("Project or compilation database path")},
        {QStringLiteral("Папка…"), QStringLiteral("Folder…")},
        {QStringLiteral("Анализировать"), QStringLiteral("Analyze")},
        {QStringLiteral("Анализ…"), QStringLiteral("Analyzing…")},
        {QStringLiteral("Экспорт графа…"), QStringLiteral("Export graph…")},
        {QStringLiteral("Экспорт графа"), QStringLiteral("Export graph")},
        {QStringLiteral("Граф экспортирован: %1"), QStringLiteral("Graph exported: %1")},
        {QStringLiteral("Ошибка экспорта графа"), QStringLiteral("Graph export failed")},
        {QStringLiteral("Ошибка экспорта"), QStringLiteral("Export error")},
        {QStringLiteral("Уместить граф"), QStringLiteral("Fit graph")},
        {QStringLiteral("Выберите папку проекта и нажмите «Анализировать»."), QStringLiteral("Choose a project folder and click Analyze.")},
        {QStringLiteral("Файлы проекта"), QStringLiteral("Project files")},
        {QStringLiteral("Найти файл…"), QStringLiteral("Find a file…")},
        {QStringLiteral("ФАЙЛЫ"), QStringLiteral("FILES")},
        {QStringLiteral("Граф"), QStringLiteral("Graph")},
        {QStringLiteral("Циклы"), QStringLiteral("Cycles")},
        {QStringLiteral("Циклы (%1)"), QStringLiteral("Cycles (%1)")},
        {QStringLiteral("Проверка #include"), QStringLiteral("#include check")},
        {QStringLiteral("ПРОВЕРКА НОВОГО #INCLUDE"), QStringLiteral("CHECK NEW #INCLUDE")},
        {QStringLiteral("Файл, в который добавляется #include"), QStringLiteral("File receiving the new #include")},
        {QStringLiteral("Добавить #include заголовка"), QStringLiteral("Header to include")},
        {QStringLiteral("Выберите заголовок"), QStringLiteral("Select a header")},
        {QStringLiteral("Подключаемый заголовок"), QStringLiteral("Header to include")},
        {QStringLiteral("Выберите файл и заголовок, чтобы узнать, замкнётся ли цикл."), QStringLiteral("Select a file and header to check for a cycle.")},
        {QStringLiteral("Подключение уже существует."), QStringLiteral("This include already exists.")},
        {QStringLiteral("В доступных данных цикл не найден. Анализ завершился частично."), QStringLiteral("No cycle was found in the available data. The analysis is incomplete.")},
        {QStringLiteral("Цикл замкнётся: %1\nПричина: %2 уже подключает %3%4."), QStringLiteral("This include would create a cycle: %1\n%2 already includes %3%4.")},
        {QStringLiteral(" косвенно"), QStringLiteral(" indirectly")},
        {QStringLiteral("Цикл замкнётся: файл подключит сам себя."), QStringLiteral("This include would create a cycle because the file would include itself.")},
        {QStringLiteral("Цикл не возникнет: %1 не подключает %2 ни прямо, ни косвенно."), QStringLiteral("This include would not create a cycle. %1 has no direct or indirect include of %2.")},
        {QStringLiteral("Новое подключение: %1 → %2"), QStringLiteral("Proposed include: %1 → %2")},
        {QStringLiteral("Тёмная тема"), QStringLiteral("Dark theme")},
        {QStringLiteral("A → B означает: A подключает B"), QStringLiteral("A → B means: A includes B")},
        {QStringLiteral("Подключения и зависимые файлы"), QStringLiteral("Includes and dependent files")},
        {QStringLiteral("Выберите файл"), QStringLiteral("Select a file")},
        {QStringLiteral("Циклические группы"), QStringLiteral("Cycle groups")},
        {QStringLiteral("Показать все циклы"), QStringLiteral("Show all cycles")},
        {QStringLiteral("ЗАВИСИМОСТИ"), QStringLiteral("DEPENDENCIES")},
        {QStringLiteral("ЦИКЛИЧЕСКИЕ ЗАВИСИМОСТИ"), QStringLiteral("CYCLIC DEPENDENCIES")},
        {QStringLiteral("Уровень"), QStringLiteral("Severity")},
        {QStringLiteral("Сообщение"), QStringLiteral("Message")},
        {QStringLiteral("Файл"), QStringLiteral("File")},
        {QStringLiteral("Строка"), QStringLiteral("Line")},
        {QStringLiteral("ДИАГНОСТИКА АНАЛИЗА"), QStringLiteral("ANALYSIS DIAGNOSTICS")},
        {QStringLiteral("Готово"), QStringLiteral("Ready")},
        {QStringLiteral("Выберите папку проекта"), QStringLiteral("Choose a project folder")},
        {QStringLiteral("База команд компиляции"), QStringLiteral("Compilation database")},
        {QStringLiteral("Анализ проекта…"), QStringLiteral("Analyzing project…")},
        {QStringLiteral("Анализ выполняется в фоне"), QStringLiteral("Analysis is running in the background")},
        {QStringLiteral("Укажите папку проекта или compile_commands.json."), QStringLiteral("Specify a project folder or compile_commands.json.")},
        {QStringLiteral("Указанный путь не существует: %1"), QStringLiteral("The specified path does not exist: %1")},
        {QStringLiteral("Не найден compile_commands.json. Выберите его в каталоге сборки кнопкой выше."), QStringLiteral("compile_commands.json was not found. Select it in the build directory using the button above.")},
        {QStringLiteral("Сборщик Clang не включён. Соберите с HEADERSCOPE_BUILD_SCANNER=ON."), QStringLiteral("The Clang scanner is disabled. Build with HEADERSCOPE_BUILD_SCANNER=ON.")},
        {QStringLiteral("Ошибка анализа: %1"), QStringLiteral("Analysis error: %1")},
        {QStringLiteral("Неизвестная ошибка анализа."), QStringLiteral("Unknown analysis error.")},
        {QStringLiteral("Ошибка"), QStringLiteral("Error")},
        {QStringLiteral("Предупреждение"), QStringLiteral("Warning")},
        {QStringLiteral("Анализ не выполнен. Подробности в диагностике."), QStringLiteral("Analysis failed. See diagnostics for details.")},
        {QStringLiteral("Ошибка анализа"), QStringLiteral("Analysis failed")},
        {QStringLiteral("Частичный анализ"), QStringLiteral("Partial analysis")},
        {QStringLiteral("Анализ завершён"), QStringLiteral("Analysis complete")},
        {QStringLiteral("%1 · %2 файлов · %3 директив #include · %4 циклических групп · %5 сообщений"), QStringLiteral("%1 · %2 files · %3 #include directives · %4 cycle groups · %5 messages")},
        {QStringLiteral("Зависимые файлы · %1"), QStringLiteral("Dependent files · %1")},
        {QStringLiteral("Включает напрямую · %1"), QStringLiteral("Direct includes · %1")},
        {QStringLiteral("прямо"), QStringLiteral("direct")},
        {QStringLiteral("косвенно"), QStringLiteral("indirect")},
        {QStringLiteral("стр. %1"), QStringLiteral("line %1")},
        {QStringLiteral("Нет зависимых файлов"), QStringLiteral("No dependent files")},
        {QStringLiteral("Нет подключений"), QStringLiteral("No includes")},
        {QStringLiteral("Группа %1 · файлов: %2 · циклов: %3"), QStringLiteral("Group %1 · files: %2 · cycles: %3")},
        {QStringLiteral("Циклов не найдено"), QStringLiteral("No cycles found")},
        {QStringLiteral("Граф подключений"), QStringLiteral("Include graph")},
        {QStringLiteral("Запустите анализ, чтобы увидеть связи"), QStringLiteral("Run analysis to see dependencies")},
        {QStringLiteral("Нет файлов для отображения"), QStringLiteral("No files to display")}
    };
    return values;
}
class EnglishTranslator final : public QTranslator {
public:
    using QTranslator::QTranslator;
    bool isEmpty() const override { return false; }
    QString translate(const char* context, const char* source, const char*, int) const override {
        if (qstrcmp(context, "MainWindow") && qstrcmp(context, "GraphPlaceholder")) return {};
        return translations().value(QString::fromUtf8(source));
    }
};
constexpr int sourceRole = Qt::UserRole + 40;
constexpr int argumentsRole = Qt::UserRole + 41;
QString format(QString value, const QStringList& arguments) {
    for (const auto& argument : arguments) value = value.arg(argument);
    return value;
}
}

void language::setEnglish(bool enabled) {
    static auto* translator = new EnglishTranslator(QCoreApplication::instance());
    static bool installed = false;
    if (enabled == installed) return;
    installed = enabled;
    if (enabled) QCoreApplication::installTranslator(translator);
    else QCoreApplication::removeTranslator(translator);
}
QString language::text(const char* source) {
    return QCoreApplication::translate("MainWindow", source);
}
void language::setItemText(QTreeWidgetItem* item, int column, const char* source, const QStringList& arguments) {
    item->setData(column, sourceRole, QString::fromUtf8(source));
    item->setData(column, argumentsRole, arguments);
    item->setText(column, format(text(source), arguments));
}
void language::retranslateItem(QTreeWidgetItem* item) {
    for (int column = 0; column < item->columnCount(); ++column) {
        const auto source = item->data(column, sourceRole);
        if (source.isValid()) item->setText(column, format(text(source.toString().toUtf8().constData()),
            item->data(column, argumentsRole).toStringList()));
    }
}
QString language::diagnosticText(const QString& source) {
    // Only application-generated messages are translated. Clang's own output stays verbatim.
    if (translations().contains(source)) return text(source.toUtf8().constData());
    for (const auto* pattern : {"Указанный путь не существует: %1", "Ошибка анализа: %1"}) {
        const QString prefix = QString::fromUtf8(pattern).section(QStringLiteral("%1"), 0, 0);
        if (source.startsWith(prefix)) return text(pattern).arg(source.mid(prefix.size()));
    }
    return source;
}
