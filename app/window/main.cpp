#include <QApplication>
#include "main_window.hpp"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("HeaderScope"));

    app.setOrganizationName(QStringLiteral("HeaderScope"));
    MainWindow window;
    window.show();

    return app.exec();
}
