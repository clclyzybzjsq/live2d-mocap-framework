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
    void set_opacity(double opacity);

public:
    // 形象渲染器（不持有所有权）。未设置时回退为内置占位绘制。
    // 与虚拟摄像头合成共用同一渲染器实例，保证两处画面一致。
    void set_avatar_renderer(AvatarRenderer* r);

    bool click_through() const { return click_through_; }

signals:
    void click_through_changed(bool enabled);

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

    std::mutex mutex_;
    lb::core::SemanticParams semantic_;
    lb::core::PipelineStats stats_;

    AvatarRenderer* avatar_renderer_ = nullptr; // 不持有所有权
    QImage canvas_;                             // 渲染画布缓存（避免每帧分配）

    QPoint drag_offset_;
    bool dragging_ = false;
    bool draggable_ = true;
    bool click_through_ = false;
    double scale_ = 1.0;

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