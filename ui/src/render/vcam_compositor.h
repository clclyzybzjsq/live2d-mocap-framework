#pragma once

#include <QImage>

#include <cstdint>
#include <vector>

#include "lb/core/semantic_params.h"
#include "lb/core/types.h"

namespace lb::ui {

// 虚拟摄像头输出内容。
enum class VcamContent : uint8_t {
    Live2dOverBlurredBackground = 0, // 默认：Live2D 形象 + 虚化背景
    BackgroundOnly,                  // 仅背景（无 Live2D 画面），默认关闭的选项
};

// 把「相机帧 + 语义参数」合成为虚拟摄像头输出帧（BGRA32）。
// 背景虚化用降采样往返近似（视觉上接近高斯，成本极低，可每帧调用）。
// 返回的帧视图指向内部缓冲，仅在下次 compose 前有效（与管线回调同步使用）。
class VcamCompositor {
public:
    void set_content(VcamContent c) { content_ = c; }
    VcamContent content() const { return content_; }

    // camera 须为 BGRA32 格式；无法合成时返回 nullptr。
    const lb::core::FrameView* compose(const lb::core::FrameView& camera,
                                       const lb::core::SemanticParams& sem);

private:
    VcamContent content_ = VcamContent::Live2dOverBlurredBackground;
    QImage canvas_;
    std::vector<uint8_t> buffer_;
    lb::core::FrameView view_;
};

} // namespace lb::ui
