#include "vcam_compositor.h"

#include <QPainter>

#include <algorithm>
#include <cstring>

#include "avatar_painter.h"
#include "avatar_renderer.h"

namespace lb::ui {

namespace {
// 背景虚化强度：降采样倍数（越大越模糊、成本越低）
constexpr int kBlurDownscale = 12;
// 虚化背景压暗系数：让人物从背景中凸显（虚拟主播画面惯例）
constexpr double kBackgroundDim = 0.72;

// 压暗用查表（避免每像素浮点乘法）
struct DimTable {
    uint8_t v[256];
    DimTable() {
        for (int i = 0; i < 256; ++i) {
            v[i] = static_cast<uint8_t>(i * kBackgroundDim);
        }
    }
};
const DimTable kDim;
} // namespace

const lb::core::FrameView* VcamCompositor::compose(const lb::core::FrameView& camera,
                                                   const lb::core::SemanticParams& sem) {
    if (camera.empty() || camera.format != lb::core::PixelFormat::BGRA32) return nullptr;
    const int w = camera.width;
    const int h = camera.height;

    // 输入帧直接包成 QImage（只读、不拷贝）。小端下 ARGB32 的内存布局即 BGRA。
    const QImage src(static_cast<const uchar*>(camera.plane0), w, h,
                     static_cast<int>(camera.stride0), QImage::Format_ARGB32);

    // 背景：降采样到 1/kBlurDownscale 再用双线性放大回来，得到廉价的大半径模糊
    const int dw = std::max(1, w / kBlurDownscale);
    const int dh = std::max(1, h / kBlurDownscale);
    const QImage small = src.scaled(dw, dh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    canvas_ = small.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    // 背景压暗：人物（灰调形象）与虚化背景亮度接近时几乎看不出轮廓，
    // 压暗背景是虚拟主播画面的常规做法
    for (int y = 0; y < h; ++y) {
        uchar* line = canvas_.scanLine(y);
        for (int x = 0; x < w; ++x) {
            uchar* p = line + x * 4;
            p[0] = kDim.v[p[0]];
            p[1] = kDim.v[p[1]];
            p[2] = kDim.v[p[2]];
        }
    }

    // Live2D 形象叠加（默认内容；仅背景模式下跳过）
    if (content_ == VcamContent::Live2dOverBlurredBackground) {
        AvatarRenderer* r = renderer_.load();
        if (r) {
            // 渲染器自行决定线程策略（占位渲染直接 QPainter，Cubism 内部处理 GL 线程）
            r->render(canvas_, sem);
        } else {
            // 未设置渲染器时的内置回退
            QPainter p(&canvas_);
            p.setRenderHint(QPainter::Antialiasing, true);
            paint_avatar(p, w, h, sem);
        }
    }

    // 输出到紧凑 BGRA 缓冲（QImage 每行有对齐填充，写端要求连续 pitch）
    const size_t pitch = static_cast<size_t>(w) * 4u;
    buffer_.resize(pitch * static_cast<size_t>(h));
    for (int y = 0; y < h; ++y) {
        std::memcpy(buffer_.data() + pitch * static_cast<size_t>(y),
                    canvas_.constScanLine(y), pitch);
    }

    view_.plane0 = buffer_.data();
    view_.stride0 = static_cast<uint32_t>(pitch);
    view_.plane1 = nullptr;
    view_.stride1 = 0;
    view_.width = w;
    view_.height = h;
    view_.format = lb::core::PixelFormat::BGRA32;
    view_.timestamp_us = camera.timestamp_us;
    return &view_;
}

} // namespace lb::ui
