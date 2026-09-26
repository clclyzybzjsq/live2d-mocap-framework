#pragma once

#include <QImage>
#include <QString>

#include <memory>

#include "lb/core/semantic_params.h"
#include "live2d_model_library.h"

namespace lb::ui {

// 头像渲染器：把语义参数渲染为一张画布图像（供浮窗显示与虚拟摄像头合成共用）。
//
// 当前实现为 QPainter 占位形象；接入 Cubism SDK 后由 CubismAvatarRenderer 替换，
// 调用方（浮窗 / VcamCompositor）无需改动。
//
// 线程约定：render() 由管线回调线程调用。未来的 Cubism 实现必须在内部处理
// GL 上下文线程问题（例如在 UI 线程渲染到 FBO 后双缓冲交给本接口读取）。
class AvatarRenderer {
public:
    virtual ~AvatarRenderer() = default;

    // 绑定模型（占位实现忽略模型信息，总能成功）
    virtual bool initialize(const Live2dModelInfo& model) = 0;

    // UI 线程调用：需要 GL 上下文等线程绑定资源的渲染器在此真正渲染一帧；
    // 占位渲染器不需要，默认空实现。
    virtual void tick(const lb::core::SemanticParams& sem) { (void)sem; }

    // 渲染到 target；target 的尺寸即输出画布尺寸，未绘制区域保持原有 alpha。
    // 可能在管线回调线程调用，实现不得触碰 GL 上下文等线程绑定资源。
    virtual void render(QImage& target, const lb::core::SemanticParams& sem) = 0;

    virtual QString name() const = 0;     // 渲染器显示名（状态栏/日志用）
    virtual bool model_bound() const = 0; // 是否已绑定可渲染模型
};

// M1 占位渲染器：QPainter 绘制的简化形象（无需模型文件即可出画面）
class PlaceholderAvatarRenderer final : public AvatarRenderer {
public:
    bool initialize(const Live2dModelInfo& model) override;
    void render(QImage& target, const lb::core::SemanticParams& sem) override;
    QString name() const override;
    bool model_bound() const override;
};

// 本次构建是否包含 Cubism 渲染支持（-DLB_WITH_CUBISM + SDK 就位）
bool cubism_available();

// 创建最佳可用渲染器：编译了 Cubism 且模型可用则用 Cubism 渲染器，
// 否则回退到占位渲染器（保证任何情况下都有画面）。
std::unique_ptr<AvatarRenderer> create_avatar_renderer(const Live2dModelInfo& model);

} // namespace lb::ui
