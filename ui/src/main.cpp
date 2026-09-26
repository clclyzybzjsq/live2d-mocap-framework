#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFile>
#include <QSurfaceFormat>

#include "floating/float_window.h"
#include "main_window.h"

int main(int argc, char** argv) {
    // 必须在 QApplication 之前设置：Cubism 的 OpenGL ES2 后端使用固定管线风格的
    // 调用，在 Core Profile 下会 GL_INVALID_OPERATION 且画面空白；
    // 而 Qt Widgets 初始化会把默认格式带回 Core Profile。
    {
        QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
        // Intel 驱动对 Compatibility Profile 最高支持 3.2；若不指定版本，
        // Qt 默认带的 4.6 会让驱动回退成 Core Profile（Cubism 需要兼容模式）
        // 2.1 时代没有 Core/Compatibility 之分，驱动会给出兼容上下文；
        // 现代驱动对 Compatibility Profile 请求往往直接回退成 Core
        fmt.setVersion(2, 1);
        fmt.setProfile(QSurfaceFormat::NoProfile);
        fmt.setDepthBufferSize(24);
        fmt.setStencilBufferSize(8);
        QSurfaceFormat::setDefaultFormat(fmt);
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName("livebridge");
    QApplication::setOrganizationName("YunoLab");

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

    lb::ui::MainWindow window;
    window.show();
    window.apply_startup_options(parser.isSet(optVcam), parser.isSet(optFloat),
                                 parser.value(optModel));
    return QApplication::exec();
}
