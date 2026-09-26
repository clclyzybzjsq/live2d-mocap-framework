#include "cubism_render_surface.h"

#include "cubism_avatar_renderer.h"

namespace lb::ui {

CubismRenderSurface::CubismRenderSurface(QWidget* parent) : QOpenGLWidget(parent) {
    // 这个 widget 不用于显示，只为拿到一个"在 Qt Widgets 应用里可用"的 GL 上下文。
    // 移到屏幕外并保持极小的尺寸，避免干扰用户界面。
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    resize(1, 1);
    move(-10000, -10000);
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

QImage CubismRenderSurface::frame() const {
    if (!renderer_) return QImage();
    return renderer_->current_frame();
}

bool CubismRenderSurface::ready() const { return renderer_ && renderer_->model_bound(); }

void CubismRenderSurface::initializeGL() {
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
    emit initialized(ok);
}

void CubismRenderSurface::paintGL() {
    if (!renderer_ || !renderer_->model_bound()) return;
    lb::core::SemanticParams sem;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sem = semantics_;
    }
    // 上下文此时由 Qt 保证为 current，直接渲染到渲染器自己的 FBO 并读回缓存
    renderer_->render_once(sem);
}

} // namespace lb::ui
