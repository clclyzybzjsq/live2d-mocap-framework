#include "main_window.h"

#include <QApplication>
#include <cstring>
#include <QFileInfo>
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
#include "render/avatar_renderer.h"
#include "render/live2d_model_library.h"
#include "render/vcam_compositor.h"
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
    models_page_ = new ModelsPage(pages_);
    mocap_page_ = new MocapPage(pages_);
    output_page_ = new OutputPage(pages_);
    pages_->addWidget(models_page_);
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

    connect(models_page_, &ModelsPage::model_changed, this,
            [this](const QString& name) {
                model_name_->setText(name);
                status_model_->setText(QStringLiteral("模型: %1").arg(name));
            });

    // 模型选择 -> 重建渲染器（浮窗与虚拟摄像头合成共用同一实例）
    connect(models_page_, &ModelsPage::model_activated, this,
            &MainWindow::on_model_activated);

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

    // 虚拟摄像头画面内容：默认 Live2D + 虚化背景；打开"仅输出背景"则隐藏 Live2D
    vcam_compositor_ = std::make_unique<VcamCompositor>();
    connect(output_page_, &OutputPage::vcam_background_only_toggled, this, [this](bool on) {
        if (!vcam_compositor_) return;
        vcam_compositor_->set_content(
            on ? VcamContent::BackgroundOnly : VcamContent::Live2dOverBlurredBackground);
    });

    build_tray();

    // 形象渲染器：初始为占位实现（无模型也能出画面）。
    // 模型页若已恢复上次选择，on_model_activated 会替换为对应模型的渲染器。
    avatar_renderer_ = create_avatar_renderer(current_model_);
    if (vcam_compositor_) vcam_compositor_->set_avatar_renderer(avatar_renderer_.get());
    if (float_window_) float_window_->set_avatar_renderer(avatar_renderer_.get());
    if (models_page_ && !models_page_->current_model_json().isEmpty()) {
        on_model_activated(models_page_->current_model_json());
    }

    ui_timer_ = new QTimer(this);
    connect(ui_timer_, &QTimer::timeout, this, &MainWindow::refresh_status);
    ui_timer_->start(250);

    // 形象渲染与虚拟摄像头输出节拍。
    // Cubism 渲染必须在持有 GL 上下文的 UI 线程进行；虚拟摄像头推帧也统一由
    // 这里驱动，这样动捕未启动时（无相机帧）虚拟摄像头依然输出待机形象。
    avatar_timer_ = new QTimer(this);
    connect(avatar_timer_, &QTimer::timeout, this, [this]() {
        if (!avatar_renderer_) return;
        lb::core::SemanticParams sem;
        {
            std::lock_guard<std::mutex> lock(semantic_mutex_);
            sem = last_semantic_;
        }
        avatar_renderer_->tick(sem);

        if (!vcam_ || !vcam_->is_running() || !vcam_compositor_) return;
        lb::core::FrameView cam;
        {
            std::lock_guard<std::mutex> lock(camera_mutex_);
            if (camera_valid_ && !camera_buf_.empty()) {
                cam.plane0 = camera_buf_.data();
                cam.stride0 = static_cast<uint32_t>(camera_w_) * 4u;
                cam.width = camera_w_;
                cam.height = camera_h_;
                cam.format = lb::core::PixelFormat::BGRA32;
            }
        }
        const lb::core::FrameView* out = vcam_compositor_->compose(cam, sem);
        if (out) vcam_->push_frame(*out);
    });
    avatar_timer_->start(33); // 约 30fps
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

    // 设备注册（首次需要一次管理员授权）。注册失败不阻止写端启动：
    // 共享内存照常喂帧，只是其他软件暂时枚举不到该设备。
    bool registered = vcam_->is_registered();
    if (!registered) registered = vcam_->ensure_registered();

    const bool ok = vcam_->start(output_page_->vcam_config());
    if (!ok) {
        vcam_enabled_ = false;
        output_page_->set_vcam_checked(false);
        if (toggle_vcam_) toggle_vcam_->setChecked(false);
        output_page_->set_vcam_status(QStringLiteral("启动失败"));
        return;
    }

    output_page_->set_vcam_status(
        registered
            ? QStringLiteral("运行中")
            : QStringLiteral("运行中（未注册：其他软件暂不可见，需一次管理员授权，见 scripts/register_vcam.ps1）"));
}

void MainWindow::on_model_activated(const QString& model_json) {
    const Live2dModelInfo info = Live2dModelLibrary::parse(model_json);
    if (!info.usable()) {
        status_model_->setText(QStringLiteral("模型不可用: %1")
                                   .arg(QFileInfo(model_json).fileName()));
        return;
    }
    current_model_ = info;

    std::unique_ptr<AvatarRenderer> fresh = create_avatar_renderer(current_model_);
    if (!fresh) return;

    // 旧渲染器延迟到下一次替换时回收：管线回调线程可能正在使用它
    retired_renderer_ = std::move(avatar_renderer_);
    avatar_renderer_ = std::move(fresh);

    if (vcam_compositor_) vcam_compositor_->set_avatar_renderer(avatar_renderer_.get());
    if (float_window_) float_window_->set_avatar_renderer(avatar_renderer_.get());

    status_model_->setText(
        QStringLiteral("模型: %1（%2）").arg(info.name, avatar_renderer_->name()));
}

void MainWindow::apply_startup_options(bool auto_vcam, bool show_float,
                                       const QString& model_path) {
    if (!model_path.isEmpty()) on_model_activated(model_path);

    if (show_float && float_window_) {
        float_window_->show();
        float_window_->raise();
        if (output_page_) output_page_->set_float_checked(true);
    }

    if (auto_vcam) set_vcam_enabled(true);
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
    // 记录最近语义参数供 UI 线程的渲染节拍使用（跨线程，加锁拷贝）
    {
        std::lock_guard<std::mutex> lock(semantic_mutex_);
        last_semantic_ = semantic;
    }
    // 缓存相机帧（BGRA 紧凑拷贝）。虚拟摄像头不在这里推帧：
    // 推帧统一由 UI 定时器驱动，动捕停止后仍能继续输出待机形象。
    if (frame.format == lb::core::PixelFormat::BGRA32 && frame.width > 0 && frame.height > 0 &&
        frame.plane0 != nullptr) {
        const size_t pitch = static_cast<size_t>(frame.width) * 4u;
        const auto* src = static_cast<const uint8_t*>(frame.plane0);
        std::lock_guard<std::mutex> lock(camera_mutex_);
        camera_buf_.resize(pitch * static_cast<size_t>(frame.height));
        for (int y = 0; y < frame.height; ++y) {
            std::memcpy(camera_buf_.data() + pitch * static_cast<size_t>(y),
                        src + static_cast<size_t>(frame.stride0) * static_cast<size_t>(y), pitch);
        }
        camera_w_ = frame.width;
        camera_h_ = frame.height;
        camera_valid_ = true;
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