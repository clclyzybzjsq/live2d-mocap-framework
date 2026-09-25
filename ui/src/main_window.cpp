#include "main_window.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMetaObject>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>

#include <cstdlib>

#include "floating/float_window.h"
#include "pages/models_page.h"
#include "pages/mocap_page.h"
#include "pages/output_page.h"
#include "pages/settings_page.h"

namespace lb::ui {

namespace {

std::string env_or(const char* key, const std::string& fallback) {
    const char* v = std::getenv(key);
    return (v && *v) ? std::string(v) : fallback;
}

} // namespace

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
    auto* models_page = new ModelsPage(pages_);
    mocap_page_ = new MocapPage(pages_);
    output_page_ = new OutputPage(pages_);
    pages_->addWidget(models_page);
    pages_->addWidget(mocap_page_);
    pages_->addWidget(output_page_);
    pages_->addWidget(new SettingsPage(pages_));

    body_layout->addWidget(nav_);
    body_layout->addWidget(pages_, 1);
    root->addWidget(body, 1);
    root->addWidget(build_status_bar());

    setCentralWidget(central);

    connect(nav_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
    nav_->setCurrentRow(0);

    connect(models_page, &ModelsPage::model_changed, this,
            [this](const QString& name) {
                model_name_->setText(name);
                status_model_->setText(QStringLiteral("模型: %1").arg(name));
            });

    // 浮窗
    float_window_ = new FloatWindowWidget();
    connect(output_page_, &OutputPage::floating_window_toggled, this,
            [this](bool visible) {
                if (visible) {
                    float_window_->show();
                    float_window_->raise();
                } else {
                    float_window_->hide();
                }
            });
    connect(output_page_, &OutputPage::floating_window_click_through, this,
            [this](bool enabled) { float_window_->set_click_through(enabled); });
    connect(output_page_, &OutputPage::floating_window_scale, this,
            [this](double scale) { float_window_->set_scale(scale); });
    connect(output_page_, &OutputPage::floating_window_opacity, this,
            [this](double opacity) { float_window_->set_opacity(opacity); });

    // 虚拟摄像头：开关 -> 注册/启动（实例由 MainWindow 持有，管线线程喂帧）
    connect(output_page_, &OutputPage::virtual_camera_toggled, this,
            &MainWindow::set_vcam_enabled);

    build_tray();

    ui_timer_ = new QTimer(this);
    connect(ui_timer_, &QTimer::timeout, this, &MainWindow::refresh_status);
    ui_timer_->start(250);
}

MainWindow::~MainWindow() {
    if (pipeline_) pipeline_->stop();
    if (float_window_) {
        float_window_->hide();
        delete float_window_;
        float_window_ = nullptr;
    }
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
    connect(show_float, &QAction::triggered, this, [this]() {
        if (float_window_->isVisible()) float_window_->hide();
        else float_window_->show();
    });

    auto* toggle_mocap = tray_menu_->addAction(QStringLiteral("开关动捕"));
    toggle_mocap->setCheckable(true);
    connect(toggle_mocap, &QAction::toggled, this, &MainWindow::set_running);
    connect(this, &MainWindow::running_changed, this,
            [toggle_mocap](bool running) { toggle_mocap->setChecked(running); });

    auto* toggle_vcam = tray_menu_->addAction(QStringLiteral("开关虚拟摄像头"));
    toggle_vcam->setCheckable(true);
    connect(toggle_vcam, &QAction::toggled, this, &MainWindow::set_vcam_enabled);
    connect(this, &MainWindow::running_changed, this,
            [toggle_vcam](bool) { /* 状态由 set_vcam_enabled 统一同步 */ });
    toggle_vcam_ = toggle_vcam;

    tray_menu_->addSeparator();
    auto* quit = tray_menu_->addAction(QStringLiteral("退出"));
    connect(quit, &QAction::triggered, qApp, &QCoreApplication::quit);

    tray_->setContextMenu(tray_menu_);
    tray_->show();
}

void MainWindow::set_running(bool running) {
    if (running_ == running) return;
    running_ = running;
    emit running_changed(running);

    start_button_->setChecked(running);
    status_text_->setText(running ? QStringLiteral("动捕运行中")
                                  : QStringLiteral("未运行"));
    status_dot_->setStyleSheet(running
        ? QStringLiteral("background-color: #3FBF7F; border-radius: 2px;")
        : QStringLiteral("background-color: #6B7280; border-radius: 2px;"));

    if (!running) {
        if (pipeline_) pipeline_->stop();
        status_camera_->setText(QStringLiteral("摄像头: 未连接"));
        return;
    }

    // 启动：采集 -> 推理边车 -> 求解 -> 平滑 -> 映射 -> 浮窗
    if (!pipeline_) pipeline_ = std::make_unique<lb::core::MotionPipeline>();

    lb::core::PipelineConfig cfg;
    cfg.device_id = mocap_page_->device_id().toStdString();
    cfg.capture = mocap_page_->capture_config();
    cfg.inference.model_dir = env_or("LB_MODEL_DIR", "D:/ournotes_vtube/inference/models");

    if (!pipeline_->initialize(cfg)) {
        set_running(false); // 回到未运行态；状态描述留给状态栏
        status_text_->setText(QStringLiteral("推理初始化失败"));
        status_dot_->setStyleSheet(
            QStringLiteral("background-color: #E5484D; border-radius: 2px;"));
        return;
    }

    const bool ok = pipeline_->start(
        [this](const lb::core::FrameView& frame,
               const lb::core::ModelParamList& params,
               const lb::core::SemanticParams& semantic,
               const lb::core::PipelineStats& stats) {
            on_pipeline_frame(frame, params, semantic, stats);
        });
    if (!ok) {
        set_running(false);
        status_text_->setText(QStringLiteral("动捕启动失败"));
        status_dot_->setStyleSheet(
            QStringLiteral("background-color: #E5484D; border-radius: 2px;"));
        return;
    }

    status_camera_->setText(QStringLiteral("摄像头: 已连接"));
}

void MainWindow::set_vcam_enabled(bool on) {
    if (vcam_enabled_ == on && on == (vcam_ && vcam_->is_running())) {
        return;
    }
    vcam_enabled_ = on;

    if (!on) {
        if (vcam_) vcam_->stop();
        output_page_->set_vcam_checked(false);
        if (toggle_vcam_) toggle_vcam_->setChecked(false);
        output_page_->set_vcam_status(QStringLiteral("已停止"));
        return;
    }

    if (!vcam_) vcam_ = lb::core::create_virtual_camera();
    if (!vcam_->is_registered() && !vcam_->ensure_registered()) {
        vcam_enabled_ = false;
        output_page_->set_vcam_checked(false);
        if (toggle_vcam_) toggle_vcam_->setChecked(false);
        output_page_->set_vcam_status(
            QStringLiteral("需一次管理员权限注册（运行 scripts/register_vcam.ps1）"));
        return;
    }

    const bool ok = vcam_->start(output_page_->vcam_config());
    output_page_->set_vcam_status(ok ? QStringLiteral("运行中")
                                     : QStringLiteral("启动失败"));
    if (!ok) {
        vcam_enabled_ = false;
        output_page_->set_vcam_checked(false);
        if (toggle_vcam_) toggle_vcam_->setChecked(false);
    }
}

void MainWindow::refresh_status() {
    if (!running_ || !pipeline_) return;
    const lb::core::PipelineStats stats = pipeline_->stats();

    status_infer_->setText(QStringLiteral("推理: %1 FPS")
                               .arg(stats.infer_fps, 0, 'f', 0));
    status_render_->setText(QStringLiteral("渲染: %1 FPS")
                                .arg(stats.render_fps, 0, 'f', 0));
    status_latency_->setText(QStringLiteral("延迟: %1 ms")
                                 .arg(stats.latency_ms, 0, 'f', 0));
    mocap_page_->set_confidence(stats.face_detected ? 100 : 0);
}

void MainWindow::on_pipeline_frame(const lb::core::FrameView& frame,
                                   const lb::core::ModelParamList& params,
                                   const lb::core::SemanticParams& semantic,
                                   const lb::core::PipelineStats& stats) {
    // 虚拟摄像头喂帧：管线回调线程直推（vcam 内部有锁）
    // 注：回调携带的 frame 为管线内部分配的缓冲区，拷贝到共享内存后即可释放
    if (vcam_ && vcam_->is_running()) {
        vcam_->push_frame(frame);
    }

    FrameSnapshot snap;
    snap.params = params;
    snap.semantic = semantic;
    snap.stats = stats;
    if (float_window_ && float_window_->isVisible()) {
        QMetaObject::invokeMethod(float_window_, "set_frame", Qt::QueuedConnection,
                                  Q_ARG(FrameSnapshot, snap));
    }
}

} // namespace lb::ui