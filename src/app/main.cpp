#include <QApplication>
#include <QFontDatabase>
#include "app/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Akera Glitch Studio");
    app.setOrganizationName("Akera Sky");

    MainWindow window;
    window.show();
    return app.exec();
}
