#include "d3d11_context.h"

#include <QImage>

#include <cstdio>

namespace lb::ui {

namespace {
// 统一的 DXGI 格式：BGRA，与 QImage::Format_ARGB32 内存布局一致
constexpr DXGI_FORMAT kColorFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
} // namespace

// ---------------- D3d11Device ----------------

D3d11Device::~D3d11Device() {
    if (context_) context_->Release();
    if (device_) device_->Release();
}

bool D3d11Device::create() {
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_10_0;

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                   static_cast<UINT>(sizeof(levels) / sizeof(levels[0])),
                                   D3D11_SDK_VERSION, &device_, &got, &context_);
    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
        // 未安装调试层时去掉该标志重试
        flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                               static_cast<UINT>(sizeof(levels) / sizeof(levels[0])),
                               D3D11_SDK_VERSION, &device_, &got, &context_);
    }
    if (FAILED(hr)) {
        std::fprintf(stderr, "[d3d11] D3D11CreateDevice 失败: 0x%08lX\n",
                     static_cast<unsigned long>(hr));
        return false;
    }
    return true;
}

// ---------------- D3d11RenderTarget ----------------

D3d11RenderTarget::~D3d11RenderTarget() {
    if (staging_) staging_->Release();
    if (dsv_) dsv_->Release();
    if (depth_) depth_->Release();
    if (rtv_) rtv_->Release();
    if (color_) color_->Release();
}

bool D3d11RenderTarget::create(D3d11Device& dev, int width, int height) {
    width_ = width;
    height_ = height;
    ID3D11Device* d = dev.device();

    // 颜色缓冲
    D3D11_TEXTURE2D_DESC cd = {};
    cd.Width = static_cast<UINT>(width);
    cd.Height = static_cast<UINT>(height);
    cd.MipLevels = 1;
    cd.ArraySize = 1;
    cd.Format = kColorFormat;
    cd.SampleDesc.Count = 1;
    cd.Usage = D3D11_USAGE_DEFAULT;
    cd.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(d->CreateTexture2D(&cd, nullptr, &color_))) {
        std::fprintf(stderr, "[d3d11] 颜色缓冲创建失败\n");
        return false;
    }
    if (FAILED(d->CreateRenderTargetView(color_, nullptr, &rtv_))) {
        std::fprintf(stderr, "[d3d11] RTV 创建失败\n");
        return false;
    }

    // 深度/模板（Cubism 的剪裁遮罩需要模板）
    D3D11_TEXTURE2D_DESC dd = cd;
    dd.Format = kDepthFormat;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(d->CreateTexture2D(&dd, nullptr, &depth_))) {
        std::fprintf(stderr, "[d3d11] 深度缓冲创建失败\n");
        return false;
    }
    if (FAILED(d->CreateDepthStencilView(depth_, nullptr, &dsv_))) {
        std::fprintf(stderr, "[d3d11] DSV 创建失败\n");
        return false;
    }

    // 读回用的 staging 纹理
    D3D11_TEXTURE2D_DESC sd = cd;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(d->CreateTexture2D(&sd, nullptr, &staging_))) {
        std::fprintf(stderr, "[d3d11] staging 纹理创建失败\n");
        return false;
    }
    return true;
}

void D3d11RenderTarget::bind(ID3D11DeviceContext* ctx) const {
    ID3D11RenderTargetView* rtvs[] = {rtv_};
    ctx->OMSetRenderTargets(1, rtvs, dsv_);
    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<FLOAT>(width_);
    vp.Height = static_cast<FLOAT>(height_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);
}

void D3d11RenderTarget::clear(ID3D11DeviceContext* ctx) const {
    const FLOAT transparent[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    ctx->ClearRenderTargetView(rtv_, transparent);
    ctx->ClearDepthStencilView(dsv_, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

bool D3d11RenderTarget::readback(D3d11Device& dev, ID3D11DeviceContext* ctx, QImage& out) const {
    if (!staging_ || !color_) return false;
    ctx->CopyResource(staging_, color_);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(ctx->Map(staging_, 0, D3D11_MAP_READ, 0, &mapped))) {
        std::fprintf(stderr, "[d3d11] Map 失败\n");
        return false;
    }
    if (out.size() != QSize(width_, height_) || out.format() != QImage::Format_ARGB32) {
        out = QImage(width_, height_, QImage::Format_ARGB32);
    }
    const auto* src = static_cast<const uint8_t*>(mapped.pData);
    for (int y = 0; y < height_; ++y) {
        std::memcpy(out.scanLine(y), src + static_cast<size_t>(y) * mapped.RowPitch,
                    static_cast<size_t>(width_) * 4u);
    }
    ctx->Unmap(staging_, 0);
    (void)dev;
    return true;
}

// ---------------- 纹理上传 ----------------

ID3D11ShaderResourceView* create_texture_from_image(D3d11Device& dev, const QImage& image) {
    if (image.isNull()) return nullptr;
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = static_cast<UINT>(rgba.width());
    td.Height = static_cast<UINT>(rgba.height());
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = rgba.constBits();
    init.SysMemPitch = static_cast<UINT>(rgba.bytesPerLine());

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(dev.device()->CreateTexture2D(&td, &init, &tex))) {
        std::fprintf(stderr, "[d3d11] 模型纹理创建失败\n");
        return nullptr;
    }
    ID3D11ShaderResourceView* srv = nullptr;
    const HRESULT hr = dev.device()->CreateShaderResourceView(tex, nullptr, &srv);
    tex->Release();
    if (FAILED(hr)) return nullptr;
    return srv;
}

} // namespace lb::ui
