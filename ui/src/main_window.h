#pragma once

#include "lb/core/motion_pipeline.h"
#include "lb/core/virtual_camera.h"
#include "render/live2d_model_library.h"

#include <QMainWindow>

#include <memory>

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
    std::unique_ptr<AvatarRenderer> avatar_renderer_;
    // 上一次的渲染器：延迟回收，避免管线回调线程仍在使用时被销毁
    std::unique_ptr<AvatarRenderer> retired_renderer_;
    Live2dModelInfo current_model_;
    FloatWindowWidget* float_window_ = nullptr;
    QTimer* ui_timer_ = nullptr;
    bool running_ = false;
    bool vcam_enabled_ = false;
};

} // namespace lb::ui