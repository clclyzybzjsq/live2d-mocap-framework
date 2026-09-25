#include <QApplication>
#include <QFile>

#include "main_window.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("livebridge");
    QApplication::setOrganizationName("YunoLab");

    QFile theme(":/theme.qss");
    if (theme.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(theme.readAll()));
    }

    lb::ui::MainWindow window;
    window.show();
    return QApplication::exec();
}