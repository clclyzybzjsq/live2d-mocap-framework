#pragma once

#include "lb/core/motion_pipeline.h"
#include "lb/core/virtual_camera.h"
#include "render/live2d_model_library.h"

#include <QMainWindow>

#include <memory>
#include <mutex>
#include <vector>

class QLabel;
class QAction;
class QListWidget;
class QMenu;
class QPushButton;
class QStackedWidget;
class QSystemTrayIcon;
class QTimer;

namespace lb::ui {

class AvatarRenderer;
class CubismRenderSurface;
class FloatWindowWidget;
class ModelsPage;
class MocapPage;
class OutputPage;
class SettingsPage;
class VcamCompositor;
struct FrameSnapshot;

// 主窗口。布局按方案 7.3：顶栏（唯一主按钮）+ 左导航 + 内容区 + 状态栏。
// M1：开始动捕按钮驱动 MotionPipeline（采集 -> 推理边车 -> 求解 -> 平滑 -> 映射 -> 浮窗）。
class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

public slots:
    void set_running(bool running);
    void set_vcam_enabled(bool on);

    // 启动参数：自动开启虚拟摄像头/浮窗、指定模型
    void apply_startup_options(bool auto_vcam, bool show_float, const QString& model_path);

signals:
    void running_changed(bool running);

private:
    QWidget* build_top_bar();
    QWidget* build_status_bar();
    void build_tray();
    void refresh_status();
    void on_pipeline_frame(const lb::core::FrameView& frame,
                           const lb::core::ModelParamList& params,
                           const lb::core::SemanticParams& semantic,
                           const lb::core::PipelineStats& stats);
    // 模型页选中模型 -> 重建形象渲染器（浮窗与虚拟摄像头共用）
    void on_model_activated(const QString& model_json);

    // 顶栏
    QLabel* status_dot_ = nullptr;
    QLabel* status_text_ = nullptr;
    QLabel* model_name_ = nullptr;
    QPushButton* start_button_ = nullptr;

    // 导航与页面
    QListWidget* nav_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    MocapPage* mocap_page_ = nullptr;
    ModelsPage* models_page_ = nullptr;
    OutputPage* output_page_ = nullptr;

    // 状态栏
    QLabel* status_camera_ = nullptr;
    QLabel* status_infer_ = nullptr;
    QLabel* status_render_ = nullptr;
    QLabel* status_latency_ = nullptr;
    QLabel* status_model_ = nullptr;

    // 托盘
    QSystemTrayIcon* tray_ = nullptr;
    QMenu* tray_menu_ = nullptr;
    QAction* toggle_vcam_ = nullptr;

    // 管线与浮窗
    std::unique_ptr<lb::core::MotionPipeline> pipeline_;
    std::shared_ptr<lb::core::IVirtualCamera> vcam_;
    // 虚拟摄像头输出合成：默认 Live2D 形象 + 虚化背景
    std::unique_ptr<VcamCompositor> vcam_compositor_;
    // 形象渲染器（浮窗与虚拟摄像头合成共用同一实例）
    // 屏幕外的 QOpenGLWidget：为 Cubism 提供可用的 GL 上下文与渲染时机
    // （Qt Widgets 应用里自建离屏上下文渲染不出内容，见 cubism_render_surface.h）
    CubismRenderSurface* render_surface_ = nullptr;
    Live2dModelInfo current_model_;
    // Cubism 渲染需要 GL 上下文，只能在 UI 线程驱动
    QTimer* avatar_timer_ = nullptr;
    std::mutex semantic_mutex_;
    lb::core::SemanticParams last_semantic_;
    // 最近相机帧缓存（BGRA 紧凑拷贝）：动捕未启动时虚拟摄像头仍可输出待机形象
    std::mutex camera_mutex_;
    std::vector<uint8_t> camera_buf_;
    int camera_w_ = 0;
    int camera_h_ = 0;
    bool camera_valid_ = false;
    FloatWindowWidget* float_window_ = nullptr;
    QTimer* ui_timer_ = nullptr;
    bool running_ = false;
    bool vcam_enabled_ = false;
};

} // namespace lb::ui
