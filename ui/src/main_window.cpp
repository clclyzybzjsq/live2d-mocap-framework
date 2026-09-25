#include "main_window.h"

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>

#include "pages/models_page.h"
#include "pages/mocap_page.h"
#include "pages/output_page.h"
#include "pages/settings_page.h"

namespace lb::ui {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Live Bridge");
    resize(1080, 680);
    setMinimumSize(960, 600);

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(build_top_bar());

    auto* body = new QWidget(central);
    auto* body_layout = new QHBoxLayout(body);
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(0);

    nav_ = new QListWidget(body);
    nav_->setObjectName("NavList");
    nav_->setFixedWidth(160);
    nav_->addItems({ QStringLiteral("模型"), QStringLiteral("动捕"),
                     QStringLiteral("输出"), QStringLiteral("设置") });

    pages_ = new QStackedWidget(body);
    pages_->addWidget(new ModelsPage(pages_));
    pages_->addWidget(new MocapPage(pages_));
    pages_->addWidget(new OutputPage(pages_));
    pages_->addWidget(new SettingsPage(pages_));

    body_layout->addWidget(nav_);
    body_layout->addWidget(pages_, 1);
    root->addWidget(body, 1);
    root->addWidget(build_status_bar());

    setCentralWidget(central);

    connect(nav_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
    nav_->setCurrentRow(0);

    auto* models = qobject_cast<ModelsPage*>(pages_->widget(0));
    connect(models, &ModelsPage::model_changed, this,
            [this](const QString& name) {
                model_name_->setText(name);
                status_model_->setText(QStringLiteral("模型: %1").arg(name));
            });

    build_tray();

    ui_timer_ = new QTimer(this);
    connect(ui_timer_, &QTimer::timeout, this, &MainWindow::refresh_status);
    ui_timer_->start(250);
}

QWidget* MainWindow::build_top_bar() {
    auto* bar = new QWidget(this);
    bar->setObjectName("TopBar");
    bar->setFixedHeight(48);

    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(8);

    status_dot_ = new QLabel(bar);
    status_dot_->setObjectName("StatusDot");
    status_dot_->setFixedSize(10, 10);

    status_text_ = new QLabel(QStringLiteral("未运行"), bar);
    status_text_->setObjectName("StatusText");

    model_name_ = new QLabel(QStringLiteral("未加载模型"), bar);
    model_name_->setObjectName("ModelName");

    start_button_ = new QPushButton(QStringLiteral("开始动捕"), bar);
    start_button_->setObjectName("StartButton");
    start_button_->setCheckable(true);

    layout->addWidget(status_dot_);
    layout->addWidget(status_text_);
    layout->addSpacing(16);
    layout->addWidget(model_name_);
    layout->addStretch(1);
    layout->addWidget(start_button_);

    connect(start_button_, &QPushButton::toggled, this, &MainWindow::set_running);
    return bar;
}

QWidget* MainWindow::build_status_bar() {
    auto* bar = new QWidget(this);
    bar->setObjectName("StatusBar");
    bar->setFixedHeight(28);

    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(20);

    auto make_label = [&](const QString& text) {
        auto* label = new QLabel(text, bar);
        label->setObjectName("StatusBarText");
        return label;
    };

    status_camera_ = make_label(QStringLiteral("摄像头: 未连接"));
    status_infer_ = make_label(QStringLiteral("推理: -- FPS"));
    status_render_ = make_label(QStringLiteral("渲染: -- FPS"));
    status_latency_ = make_label(QStringLiteral("延迟: -- ms"));
    status_model_ = make_label(QStringLiteral("模型: --"));

    layout->addWidget(status_camera_);
    layout->addWidget(status_infer_);
    layout->addWidget(status_render_);
    layout->addWidget(status_latency_);
    layout->addStretch(1);
    layout->addWidget(status_model_);
    return bar;
}

void MainWindow::build_tray() {
    QPixmap icon_pixmap(16, 16);
    icon_pixmap.fill(Qt::transparent);
    QPainter painter(&icon_pixmap);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#4DB8FF"));
    painter.drawRect(0, 0, 16, 16);

    tray_ = new QSystemTrayIcon(QIcon(icon_pixmap), this);
    tray_menu_ = new QMenu(this);

    auto* show_float = tray_menu_->addAction(QStringLiteral("显示/隐藏浮窗"));
    show_float->setCheckable(true);
    // TODO(M1): 接入浮窗可见性切换。

    auto* toggle_mocap = tray_menu_->addAction(QStringLiteral("开关动捕"));
    toggle_mocap->setCheckable(true);
    connect(toggle_mocap, &QAction::toggled, this, &MainWindow::set_running);

    auto* toggle_vcam = tray_menu_->addAction(QStringLiteral("开关虚拟摄像头"));
    toggle_vcam->setCheckable(true);
    // TODO(M2): 接入虚拟摄像头开关。

    tray_menu_->addSeparator();
    auto* quit = tray_menu_->addAction(QStringLiteral("退出"));
    connect(quit, &QAction::triggered, qApp, &QCoreApplication::quit);

    tray_->setContextMenu(tray_menu_);
    tray_->show();
}

void MainWindow::set_running(bool running) {
    running_ = running;
    start_button_->setChecked(running);
    status_text_->setText(running ? QStringLiteral("动捕运行中")
                                  : QStringLiteral("未运行"));
    status_dot_->setStyleSheet(running
        ? QStringLiteral("background-color: #3FBF7F; border-radius: 2px;")
        : QStringLiteral("background-color: #6B7280; border-radius: 2px;"));
    // TODO(M1): 真正启停采集与推理管线。
}

void MainWindow::refresh_status() {
    // TODO(M1): 从管线读取实时指标。当前为占位值。
    if (!running_) {
        status_camera_->setText(QStringLiteral("摄像头: 未连接"));
        status_infer_->setText(QStringLiteral("推理: -- FPS"));
        status_render_->setText(QStringLiteral("渲染: -- FPS"));
        status_latency_->setText(QStringLiteral("延迟: -- ms"));
    }
}

} // namespace lb::ui