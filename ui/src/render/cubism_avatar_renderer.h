#pragma once

#include <QImage>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "avatar_renderer.h"

// 注意：本头文件刻意不包含 Qt 的 OpenGL 头（QOpenGLContext 等）。
// 实现文件必须先包含 <GL/glew.h> 再包含 Qt GL 头，否则 glew 会报
// "gl.h included before glew.h" 并引发 GL 类型重复定义。
class QOpenGLContext;
class QOffscreenSurface;
class QOpenGLFramebufferObject;

namespace lb::ui {

class CubismModelRuntime;

// 基于 Live2D Cubism 原生 SDK 的头像渲染器（OpenGL 离屏渲染）。
//
// 线程模型：
//   tick()   —— 必须在创建了 GL 上下文的线程（UI 线程）调用，真正渲染一帧到
//               离屏 FBO，读回像素写入内部缓存；
//   render() —— 任意线程（管线回调线程）调用，只从缓存拷贝/缩放，不触碰 GL。
// 这样管线的帧率与 GL 上下文线程互不干扰。
class CubismAvatarRenderer final : public AvatarRenderer {
public:
    CubismAvatarRenderer();
    ~CubismAvatarRenderer() override;

    bool initialize(const Live2dModelInfo& model) override;
    void render(QImage& target, const lb::core::SemanticParams& sem) override;
    void tick(const lb::core::SemanticParams& sem) override;
    QString name() const override;
    bool model_bound() const override;

    // ---- 外部上下文模式（推荐）----
    // Qt Widgets 应用里自建的离屏 GL 上下文（QOpenGLContext + QOffscreenSurface）
    // 会出现"渲染调用成功但产出全透明"，而 QOpenGLWidget 提供的上下文可正常工作
    // （参考 QtLive2d 的做法）。这两个接口用于由 QOpenGLWidget 驱动渲染：
    //   initialize_with_current_context() —— 在 initializeGL() 里调用，上下文已 current
    //   render_once()                     —— 在 paintGL() 里调用，渲染并读回缓存
    bool initialize_with_current_context(const Live2dModelInfo& model);
    void render_once(const lb::core::SemanticParams& sem);
    // 最近一次渲染结果（任意线程，加锁拷贝）
    QImage current_frame() const;

    // 渲染分辨率（合成画布通常为相机尺寸 640x480）
    void set_render_size(int w, int h);
    // 模型显示缩放（1.0 为默认适配；越大模型越大）
    void set_zoom(float z);
    // 模型显示位置偏移（归一化，0 为中心）
    void set_offset(float x, float y);

private:
    bool ensure_gl();
    bool build_model(const Live2dModelInfo& model);
    void release_gl();
    void apply_params(const lb::core::SemanticParams& sem);
    void render_frame();

    std::unique_ptr<QOpenGLContext> context_;
    std::unique_ptr<QOffscreenSurface> surface_;
    std::unique_ptr<QOpenGLFramebufferObject> fbo_;
    std::unique_ptr<CubismModelRuntime> runtime_;
    std::vector<unsigned int> textures_;

    std::string model_dir_;

    mutable std::mutex mutex_;
    QImage front_; // 最近一次渲染结果（供 render() 读取）

    int width_ = 640;
    int height_ = 480;
    float zoom_ = 3.5f;
    float offset_x_ = 0.0f;
    float offset_y_ = -0.2f;
    float time_seconds_ = 0.0f;
    bool gl_ready_ = false;
    bool ready_ = false;
};

} // namespace lb::ui
