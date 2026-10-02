#pragma once

#include "lb/core/model_params.h"
#include "lb/core/motion_pipeline.h"
#include "lb/core/semantic_params.h"

#include <QWidget>

#include <mutex>

namespace lb::ui {

class AvatarRenderer;

// 一次管线输出的快照（用于跨线程投递）。
struct FrameSnapshot {
    lb::core::ModelParamList params;
    lb::core::SemanticParams semantic;
    lb::core::PipelineStats stats;
};

class FloatToolbar;

// M1 占位浮窗：无边框、透明、置顶，按语义参数绘制头像。
// 最终形态为 D3D11 + DirectComposition 渲染 Live2D（M1.5 起替换）。
class FloatWindowWidget final : public QWidget {
    Q_OBJECT
public:
    explicit FloatWindowWidget(QWidget* parent = nullptr);
    ~FloatWindowWidget() override;

    QSize sizeHint() const override;

public slots:
    void set_frame(const FrameSnapshot& snap);
    void set_click_through(bool enabled);
    void set_scale(double scale);
    // 整体缩放：浮窗窗口与窗内画面一起放大/缩小。
    // 与"画面取景"的缩放不同 —— 取景改的是模型的裁剪范围，这里只改显示大小。
    void set_overall_scale(double scale);
    void set_opacity(double opacity);

public:
    // 形象渲染器（不持有所有权）。未设置时回退为内置占位绘制。
    // 与虚拟摄像头合成共用同一渲染器实例，保证两处画面一致。
    void set_avatar_renderer(AvatarRenderer* r);

    bool click_through() const { return click_through_; }
    // 拖动模式：临时挂起穿透，让窗口能接收鼠标事件从而被拖动。
    // 穿透开启时窗口设了 WA_TransparentForMouseEvents，收不到任何鼠标事件，
    // 没有这个开关就完全没法移动浮窗。
    bool drag_mode() const { return drag_mode_; }
    void set_drag_mode(bool on);

signals:
    void click_through_changed(bool enabled);
    void drag_mode_changed(bool on);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void sync_toolbar();
    void save_geometry();
    void load_geometry();
    void apply_size();
    // 鼠标穿透的实际生效开关：穿透开启时仍要能拖动，故取两者的组合
    void apply_mouse_transparency();

    std::mutex mutex_;
    lb::core::SemanticParams semantic_;
    lb::core::PipelineStats stats_;

    AvatarRenderer* avatar_renderer_ = nullptr; // 不持有所有权
    QImage canvas_;                             // 渲染画布缓存（避免每帧分配）

    QPoint drag_offset_;
    bool dragging_ = false;
    bool draggable_ = true;
    bool click_through_ = false;
    bool drag_mode_ = false;
    double scale_ = 1.0;
    // 整体缩放倍率：窗口尺寸与窗内画面同倍率放大（画面相对窗口还会再放大一档，
    // 因此模型在浮窗里占的比例也随之变大，而不是留出更多空白）
    double overall_scale_ = 1.0;

    FloatToolbar* toolbar_ = nullptr;
    bool toolbar_visible_ = false;
};

// 悬浮工具条：独立小窗口，始终可点（穿透模式下仍可操作，方案 7.5）。
class FloatToolbar final : public QWidget {
    Q_OBJECT
public:
    explicit FloatToolbar(FloatWindowWidget* host, QWidget* parent = nullptr);

    void reposition();

private:
    FloatWindowWidget* host_ = nullptr;
};

} // namespace lb::ui

Q_DECLARE_METATYPE(lb::ui::FrameSnapshot)