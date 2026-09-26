#include "cubism_avatar_renderer.h"

#include <QImage>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "CubismFramework.hpp"
#include "CubismModelSettingJson.hpp"
#include "Effect/CubismBreath.hpp"
#include "Physics/CubismPhysics.hpp"
#include "Id/CubismIdManager.hpp"
#include "Math/CubismMatrix44.hpp"
#include "Math/CubismModelMatrix.hpp"
#include "Model/CubismModel.hpp"
#include "Model/CubismUserModel.hpp"
#include "Rendering/D3D11/CubismDeviceInfo_D3D11.hpp"
#include "Rendering/D3D11/CubismRenderer_D3D11.hpp"

#include "cubism_runtime.h"
#include "d3d11_context.h"

using namespace Live2D::Cubism::Framework;

namespace lb::ui {

// ---- 模型运行时：加载 moc3/纹理 + 应用参数 + 绘制（D3D11 后端） ----
class CubismModelRuntime final : public CubismUserModel {
public:
    bool load(const std::string& model_json, int w, int h, D3d11Device& dev) {
        cubism_log("[runtime] load 开始: %s\n", model_json.c_str());
        const size_t slash = model_json.find_last_of("/\\");
        dir_ = (slash == std::string::npos) ? std::string() : model_json.substr(0, slash + 1);

        const CubismRuntime::FileBytes json = CubismRuntime::read_file(model_json);
        if (!json.ok()) {
            cubism_log("[runtime] model3.json 读取失败\n");
            return false;
        }
        CubismModelSettingJson setting(json.data.data(),
                                       static_cast<csmSizeInt>(json.data.size()));

        const CubismRuntime::FileBytes moc = CubismRuntime::read_file(dir_ + setting.GetModelFileName());
        if (!moc.ok()) {
            cubism_log("[runtime] moc3 读取失败: %s\n", (dir_ + setting.GetModelFileName()).c_str());
            return false;
        }
        LoadModel(const_cast<csmByte*>(moc.data.data()), static_cast<csmSizeInt>(moc.data.size()));
        if (!GetModel()) {
            cubism_log("[runtime] LoadModel 失败（moc3 无效？）\n");
            return false;
        }
        cubism_log("[runtime] 模型就绪: 参数=%d 绘制对象=%d\n", GetModel()->GetParameterCount(),
                   GetModel()->GetDrawableCount());

        // 关联 Cubism 的 D3D11 设备信息（首次调用会建立着色器与渲染状态）
        Rendering::CubismDeviceInfo_D3D11::GetDeviceInfo(dev.device());

        CreateRenderer(static_cast<csmUint32>(w), static_cast<csmUint32>(h));

        for (csmInt32 i = 0; i < setting.GetTextureCount(); ++i) {
            const csmChar* name = setting.GetTextureFileName(i);
            if (!name || name[0] == '\0') continue;
            const QImage img(QString::fromLocal8Bit((dir_ + name).c_str()));
            ID3D11ShaderResourceView* srv = create_texture_from_image(dev, img);
            if (srv) {
                GetRenderer<Rendering::CubismRenderer_D3D11>()->BindTexture(i, srv);
                textures_.push_back(srv);
            } else {
                cubism_log("[runtime] 纹理上传失败: %s\n", name);
            }
        }
        GetRenderer<Rendering::CubismRenderer_D3D11>()->IsPremultipliedAlpha(false);

        setting.GetLayoutMap(layout_);
        GetModelMatrix()->SetupFromLayout(layout_);

        // 物理演算：让头发/服饰自然摆动（来自 model3.json 的 Physics 引用）
        const csmChar* physics_file = setting.GetPhysicsFileName();
        if (physics_file && physics_file[0] != '\0') {
            const CubismRuntime::FileBytes ph = CubismRuntime::read_file(dir_ + physics_file);
            if (ph.ok()) {
                _physics = CubismPhysics::Create(ph.data.data(),
                                                 static_cast<csmSizeInt>(ph.data.size()));
            }
        }

        // 呼吸：只驱动 ParamBreath。头部参数由动捕实时驱动，
        // 若呼吸也去改 ParamAngleX/Y/Z 会与动捕打架
        _breath = CubismBreath::Create();
        if (_breath) {
            csmVector<CubismBreath::BreathParameterData> breath_params;
            breath_params.PushBack(CubismBreath::BreathParameterData(
                CubismFramework::GetIdManager()->GetId("ParamBreath"), 0.5f, 0.5f, 1.0f, 1.0f));
            _breath->SetParameters(breath_params);
        }

        // 以当前姿态为基准存一次参数
        GetModel()->SaveParameters();
        return true;
    }

    void apply_params(const std::vector<core::CubismParamValue>& params) {
        Csm::CubismModel* m = GetModel();
        if (!m) return;
        for (const core::CubismParamValue& p : params) {
            const CubismIdHandle id = CubismFramework::GetIdManager()->GetId(p.id);
            const csmInt32 index = m->GetParameterIndex(id);
            if (index >= 0) m->SetParameterValue(index, p.value, 1.0f);
        }
    }

    void draw_frame(int width, int height, float zoom, float offset_x, float offset_y) {
        Csm::CubismModel* m = GetModel();
        if (!m) return;

        // 每帧从布局重建模型矩阵：SetWidth 是把变换乘到当前矩阵上，
        // 若直接连续调用会逐帧累加导致模型迅速移出画面
        GetModelMatrix()->SetupFromLayout(layout_);
        GetModelMatrix()->SetWidth(2.0f * zoom);

        CubismMatrix44 projection;
        projection.LoadIdentity();
        // 画布比画面宽时按高度对齐，否则按宽度对齐（保持模型不变形）
        const float canvas_w = m->GetCanvasWidth();
        const float canvas_h = m->GetCanvasHeight();
        const float canvas_aspect = (canvas_h > 0.0f) ? canvas_w / canvas_h : 1.0f;
        const float view_aspect = static_cast<float>(width) / static_cast<float>(height);
        if (canvas_aspect > view_aspect) {
            projection.Scale(1.0f, view_aspect / canvas_aspect);
        } else {
            projection.Scale(canvas_aspect / view_aspect, 1.0f);
        }
        // 取景平移放在投影空间：±1 约等于整幅画面宽度/高度的位移，
        // 与 UI 滑块语义一致（放在模型矩阵里会被 SetWidth 缩放吞掉）
        if (offset_x != 0.0f || offset_y != 0.0f) {
            projection.Translate(offset_x * 2.0f, offset_y * 2.0f);
        }
        projection.MultiplyByMatrix(GetModelMatrix());

        auto* renderer = GetRenderer<Rendering::CubismRenderer_D3D11>();
        if (!renderer) {
            cubism_log("[runtime] D3D11 渲染器为空\n");
            return;
        }
        renderer->SetMvpMatrix(&projection);
        renderer->DrawModel();
    }

    Csm::CubismModel* model() { return GetModel(); }

    // 每帧调用：物理、呼吸，以及动捕未活跃时的待机微动。
    // dt 为距上一帧的秒数；mocap_active 表示动捕当前是否在驱动该模型。
    void update(float dt, bool mocap_active) {
        Csm::CubismModel* m = GetModel();
        if (!m) return;
        if (_physics) _physics->Evaluate(m, dt);
        if (_breath) _breath->UpdateParameters(m, dt);
        if (!mocap_active) apply_idle_motion(dt);
        m->Update();
    }

    // 待机微动：模型本身没有物理/呼吸数据（大量解包模型如此）时，
    // 用程序化的小幅动作让形象不至于像张静止图片：自动眨眼 + 轻微摆动。
    void apply_idle_motion(float dt) {
        idle_time_ += dt;
        Csm::CubismModel* m = GetModel();
        if (!m) return;

        // 眨眼：约 4 秒一次，单次闭合 ~0.12 秒（用两段相位近似闭-开）
        constexpr float kBlinkCycle = 4.0f;
        const float phase = std::fmod(idle_time_, kBlinkCycle);
        const float eye = (phase < 0.07f || (phase >= 0.13f && phase < 0.19f)) ? 0.06f : 1.0f;
        set_param_if_exists("ParamEyeLOpen", eye);
        set_param_if_exists("ParamEyeROpen", eye);

        // 极缓慢的呼吸式起伏与视线漂移（幅度很小，不喧宾夺主）
        set_param_if_exists("ParamAngleX", std::sin(idle_time_ * 0.55f) * 3.0f);
        set_param_if_exists("ParamAngleY", std::sin(idle_time_ * 0.43f) * 2.0f);
        set_param_if_exists("ParamAngleZ", std::sin(idle_time_ * 0.71f) * 2.0f);
        set_param_if_exists("ParamBodyAngleX", std::sin(idle_time_ * 0.90f) * 1.5f);
        set_param_if_exists("ParamBreath", 0.5f + 0.5f * std::sin(idle_time_ * 1.4f));
    }

    void set_param_if_exists(const char* id, float value) {
        Csm::CubismModel* m = GetModel();
        if (!m) return;
        const CubismIdHandle h = CubismFramework::GetIdManager()->GetId(id);
        const csmInt32 idx = m->GetParameterIndex(h);
        if (idx >= 0) m->SetParameterValue(idx, value, 1.0f);
    }

    bool has_physics() const { return _physics != nullptr; }
    bool has_breath() const { return _breath != nullptr; }

    void release_textures() {
        for (ID3D11ShaderResourceView* srv : textures_) {
            if (srv) srv->Release();
        }
        textures_.clear();
    }

private:
    std::string dir_;
    std::vector<ID3D11ShaderResourceView*> textures_;
    csmMap<csmString, csmFloat32> layout_; // 布局信息：每帧据此重建模型矩阵
    float idle_time_ = 0.0f;               // 待机微动的时间累积
};

// ---- CubismAvatarRenderer ----

CubismAvatarRenderer::CubismAvatarRenderer() = default;

CubismAvatarRenderer::~CubismAvatarRenderer() { release_gpu(); }

bool CubismAvatarRenderer::ensure_device() {
    if (device_ready_) return true;
    d3d_ = std::make_unique<D3d11Device>();
    if (!d3d_->create()) {
        cubism_log("[cubism] D3D11 设备创建失败\n");
        d3d_.reset();
        return false;
    }
    cubism_log("[cubism] D3D11 设备就绪\n");
    device_ready_ = true;
    return true;
}

bool CubismAvatarRenderer::build_model(const Live2dModelInfo& model) {
    cubism_log("[cubism] build_model 进入\n");
    if (!CubismRuntime::acquire()) {
        cubism_log("[cubism] CubismRuntime::acquire 失败\n");
        return false;
    }
    runtime_ = std::make_unique<CubismModelRuntime>();
    if (!runtime_->load(model.model_json.toLocal8Bit().toStdString(), width_, height_, *d3d_)) {
        cubism_log("[cubism] 模型运行时加载失败\n");
        runtime_.reset();
        CubismRuntime::release();
        return false;
    }
    return true;
}

void CubismAvatarRenderer::release_gpu() {
    rt_.reset();
    if (runtime_) {
        runtime_->release_textures();
        runtime_->DeleteRenderer();
        runtime_.reset();
        CubismRuntime::release();
    }
    d3d_.reset();
    device_ready_ = false;
    ready_ = false;
}

bool CubismAvatarRenderer::initialize(const Live2dModelInfo& model) {
    if (!model.usable()) {
        cubism_log("[cubism] 模型不可用，跳过初始化\n");
        return false;
    }
    if (!ensure_device()) {
        cubism_log("[cubism] D3D11 初始化失败，回退占位渲染\n");
        return false;
    }

    rt_ = std::make_unique<D3d11RenderTarget>();
    if (!rt_->create(*d3d_, width_, height_)) {
        cubism_log("[cubism] 渲染目标创建失败\n");
        release_gpu();
        return false;
    }

    if (!build_model(model)) {
        cubism_log("[cubism] 模型加载失败\n");
        release_gpu();
        return false;
    }

    ready_ = true;
    cubism_log("[cubism] 初始化完成\n");
    return true;
}

void CubismAvatarRenderer::apply_params(const lb::core::SemanticParams& sem) {
    core::CubismParamValue values[core::kCubismParamMax];
    const size_t n = core::map_semantics_to_cubism(sem, values, core::kCubismParamMax);
    std::vector<core::CubismParamValue> params(values, values + n);
    runtime_->apply_params(params);
}

void CubismAvatarRenderer::render_frame() {
    ID3D11DeviceContext* ctx = d3d_->context();
    rt_->bind(ctx);
    rt_->clear(ctx);
    runtime_->draw_frame(width_, height_, zoom_, offset_x_, offset_y_);
}

void CubismAvatarRenderer::tick(const lb::core::SemanticParams& sem) {
    if (!ready_) return;

    // 时间步长：驱动呼吸与物理演算
    const auto now = std::chrono::steady_clock::now();
    if (tick_started_) {
        const float dt = std::chrono::duration<float>(now - last_tick_).count();
        // 夹住异常间隔（例如窗口被挂起），否则物理会跳变
        dt_seconds_ = std::min(std::max(dt, 0.001f), 0.1f);
    }
    last_tick_ = now;
    tick_started_ = true;
    time_seconds_ += dt_seconds_;

    apply_params(sem);
    // 有人脸 = 动捕在驱动；无人脸 = 待机，用程序化微动让形象活着
    runtime_->update(dt_seconds_, sem.face_detected);
    render_frame();

    // 从渲染目标读回（D3D 的行序自上而下，与 QImage 一致，无需翻转）
    if (rt_ && rt_->readback(*d3d_, d3d_->context(), readback_)) {
        {
            static int dbg = 0;
            if (dbg++ < 4) {
                long opaque = 0;
                for (int y = 0; y < readback_.height(); ++y) {
                    const QRgb* line = reinterpret_cast<const QRgb*>(readback_.constScanLine(y));
                    for (int x = 0; x < readback_.width(); ++x) {
                        if (qAlpha(line[x]) > 8) ++opaque;
                    }
                }
                const double total = static_cast<double>(readback_.width()) * readback_.height();
                cubism_log("[cubism] tick 读回: 非透明=%ld/%.0f (%.1f%%)\n", opaque, total,
                           100.0 * static_cast<double>(opaque) / total);
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        front_ = readback_;
    }
}

void CubismAvatarRenderer::render(QImage& target, const lb::core::SemanticParams&) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (front_.isNull()) return;
    if (front_.size() == target.size()) {
        QPainter p(&target);
        p.drawImage(0, 0, front_);
        return;
    }
    // 画布尺寸与渲染尺寸不一致时等比缩放居中
    const QImage scaled =
        front_.scaled(target.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter p(&target);
    p.drawImage((target.width() - scaled.width()) / 2,
                (target.height() - scaled.height()) / 2, scaled);
}

QString CubismAvatarRenderer::name() const { return QStringLiteral("Cubism"); }

bool CubismAvatarRenderer::model_bound() const { return ready_; }

void CubismAvatarRenderer::set_render_size(int w, int h) {
    if (w > 0 && h > 0) {
        width_ = w;
        height_ = h;
    }
}

void CubismAvatarRenderer::set_view(float zoom, float offset_x, float offset_y) {
    zoom_ = zoom;
    offset_x_ = offset_x;
    offset_y_ = offset_y;
}

void CubismAvatarRenderer::set_zoom(float z) { zoom_ = z; }

void CubismAvatarRenderer::set_offset(float x, float y) {
    offset_x_ = x;
    offset_y_ = y;
}

} // namespace lb::ui
