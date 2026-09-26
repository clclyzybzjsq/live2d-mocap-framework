#pragma once

#include <QImage>
#include <QOpenGLWidget>

#include <memory>
#include <mutex>

#include "lb/core/semantic_params.h"
#include "live2d_model_library.h"

namespace lb::ui {

class CubismAvatarRenderer;

// 屏幕外的 Live2D 渲染宿主。
//
// 为什么需要它：在 Qt Widgets 应用里，自建的离屏 GL 上下文
// （QOpenGLContext + QOffscreenSurface）会出现"渲染调用全部成功、fbo 内容却
// 全透明"的现象（同一份渲染器代码在 QGuiApplication 的测试工具里正常）。
// QtLive2d 项目（github.com/duanxianpi/QtLive2d）的做法是用 QOpenGLWidget 的
// 上下文渲染，这条路是可用的，因此这里借用一个被移到屏幕外的 QOpenGLWidget
// 来提供可正常工作的上下文与渲染时机，渲染结果仍写回渲染器内部缓存，
// 由浮窗与虚拟摄像头合成器读取（不依赖 widget 的显示内容）。
class CubismRenderSurface final : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit CubismRenderSurface(QWidget* parent = nullptr);
    ~CubismRenderSurface() override;

    // 设置要渲染的模型；若 GL 尚未初始化则记下，待 initializeGL 时加载
    void set_model(const Live2dModelInfo& model);
    // 更新语义参数（下一帧渲染时生效）
    void set_semantics(const lb::core::SemanticParams& sem);
    // 取最近一帧（任意线程）
    QImage frame() const;
    bool ready() const;
    // 渲染器实例（可能为空，需等 initialized 信号）
    CubismAvatarRenderer* renderer() { return renderer_.get(); }

    // 取景（缩放 + 位置）
    void set_view(float zoom, float offset_x, float offset_y);

signals:
    void initialized(bool ok);

protected:
    void initializeGL() override;
    void paintGL() override;

private:
    std::unique_ptr<CubismAvatarRenderer> renderer_;
    Live2dModelInfo pending_model_;
    bool has_pending_model_ = false;
    lb::core::SemanticParams semantics_;
    mutable std::mutex mutex_;
};

} // namespace lb::ui
