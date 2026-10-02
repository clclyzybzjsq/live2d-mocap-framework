#include "cubism_render_surface.h"

#include "cubism_avatar_renderer.h"
#include "cubism_runtime.h" // cubism_log

#include <algorithm>

namespace lb::ui {

CubismRenderSurface::CubismRenderSurface(QWidget* parent) : QOpenGLWidget(parent) {
    cubism_log("[surface] 构造\n");
    // 这个 widget 不用于显示，只为拿到一个"在 Qt Widgets 应用里可用"的 GL 上下文。
    // WA_DontShowOnScreen：widget 会被"显示"（GL 上下文因此正常初始化）但不绘制到屏幕。
    // 注意：调用方不要给它设置父窗口 —— 作为子控件时它仍会占用/干扰父窗口的绘制区域
    // （表现为主窗口局部渲染异常），因此这里要求它是独立顶层 widget。
    setAttribute(Qt::WA_DontShowOnScreen, true);
    resize(640, 480);
}

CubismRenderSurface::~CubismRenderSurface() {
    if (renderer_) {
        makeCurrent();
        renderer_.reset(); // 释放 GL 资源（纹理/FBO/渲染器）时上下文需为 current
        doneCurrent();
    }
}

void CubismRenderSurface::set_model(const Live2dModelInfo& model) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_model_ = model;
        has_pending_model_ = true;
    }
    if (renderer_) {
        // 已在运行：直接热切换模型
        makeCurrent();
        renderer_.reset();
        renderer_ = std::make_unique<CubismAvatarRenderer>();
        const bool ok = renderer_->initialize_with_current_context(model);
        doneCurrent();
        interp_.reset(); // 换模型后不要从上一个模型的姿态插值过去
        emit initialized(ok);
    }
    update();
}

void CubismRenderSurface::set_semantics(const lb::core::SemanticParams& sem) {
    std::lock_guard<std::mutex> lock(mutex_);
    semantics_ = sem;
}

void CubismRenderSurface::set_view(float zoom, float offset_x, float offset_y) {
    if (renderer_) renderer_->set_view(zoom, offset_x, offset_y);
}

void CubismRenderSurface::set_motion_gain(float g) {
    if (renderer_) renderer_->set_motion_gain(g);
}

void CubismRenderSurface::set_eye_gain(float g) {
    if (renderer_) renderer_->set_eye_gain(g);
}

void CubismRenderSurface::set_mouth_gain(float g) {
    if (renderer_) renderer_->set_mouth_gain(g);
}

void CubismRenderSurface::set_expression(int index) {
    if (renderer_) renderer_->set_expression(index);
    update(); // 表情切换立刻重画一帧，否则要等下一个渲染节拍
}

std::vector<QString> CubismRenderSurface::expression_names() const {
    return renderer_ ? renderer_->expression_names() : std::vector<QString>{};
}

QImage CubismRenderSurface::frame() const {
    if (!renderer_) return QImage();
    return renderer_->current_frame();
}

bool CubismRenderSurface::ready() const { return renderer_ && renderer_->model_bound(); }

void CubismRenderSurface::initializeGL() {
    cubism_log("[surface] initializeGL 进入\n");
    makeCurrent();
    renderer_ = std::make_unique<CubismAvatarRenderer>();

    bool ok = false;
    Live2dModelInfo model;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        model = pending_model_;
        ok = has_pending_model_ && renderer_->initialize_with_current_context(model);
    }
    doneCurrent();
    cubism_log("[surface] initializeGL 退出 ok=%d\n", ok ? 1 : 0);
    emit initialized(ok);
}

void CubismRenderSurface::paintGL() {
    if (!renderer_ || !renderer_->model_bound()) return;
    lb::core::SemanticParams target;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        target = semantics_;
    }

    // 动捕帧率（约 15fps）明显低于渲染帧率（60fps）。把动捕样本直接贴到模型上，
    // 动作会呈台阶状；这里在渲染帧率上做一次时间常数很短的低通，等价于在动捕
    // 样本之间插值 —— 动作连续，而时间常数只有几十毫秒，几乎不增加可感延迟。
    const auto now = std::chrono::steady_clock::now();
    double dt = 1.0 / 60.0;
    if (last_paint_ts_.time_since_epoch().count() != 0) {
        dt = std::chrono::duration<double>(now - last_paint_ts_).count();
    }
    last_paint_ts_ = now;
    dt = std::clamp(dt, 0.001, 0.2);

    const lb::core::SemanticParams sem = interp_.push(target, dt);

    // 上下文此时由 Qt 保证为 current，直接渲染到渲染器自己的 FBO 并读回缓存
    renderer_->render_once(sem);
}

} // namespace lb::ui
