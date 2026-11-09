#include <QApplication>
#include <QMainWindow>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("HeaderScope"));

    QMainWindow window;
    window.setWindowTitle(app.applicationName());
    window.resize(1024, 640);
    window.show();

    return app.exec();
}
