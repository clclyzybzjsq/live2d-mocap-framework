#pragma once

namespace lb::core {

// GPU 纹理抽象（方案 2.3 的 GPU 纹理抽象）。
class ITexture2D {
public:
    virtual ~ITexture2D() = default;

    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual void* native_handle() const = 0; // 平台纹理句柄（D3D11 等）
};

class IGpuContext {
public:
    virtual ~IGpuContext() = default;

    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    virtual ITexture2D* create_texture(int width, int height) = 0;
    virtual void destroy_texture(ITexture2D* texture) = 0;

    virtual void begin_frame() = 0;
    virtual void end_frame() = 0;
};

} // namespace lb::core