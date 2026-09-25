#pragma once

#include "lb/core/gpu_context.h"

namespace lb::win {

// D3D11 GPU 上下文。TODO(M1)：创建设备/上下文/交换链（浮窗用 DirectComposition）。
// 当前为桩实现：initialize 返回 false 表示尚未接入。
class WinGpuContext final : public lb::core::IGpuContext,
                            public lb::core::ITexture2D {
public:
    WinGpuContext() = default;
    ~WinGpuContext() override = default;

    // IGpuContext
    bool initialize() override;
    void shutdown() override;
    lb::core::ITexture2D* create_texture(int width, int height) override;
    void destroy_texture(lb::core::ITexture2D* texture) override;
    void begin_frame() override;
    void end_frame() override;

    // ITexture2D（桩：未初始化时为无效纹理）
    int width() const override { return width_; }
    int height() const override { return height_; }
    void* native_handle() const override { return nullptr; }

private:
    bool initialized_ = false;
    int width_ = 0;
    int height_ = 0;
};

} // namespace lb::win