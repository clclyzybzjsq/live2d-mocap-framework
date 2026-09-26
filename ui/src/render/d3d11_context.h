#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

class QImage;

namespace lb::ui {

// D3D11 离屏渲染基础设施。
//
// 为什么不用 OpenGL：Cubism 的 OpenGL ES2 后端在 Qt Widgets 应用里只能拿到
// Core Profile（渲染空白），而改用原生 WGL 上下文后虽然拿到 Compatibility
// Profile，渲染仍以 GL_INVALID_OPERATION 失败（同一份代码在 Qt 上下文下正常）。
// D3D11 与 Qt 完全解耦，且本项目平台层已在用 D3D11，因此改走这一路径。
class D3d11Device {
public:
    ~D3d11Device();

    bool create();
    ID3D11Device* device() const { return device_; }
    ID3D11DeviceContext* context() const { return context_; }

private:
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
};

// 颜色 + 深度/模板渲染目标，附带 staging 读回
class D3d11RenderTarget {
public:
    ~D3d11RenderTarget();

    bool create(D3d11Device& dev, int width, int height);
    void bind(ID3D11DeviceContext* ctx) const;
    void clear(ID3D11DeviceContext* ctx) const;

    // 读回为 QImage（ARGB32，行序已按 D3D 约定处理）
    bool readback(D3d11Device& dev, ID3D11DeviceContext* ctx, QImage& out) const;

    int width() const { return width_; }
    int height() const { return height_; }

private:
    ID3D11Texture2D* color_ = nullptr;
    ID3D11RenderTargetView* rtv_ = nullptr;
    ID3D11Texture2D* depth_ = nullptr;
    ID3D11DepthStencilView* dsv_ = nullptr;
    ID3D11Texture2D* staging_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

// 把 QImage 上传为纹理并返回 SRV（供 CubismRenderer_D3D11::BindTexture 使用）
ID3D11ShaderResourceView* create_texture_from_image(D3d11Device& dev, const QImage& image);

} // namespace lb::ui
