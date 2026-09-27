#include "main_window.h"

#include <QApplication>
#include <cstring>
#include <QDebug>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMetaObject>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
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
#include "render/cubism_avatar_renderer.h"
#include "render/cubism_render_surface.h"
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
    connect(output_page_, &OutputPage::floating_window_scale, this, [this](double scale) {
        if (float_window_) float_window_->set_scale(scale);
    });
    connect(output_page_, &OutputPage::floating_window_overall_scale, this, [this](double scale) {
        if (float_window_) float_window_->set_overall_scale(scale);
    });
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

    // 画面取景：恢复上次设置（默认上半身），并在变化时应用到渲染器
    {
        QSettings settings;
        const double z = settings.value(QStringLiteral("view/zoom"), output_page_->view_zoom()).toDouble();
        const double ox = settings.value(QStringLiteral("view/offset_x"), output_page_->view_offset_x()).toDouble();
        const double oy = settings.value(QStringLiteral("view/offset_y"), output_page_->view_offset_y()).toDouble();
        output_page_->set_view(z, ox, oy);
    }
    connect(output_page_, &OutputPage::view_changed, this,
            [this](double zoom, double offset_x, double offset_y) {
                if (render_surface_) {
                    render_surface_->set_view(zoom, offset_x, offset_y);
                    // 立刻重跑一帧并让浮窗重画：否则要等下一次定时器才看到变化，
                    // 且浮窗在管线未运行时根本不会重绘（表现为拖滑块没反应）
                    render_surface_->update();
                }
                if (float_window_) float_window_->update();
                QSettings settings;
                settings.setValue(QStringLiteral("view/zoom"), zoom);
                settings.setValue(QStringLiteral("view/offset_x"), offset_x);
                settings.setValue(QStringLiteral("view/offset_y"), offset_y);
            });

    // 动作幅度：恢复上次设置，并在变化时应用到渲染器
    {
        QSettings settings;
        mocap_page_->set_motion_gain(
            settings.value(QStringLiteral("mocap/gain"), mocap_page_->motion_gain()).toDouble());
    }
    connect(mocap_page_, &MocapPage::motion_gain_changed, this, [this](double gain) {
        if (render_surface_) render_surface_->set_motion_gain(static_cast<float>(gain));
        QSettings settings;
        settings.setValue(QStringLiteral("mocap/gain"), gain);
    });

    // 平滑强度：恢复上次设置；管线未运行时先存着，启动时随配置一起下发
    {
        QSettings settings;
        mocap_page_->set_smoothing(
            settings.value(QStringLiteral("mocap/smoothing"), mocap_page_->smoothing()).toDouble());
    }
    connect(mocap_page_, &MocapPage::smoothing_changed, this, [this](double value) {
        if (pipeline_) pipeline_->set_smoothing(value);
        QSettings settings;
        settings.setValue(QStringLiteral("mocap/smoothing"), value);
    });

    // 表情幅度：恢复上次设置，并在变化时应用到渲染器
    {
        QSettings settings;
        mocap_page_->set_face_gain(
            settings.value(QStringLiteral("mocap/face_gain"), mocap_page_->face_gain()).toDouble());
    }
    connect(mocap_page_, &MocapPage::face_gain_changed, this, [this](double gain) {
        if (render_surface_) render_surface_->set_face_gain(static_cast<float>(gain));
        QSettings settings;
        settings.setValue(QStringLiteral("mocap/face_gain"), gain);
    });

    // 镜像：采集端翻转（位于推理之前，转头方向会一起镜像）。
    // 管线未运行时先存着，启动时随 CaptureConfig 一起下发。
    {
        QSettings settings;
        const bool m = settings.value(QStringLiteral("mocap/mirror"), false).toBool();
        if (mocap_page_) mocap_page_->set_mirror(m);
    }
    connect(mocap_page_, &MocapPage::mirror_changed, this, [this](bool enabled) {
        if (pipeline_) pipeline_->set_mirror(enabled);
        QSettings settings;
        settings.setValue(QStringLiteral("mocap/mirror"), enabled);
    });

    // 「应用」：摄像头/分辨率/帧率只在管线启动时下发，所以运行中就用新配置
    // 重启一次；未运行则什么都不用做（开始动捕时自然带上新配置）
    connect(mocap_page_, &MocapPage::apply_requested, this, [this]() {
        if (!running_) return;
        set_running(false);
        set_running(true);
    });

    // 形象渲染：用一个移到屏幕外的 QOpenGLWidget 作为渲染宿主。
    // Qt Widgets 应用里自建的离屏 GL 上下文渲染不出内容（全透明），
    // 而 QOpenGLWidget 提供的上下文可以正常工作。
    // 注意：延迟到主窗口显示之后再创建 —— 渲染初始化涉及模型加载与 Cubism
    // 启动，耗时可能明显，放在构造函数里会让窗口迟迟不出现。
    QTimer::singleShot(0, this, [this]() {
        // 不设父窗口：作为子控件时它会干扰主窗口的局部绘制
        render_surface_ = new CubismRenderSurface(nullptr);
        connect(render_surface_, &CubismRenderSurface::initialized, this, [this](bool ok) {
            if (CubismAvatarRenderer* r = render_surface_->renderer()) {
                if (vcam_compositor_) vcam_compositor_->set_avatar_renderer(r);
                if (float_window_) float_window_->set_avatar_renderer(r);
                render_surface_->set_view(output_page_->view_zoom(), output_page_->view_offset_x(),
                                          output_page_->view_offset_y());
                render_surface_->set_motion_gain(static_cast<float>(mocap_page_->motion_gain()));
                render_surface_->set_face_gain(static_cast<float>(mocap_page_->face_gain()));
            }
            status_model_->setText(
                ok ? QStringLiteral("模型: %1（Cubism）").arg(current_model_.name)
                   : QStringLiteral("模型: %1（渲染器初始化失败）").arg(current_model_.name));
        });
        render_surface_->show();
        if (models_page_ && !models_page_->current_model_json().isEmpty()) {
            on_model_activated(models_page_->current_model_json());
        }
    });

    ui_timer_ = new QTimer(this);
    connect(ui_timer_, &QTimer::timeout, this, &MainWindow::refresh_status);
    ui_timer_->start(250);

    // 形象渲染与虚拟摄像头输出节拍。
    // Cubism 渲染必须在持有 GL 上下文的 UI 线程进行；虚拟摄像头推帧也统一由
    // 这里驱动，这样动捕未启动时（无相机帧）虚拟摄像头依然输出待机形象。
    avatar_timer_ = new QTimer(this);
    connect(avatar_timer_, &QTimer::timeout, this, [this]() {
        lb::core::SemanticParams sem;
        {
            std::lock_guard<std::mutex> lock(semantic_mutex_);
            sem = last_semantic_;
        }
        // 驱动屏幕外渲染宿主：set_semantics + update 会在 Qt 的 paintGL 中渲染一帧
        if (render_surface_) {
            render_surface_->set_semantics(sem);
            render_surface_->update();
        }
        // 浮窗是给用户看的显示端，必须持续重绘 —— 它此前只在管线回调里 update，
        // 摄像头没开时画面就冻住，取景/整体缩放这些改动自然也看不到
        if (float_window_) float_window_->update();

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
    // 渲染/合成节拍。动捕只有约 15fps，但渲染本身很便宜（单帧 2~3ms），
    // 这里按 60fps 跑，配合渲染帧率上的插值让动作连续。
    avatar_timer_->start(16);
}

MainWindow::~MainWindow() {
    if (pipeline_) pipeline_->stop();

    // 渲染宿主无父窗口（独立顶层 widget，避免干扰主窗口绘制），需手动释放。
    // 先解除消费者的引用，避免合成器/浮窗在渲染器销毁后仍持有它。
    if (vcam_compositor_) vcam_compositor_->set_avatar_renderer(nullptr);
    if (float_window_) float_window_->set_avatar_renderer(nullptr);
    if (render_surface_) {
        render_surface_->hide();
        delete render_surface_;
        render_surface_ = nullptr;
    }

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
    cfg.smoothing.smoothing = mocap_page_->smoothing();

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

    // 交给屏幕外渲染宿主；GL 初始化完成后会通过 initialized 信号接上消费者
    if (render_surface_) {
        render_surface_->set_model(info);
        if (!render_surface_->renderer()) {
            status_model_->setText(
                QStringLiteral("模型: %1（等待 GL 初始化）").arg(info.name));
            return;
        }
    }
    status_model_->setText(QStringLiteral("模型: %1（Cubism）").arg(info.name));
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