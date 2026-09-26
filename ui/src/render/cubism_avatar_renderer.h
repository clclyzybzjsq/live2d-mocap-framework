#pragma once

#include <QImage>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "avatar_renderer.h"

// 渲染后端：D3D11（CubismRenderer_D3D11）。
//
// 为什么不用 OpenGL：Qt Widgets 应用里 QOpenGLContext 只能拿到 Core Profile
// （Cubism 的 ES2 后端渲染空白）；改用原生 WGL 拿到 Compatibility Profile 后
// 同一份代码仍以 GL_INVALID_OPERATION 失败（而 Qt 上下文下正常）。
// D3D11 与 Qt 完全解耦，且本项目平台层已在用 D3D11。
namespace lb::ui {

class D3d11Device;
class D3d11RenderTarget;
class CubismModelRuntime;

// 基于 Live2D Cubism 原生 SDK 的头像渲染器（D3D11 离屏渲染）。
//
// 线程模型：
//   tick()   —— 在持有 D3D11 设备的线程（UI 线程）调用，渲染一帧到离屏渲染
//               目标并读回像素写入内部缓存；
//   render() —— 任意线程（管线回调线程）调用，只从缓存拷贝/缩放，不触碰 GPU。
class CubismAvatarRenderer final : public AvatarRenderer {
public:
    CubismAvatarRenderer();
    ~CubismAvatarRenderer() override;

    bool initialize(const Live2dModelInfo& model) override;
    void render(QImage& target, const lb::core::SemanticParams& sem) override;
    void tick(const lb::core::SemanticParams& sem) override;
    QString name() const override;
    bool model_bound() const override;

    // 渲染分辨率（合成画布通常为相机尺寸 640x480）
    void set_render_size(int w, int h);
    // 取景：zoom 为放大倍数，offset 为画面平移（±1 约等于整幅画面尺寸）
    void set_view(float zoom, float offset_x, float offset_y);
    void set_zoom(float z);
    void set_offset(float x, float y);

private:
    bool ensure_device();
    bool build_model(const Live2dModelInfo& model);
    void release_gpu();
    void apply_params(const lb::core::SemanticParams& sem);
    void render_frame();

    std::unique_ptr<D3d11Device> d3d_;
    std::unique_ptr<D3d11RenderTarget> rt_;
    std::unique_ptr<CubismModelRuntime> runtime_;
    bool device_ready_ = false;

    mutable std::mutex mutex_;
    QImage front_;         // 最近一次渲染结果（供 render() 读取）
    QImage readback_;      // 读回复用缓冲

    int width_ = 640;
    int height_ = 480;
    float zoom_ = 3.5f;
    float offset_x_ = 0.0f;
    float offset_y_ = 0.0f;
    float time_seconds_ = 0.0f;
    float dt_seconds_ = 1.0f / 30.0f;
    std::chrono::steady_clock::time_point last_tick_{};
    bool tick_started_ = false;
    bool ready_ = false;
};

} // namespace lb::ui
