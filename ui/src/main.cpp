#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFile>

#include "floating/float_window.h"
#include "main_window.h"
#include "render/cubism_runtime.h" // cubism_log（文件日志）

using lb::ui::cubism_log;

int main(int argc, char** argv) {
    // 注意：这里刻意不再调用 QSurfaceFormat::setDefaultFormat()。
    // 早期曾为绕开"Core Profile 导致渲染空白"的假设而强制 2.1/NoProfile，
    // 但该假设是错的，而且自定义默认格式会干扰 QOpenGLWidget 的上下文创建
    // （最终采用的方案是用 QOpenGLWidget 的上下文渲染 Live2D）。
    // 日志写到 exe 同目录的 cubism_debug.log，GUI 程序不依赖 stderr。

    QApplication app(argc, argv);
    QApplication::setApplicationName("livebridge");
    QApplication::setOrganizationName("YunoLab");

    cubism_log("[main] 应用启动\n");

    qRegisterMetaType<lb::ui::FrameSnapshot>("FrameSnapshot");

    // 启动参数：便于自动化验证，也方便做「开机即工作」的快捷方式
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Live Bridge 虚拟主播动捕"));
    parser.addHelpOption();
    const QCommandLineOption optVcam(QStringLiteral("vcam"),
                                     QStringLiteral("启动后自动开启虚拟摄像头"));
    const QCommandLineOption optFloat(QStringLiteral("float"),
                                      QStringLiteral("启动后显示桌面浮窗"));
    const QCommandLineOption optModel(QStringLiteral("model"),
                                      QStringLiteral("指定模型(.model3.json)路径"),
                                      QStringLiteral("path"));
    parser.addOption(optVcam);
    parser.addOption(optFloat);
    parser.addOption(optModel);
    parser.process(app);

    QFile theme(":/theme.qss");
    if (theme.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(theme.readAll()));
    }

    cubism_log("[main] 构造主窗口\n");
    lb::ui::MainWindow window;
    window.show();
    cubism_log("[main] 主窗口已显示，应用启动参数\n");
    window.apply_startup_options(parser.isSet(optVcam), parser.isSet(optFloat),
                                 parser.value(optModel));
    cubism_log("[main] 进入事件循环\n");
    return QApplication::exec();
}
