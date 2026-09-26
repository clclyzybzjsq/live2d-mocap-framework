#pragma once

#include <QImage>

#include <atomic>
#include <cstdint>
#include <vector>

#include "lb/core/semantic_params.h"
#include "lb/core/types.h"

namespace lb::ui {

class AvatarRenderer;

// 虚拟摄像头输出内容。
enum class VcamContent : uint8_t {
    Live2dOverBlurredBackground = 0, // 默认：Live2D 形象 + 虚化背景
    BackgroundOnly,                  // 仅背景（无 Live2D 画面），默认关闭的选项
};

// 把「相机帧 + 语义参数」合成为虚拟摄像头输出帧（BGRA32）。
// 背景虚化用降采样往返近似（视觉上接近高斯，成本极低，可每帧调用）。
// 人物部分由 AvatarRenderer 绘制（占位渲染 / Cubism 渲染可替换）。
// 返回的帧视图指向内部缓冲，仅在下次 compose 前有效（与管线回调同步使用）。
class VcamCompositor {
public:
    void set_content(VcamContent c) { content_ = c; }
    VcamContent content() const { return content_; }

    // 设置形象渲染器（不持有所有权）。未设置时回退为内置占位绘制。
    void set_avatar_renderer(AvatarRenderer* r) { renderer_.store(r); }
    AvatarRenderer* avatar_renderer() const { return renderer_.load(); }

    // camera 须为 BGRA32 或为空（空表示当前没有相机帧，用兜底背景）。
    // 无法合成时返回 nullptr。
    const lb::core::FrameView* compose(const lb::core::FrameView& camera,
                                       const lb::core::SemanticParams& sem);

    // 渲染画布尺寸（无相机帧时使用）
    void set_canvas_size(int w, int h) {
        width_ = w;
        height_ = h;
    }

private:
    VcamContent content_ = VcamContent::Live2dOverBlurredBackground;
    std::atomic<AvatarRenderer*> renderer_{nullptr};
    int width_ = 640;
    int height_ = 480;
    lb::core::FrameView last_camera_; // 最近一次有相机帧时的视图信息（仅作记录）
    QImage canvas_;
    std::vector<uint8_t> buffer_;
    lb::core::FrameView view_;
};

} // namespace lb::ui
