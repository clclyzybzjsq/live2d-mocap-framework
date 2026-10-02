#include "cubism_avatar_renderer.h"

// glew.h 必须先于任何 Qt/系统 GL 头
#include <GL/glew.h>

#include <QOpenGLContext>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QOffscreenSurface>
#include <QImage>
#include <QPainter>
#include <QSurfaceFormat>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "CubismFramework.hpp"
#include "CubismModelSettingJson.hpp"
#include "Id/CubismIdManager.hpp"
#include "Math/CubismMatrix44.hpp"
#include "Math/CubismModelMatrix.hpp"
#include "Model/CubismModel.hpp"
#include "Model/CubismUserModel.hpp"
#include "Motion/CubismExpressionMotion.hpp"
#include "Motion/CubismExpressionMotionManager.hpp"
#include "Rendering/OpenGL/CubismOffscreenManager_OpenGLES2.hpp"
#include "Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp"

#include "cubism_runtime.h"

using namespace Live2D::Cubism::Framework;

namespace lb::ui {

// ---- 模型运行时：加载 moc3/纹理 + 应用参数 + 绘制 ----
class CubismModelRuntime final : public CubismUserModel {
public:
    ~CubismModelRuntime() override {
        // 表情 motion 是 autoDelete=false 提交给队列的，队列不会释放它们
        for (ExpressionEntry& e : expressions_) {
            if (e.motion) ACubismMotion::Delete(e.motion);
        }
        expressions_.clear();
    }

    bool load(const std::string& model_json, int w, int h) {
        cubism_log("[cubism] 载入模型: %s\n", model_json.c_str());
        const size_t slash = model_json.find_last_of("/\\");
        dir_ = (slash == std::string::npos) ? std::string() : model_json.substr(0, slash + 1);

        const CubismRuntime::FileBytes json = CubismRuntime::read_file(model_json);
        if (!json.ok()) return false;
        CubismModelSettingJson setting(json.data.data(),
                                       static_cast<csmSizeInt>(json.data.size()));

        const CubismRuntime::FileBytes moc = CubismRuntime::read_file(dir_ + setting.GetModelFileName());
        if (!moc.ok()) return false;
        LoadModel(const_cast<csmByte*>(moc.data.data()), static_cast<csmSizeInt>(moc.data.size()));
        if (!GetModel()) return false;
        update_bounds();
        log_param_ranges();

        CreateRenderer(static_cast<csmUint32>(w), static_cast<csmUint32>(h));

        {
            // 表情：逐条加载并保留 motion 对象（autoDelete=false，指针由本类持有，
            // 析构时统一释放）。名字取文件名去掉 .exp3.json 的部分，即 exp_xxx。
            for (csmInt32 i = 0; i < setting.GetExpressionCount(); ++i) {
                const csmChar* file = setting.GetExpressionFileName(i);
                if (!file || file[0] == '\0') continue;
                const CubismRuntime::FileBytes bytes = CubismRuntime::read_file(dir_ + file);
                if (!bytes.ok()) {
                    cubism_log("[cubism] 表情载入失败: %s\n", file);
                    continue;
                }
                std::string name = file;
                const size_t ext = name.find(".exp3");
                if (ext != std::string::npos) name = name.substr(0, ext);
                ACubismMotion* motion = LoadExpression(
                    bytes.data.data(), static_cast<csmSizeInt>(bytes.data.size()), name.c_str());
                if (motion) expressions_.push_back(ExpressionEntry{name, motion});
            }
            cubism_log("[cubism] 表情 %d 个\n", static_cast<int>(expressions_.size()));
        }

        for (csmInt32 i = 0; i < setting.GetTextureCount(); ++i) {
            const csmChar* name = setting.GetTextureFileName(i);
            if (!name || name[0] == '\0') continue;
            const unsigned int tex = upload_texture(dir_ + name);
            if (tex != 0) {
                GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->BindTexture(i, tex);
                textures_.push_back(tex);
                cubism_log("[cubism] 纹理 %d 载入成功: %s\n", static_cast<int>(i), name);
            } else {
                // 纹理缺失会让对应 drawable 采样到 0，整块画面可能全透明
                cubism_log("[cubism] 纹理 %d 载入失败: %s\n", static_cast<int>(i), name);
            }
        }
        GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->IsPremultipliedAlpha(false);

        setting.GetLayoutMap(layout_);
        {
            // 布局尺寸才是模型顶点坐标系的范围；GetCanvasWidth/Height 是 moc3 的
            // 画布单位（本模型为 1.0x1.5），两者不是同一套尺度。
            for (csmMap<csmString, csmFloat32>::const_iterator it = layout_.Begin();
                 it != layout_.End(); ++it) {
                cubism_log("[cubism] layout %s=%.4f\n", it->First.GetRawString(), it->Second);
            }
            cubism_log("[cubism] canvas=%.4fx%.4f（画布单位）\n", GetModel()->GetCanvasWidth(),
                       GetModel()->GetCanvasHeight());
        }
        GetModelMatrix()->SetupFromLayout(layout_);

        GetModel()->SaveParameters();
        return true;
    }

    // ---- 表情 ----
    // 表情是一组"参数覆盖"，必须在本帧 model()->Update() 之前施加，
    // 否则参数改了也不会反映到顶点（Update 才把参数烘进顶点）。
    int expression_count() const { return static_cast<int>(expressions_.size()); }

    std::string expression_name(int index) const {
        return (index >= 0 && index < static_cast<int>(expressions_.size()))
            ? expressions_[index].name
            : std::string();
    }

    int current_expression() const { return current_expression_; }

    void set_expression(int index) {
        if (!_expressionManager || !GetModel()) return;
        _expressionManager->StopAllMotions();
        current_expression_ = -1;
        if (index >= 0 && index < static_cast<int>(expressions_.size())) {
            // autoDelete=false：motion 对象由本类持有并在析构时释放，
            // 交给队列删除的话再切回来就是悬空指针
            _expressionManager->StartMotion(expressions_[index].motion, false);
            current_expression_ = index;
        }
    }

    void apply_expression(float dt_seconds) {
        if (_expressionManager && GetModel()) {
            _expressionManager->UpdateMotion(GetModel(), dt_seconds);
        }
    }

    void apply_params(const std::vector<core::CubismParamValue>& params) {
        Csm::CubismModel* m = GetModel();
        if (!m) return;
        for (const core::CubismParamValue& p : params) {
            const CubismIdHandle id = CubismFramework::GetIdManager()->GetId(p.id);
            const csmInt32 index = m->GetParameterIndex(id);
            if (index >= 0) m->SetParameterValue(index, to_model_range(m, index, p.id, p.value), 1.0f);
        }
    }

    void draw_frame(int width, int height, float zoom, float offset_x, float offset_y) {
        Csm::CubismModel* m = GetModel();
        if (!m) return;

        Rendering::CubismOffscreenManager_OpenGLES2::GetInstance()->BeginFrameProcess();

        const float canvas_w = m->GetCanvasWidth();
        const float canvas_h = m->GetCanvasHeight();

        {
            // 顶点包围盒：画布尺寸（GetCanvasWidth/Height）是 moc3 的画布单位
            // （本模型 1.0x1.5），与顶点坐标系不是一套尺度 —— 顶点实际只有
            // 0.39x1.28 且本来就以原点为中心。fit 必须按顶点范围算。
            static int b_ = 0;
            if (b_++ < 2) {
                // 顺便统计可见 drawable：全部不可见时画面必然全透明，
                // 这与"矩阵算错"是不同的故障，日志里要能一眼分开
                csmInt32 visible = 0;
                const csmInt32 dn = m->GetDrawableCount();
                for (csmInt32 i = 0; i < dn; ++i) {
                    if (m->GetDrawableOpacity(i) > 0.01f) ++visible;
                }
                cubism_log("[cubism] 顶点范围 x=[%.3f,%.3f] y=[%.3f,%.3f] drawables=%d 可见=%d\n",
                           bounds_min_x_, bounds_max_x_, bounds_min_y_, bounds_max_y_,
                           static_cast<int>(dn), static_cast<int>(visible));
            }
        }
        // 视口宽高比补偿。这里只用"视口"宽高比：
        // 模型矩阵自身已是等比缩放（CubismModelMatrix 用同一系数作用于 X/Y），
        // 再把画布宽高比（canvas_aspect）掺进来就是二次校正 —— 那正是之前
        // 模型被横向压扁、看着像纵向拉伸的原因。
        CubismMatrix44 projection;
        projection.LoadIdentity();
        const float view_aspect = static_cast<float>(width) / static_cast<float>(height);
        if (view_aspect >= 1.0f) {
            projection.Scale(1.0f / view_aspect, 1.0f);
        } else {
            projection.Scale(1.0f, view_aspect);
        }

        // 模型矩阵保持原方案：按布局重建 + SetWidth 等比缩放。
        // （SetWidth 的基准是画布宽度；顶点本身以原点为中心，不需要额外居中平移。
        //   曾尝试改用顶点包围盒重新计算 fit，会把画面整成空白，已回退。）
        GetModelMatrix()->SetupFromLayout(layout_);
        GetModelMatrix()->SetWidth(2.0f * zoom);
        // 平移以"模型自身尺寸"为单位，而不是取景空间单位。
        // 取景空间单位下，同一个 offset 在倍率越大时对应的模型位移越小 ——
        // zoom=40 时模型在取景空间里高达 120 单位，而平移上限只有 1，
        // 于是滑块顶到极值也移不到模型的其它部位（就是"极值不够"）。
        // 现在 offset = ±1 恰好等于移动"半个模型尺寸"，
        // 因此任何倍率下 ±1 都能覆盖模型从顶到底的全部范围，±2 留一档余量。
        {
            const float cw = (canvas_w > 0.0f) ? canvas_w : 1.0f;
            const float ch = (canvas_h > 0.0f) ? canvas_h : 1.0f;
            const float model_w = 2.0f * zoom;         // 模型在取景空间中的宽度
            const float model_h = model_w * (ch / cw); // 等比换算的高度
            if (offset_x != 0.0f || offset_y != 0.0f) {
                GetModelMatrix()->Translate(offset_x * model_w * 0.5f, offset_y * model_h * 0.5f);
            }
        }

        {
            static int c_ = 0;
            if (c_++ < 2) {
                cubism_log("[cubism] 画布=%.2fx%.2f 视口=%dx%d aspect=%.3f setWidth=%.3f zoom=%.2f "
                           "off=(%.2f,%.2f)\n",
                           canvas_w, canvas_h, width, height, view_aspect, 2.0f * zoom, zoom,
                           offset_x, offset_y);
            }
        }
        projection.MultiplyByMatrix(GetModelMatrix());

        const float* mm = GetModelMatrix()->GetArray();
        GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->SetMvpMatrix(&projection);
        GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->DrawModel();
        {
            static int m_ = 0;
            if (m_++ < 2) {
                const float* pm = projection.GetArray();
                cubism_log("[cubism] M=[sx=%.4f sy=%.4f tx=%.4f ty=%.4f] P=[sx=%.4f sy=%.4f tx=%.4f "
                           "ty=%.4f] glerr=%d\n",
                           mm[0], mm[5], mm[12], mm[13], pm[0], pm[5], pm[12], pm[13],
                           static_cast<int>(glGetError()));
            }
        }

        Rendering::CubismOffscreenManager_OpenGLES2::GetInstance()->EndFrameProcess();
        Rendering::CubismOffscreenManager_OpenGLES2::GetInstance()->ReleaseStaleRenderTextures();
    }

    Csm::CubismModel* model() { return GetModel(); }

    // 单极通道（映射层按 0..1 表达"意图"）在模型里的实际量程未必是 0..1：
    // 有的模型闭眼要走到负值（ParamEyeLOpen 最小可达 -0.3），照搬 0..1
    // 就永远闭不严、嘴也张不满。这里把意图线性拉伸到模型的实际量程，
    // 保证 0 -> 模型的完全闭合端、1 -> 完全张开端。
    // 其余通道（角度、视线等）量程与意图一致或已由映射层夹好，不做拉伸，
    // 避免在量程偏大的模型上把动作放大过头。
    static float to_model_range(Csm::CubismModel* m, csmInt32 index, const char* id, float value) {
        struct Unipolar {
            const char* id;
        };
        static const Unipolar kUnipolar[] = {
            {"ParamEyeLOpen"}, {"ParamEyeROpen"}, {"ParamMouthOpenY"},
        };
        for (const Unipolar& u : kUnipolar) {
            if (std::strcmp(u.id, id) != 0) continue;
            const float lo = m->GetParameterMinimumValue(index);
            const float hi = m->GetParameterMaximumValue(index);
            if (hi <= lo) break;
            const float t = std::clamp(value, 0.0f, 1.0f);
            return lo + t * (hi - lo);
        }
        return value;
    }

    // 量程诊断：一次把关键参数在模型里的实际上下限打出来
    void log_param_ranges() {
        Csm::CubismModel* m = GetModel();
        if (!m) return;
        static const char* kIds[] = {
            "ParamEyeLOpen", "ParamEyeROpen", "ParamEyeBallX", "ParamEyeBallY",
            "ParamMouthOpenY", "ParamMouthForm", "ParamAngleX", "ParamAngleZ",
            "ParamBodyAngleY", "ParamBodyAngleZ",
        };
        for (const char* raw : kIds) {
            const CubismIdHandle id = CubismFramework::GetIdManager()->GetId(raw);
            const csmInt32 i = m->GetParameterIndex(id);
            if (i < 0) {
                cubism_log("[cubism] 参数 %s: 模型没有\n", raw);
                continue;
            }
            cubism_log("[cubism] 参数 %s: %.3f .. %.3f (默认 %.3f)\n", raw,
                       m->GetParameterMinimumValue(i), m->GetParameterMaximumValue(i),
                       m->GetParameterDefaultValue(i));
        }
    }

    // 统计模型顶点的包围盒（取景 fit 的依据）。
    // 在加载完成、参数仍为默认值时统计一次即可。
    void update_bounds() {
        Csm::CubismModel* m = GetModel();
        if (!m) return;
        float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
        const csmInt32 dn = m->GetDrawableCount();
        for (csmInt32 i = 0; i < dn; ++i) {
            const csmInt32 vc = m->GetDrawableVertexCount(i);
            const auto* pos = m->GetDrawableVertexPositions(i);
            if (!pos) continue;
            for (csmInt32 v = 0; v < vc; ++v) {
                const float x = pos[v].X;
                const float y = pos[v].Y;
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
            }
        }
        if (maxx > minx && maxy > miny) {
            bounds_min_x_ = minx;
            bounds_max_x_ = maxx;
            bounds_min_y_ = miny;
            bounds_max_y_ = maxy;
        }
    }

    void release_textures() {
        for (unsigned int t : textures_) glDeleteTextures(1, &t);
        textures_.clear();
    }

private:
    struct ExpressionEntry {
        std::string name;
        ACubismMotion* motion = nullptr;
    };

    static unsigned int upload_texture(const std::string& path) {
        QImage img(QString::fromLocal8Bit(path.c_str()));
        if (img.isNull()) return 0;
        img = img.convertToFormat(QImage::Format_RGBA8888);
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(), 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, img.constBits());
        // 必须生成 mipmap：否则纹理不完整，采样恒为黑色
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
        return tex;
    }

    std::string dir_;
    std::vector<unsigned int> textures_;
    std::vector<ExpressionEntry> expressions_;
    int current_expression_ = -1;
    csmMap<csmString, csmFloat32> layout_; // 布局信息：每帧据此重建模型矩阵
    // 顶点包围盒（加载时统计）
    float bounds_min_x_ = -0.5f;
    float bounds_max_x_ = 0.5f;
    float bounds_min_y_ = -0.5f;
    float bounds_max_y_ = 0.5f;
};

// ---- CubismAvatarRenderer ----

CubismAvatarRenderer::CubismAvatarRenderer() = default;

CubismAvatarRenderer::~CubismAvatarRenderer() {
    if (gl_ready_ && context_ && surface_) {
        context_->makeCurrent(surface_.get());
        release_gl();
        context_->doneCurrent();
    }
}

bool CubismAvatarRenderer::ensure_gl() {
    if (gl_ready_) return true;

    // 诊断：Qt Widgets 应用可能已经创建了全局共享上下文 / 自己的上下文，
    // 与我们的离屏上下文冲突时会出现"渲染调用成功但产出全透明"
    {
        const QSurfaceFormat def = QSurfaceFormat::defaultFormat();
        const QOpenGLContext* global = QOpenGLContext::globalShareContext();
        const QOpenGLContext* cur = QOpenGLContext::currentContext();
        cubism_log("[gl] defaultFormat=%d.%d profile=%d | globalShare=%p | current=%p\n",
                   def.majorVersion(), def.minorVersion(), static_cast<int>(def.profile()),
                   static_cast<const void*>(global), static_cast<const void*>(cur));
    }

    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);

    context_ = std::make_unique<QOpenGLContext>();
    context_->setFormat(format);
    if (!context_->create()) {
        cubism_log("[gl] GL 上下文创建失败\n");
        return false;
    }
    {
        const QSurfaceFormat got = context_->format();
        cubism_log("[gl] 创建后 format=%d.%d profile=%d | shareContext=%p\n",
                   got.majorVersion(), got.minorVersion(), static_cast<int>(got.profile()),
                   static_cast<const void*>(context_->shareContext()));
    }
    surface_ = std::make_unique<QOffscreenSurface>();
    surface_->setFormat(context_->format());
    surface_->create();
    if (!surface_->isValid() || !context_->makeCurrent(surface_.get())) {
        std::fprintf(stderr, "[cubism] 离屏 surface 不可用\n");
        return false;
    }
    // GLEW 必须在 GL 上下文为 current 时初始化；失败即致命——
    // 否则后续 GL 调用会跳到空函数指针（表现为 shader 加载阶段崩溃）
    glewExperimental = GL_TRUE;
    const GLenum err = glewInit();
    if (err != GLEW_OK) {
        const auto* msg = reinterpret_cast<const char*>(glewGetErrorString(err));
        std::fprintf(stderr, "[cubism] glewInit 失败: %s\n", msg ? msg : "?");
        context_->doneCurrent();
        return false;
    }
    if (glCreateShader == nullptr || glGenBuffers == nullptr) {
        std::fprintf(stderr, "[cubism] GL 函数指针不可用，上下文异常\n");
        context_->doneCurrent();
        return false;
    }
    {
        const auto* ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        std::fprintf(stderr, "[cubism] GL %s\n", ver ? ver : "?");
    }
    glGetError(); // 清掉 glewInit 可能留下的 INVALID_ENUM
    context_->doneCurrent();
    gl_ready_ = true;
    return true;
}

bool CubismAvatarRenderer::build_model(const Live2dModelInfo& model) {
    cubism_log("[cubism] build_model: acquire\n");
    if (!CubismRuntime::acquire()) return false;
    cubism_log("[cubism] build_model: 构造 runtime\n");
    runtime_ = std::make_unique<CubismModelRuntime>();
    cubism_log("[cubism] build_model: 载入模型\n");
    if (!runtime_->load(model.model_json.toLocal8Bit().toStdString(), width_, height_)) {
        runtime_.reset();
        CubismRuntime::release();
        return false;
    }
    cubism_log("[cubism] build_model: 完成\n");
    return true;
}

void CubismAvatarRenderer::release_gl() {
    if (runtime_) {
        runtime_->release_textures();
        runtime_->DeleteRenderer();
        runtime_.reset();
        CubismRuntime::release();
    }
    fbo_.reset();
}

bool CubismAvatarRenderer::initialize_with_current_context(const Live2dModelInfo& model) {
    // 上下文由外部（QOpenGLWidget）建立且已 current，这里不再自行创建上下文。
    // 这是 Qt Widgets 应用里可用的路径：自建离屏上下文会渲染出全透明画面。
    if (ready_) return true;
    if (!model.usable()) {
        cubism_log("[cubism] 模型不可用\n");
        return false;
    }
    if (!QOpenGLContext::currentContext()) {
        cubism_log("[cubism] 当前没有 GL 上下文（需在 initializeGL 中调用）\n");
        return false;
    }

    // 关键：初始化 GL 函数指针。Cubism 的 OpenGL 后端通过 GLEW 提供全局 GL 函数，
    // 不初始化它们会在 CreateRenderer 里调用空函数指针直接崩溃（栈顶地址 0x0）。
    glewExperimental = GL_TRUE;
    const GLenum err = glewInit();
    if (err != GLEW_OK) {
        const auto* msg = reinterpret_cast<const char*>(glewGetErrorString(err));
        cubism_log("[cubism] glewInit 失败: %s\n", msg ? msg : "?");
        return false;
    }
    glGetError(); // 清掉 glewInit 可能留下的 INVALID_ENUM
    {
        const auto* ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        const auto* ren = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        cubism_log("[cubism] 上下文 GL=%s | %s\n", ver ? ver : "?", ren ? ren : "?");
    }
    // （曾经尝试"先建 FBO 并绑定、再 CreateRenderer"来修正遮罩缓冲尺寸，
    //   实测反而让渲染结果变成全透明，故保持这个顺序。）
    if (!build_model(model)) {
        cubism_log("[cubism] 模型加载失败\n");
        return false;
    }

    QOpenGLFramebufferObjectFormat fmt;
    fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    fmt.setSamples(0);
    fbo_ = std::make_unique<QOpenGLFramebufferObject>(width_, height_, fmt);
    if (!fbo_->isValid()) {
        cubism_log("[cubism] FBO 创建失败\n");
        release_gl();
        return false;
    }
    ready_ = true;
    cubism_log("[cubism] 已在外部上下文中初始化（%dx%d）\n", width_, height_);
    return true;
}

void CubismAvatarRenderer::render_once(const lb::core::SemanticParams& sem) {
    if (!ready_ || !fbo_) return;
    apply_params(sem);
    // 表情是参数覆盖，必须在 Update() 之前施加（Update 才把参数烘进顶点）
    if (runtime_) runtime_->apply_expression(expression_dt());
    if (runtime_->model()) runtime_->model()->Update();
    render_frame();
    const QImage frame = fbo_->toImage().convertToFormat(QImage::Format_ARGB32);
    {
        // 在多帧上采样：模型是热切换加载的，最初几帧可能还没画面，
        // 只看头 3 帧会把"正常渲染"误判成"全透明"
        static long n = 0;
        ++n;
        if (n == 1 || n == 10 || n == 30) {
            long op = 0;
            int minx = frame.width(), maxx = -1, miny = frame.height(), maxy = -1;
            for (int y = 0; y < frame.height(); ++y) {
                const QRgb* ln = reinterpret_cast<const QRgb*>(frame.constScanLine(y));
                for (int x = 0; x < frame.width(); ++x) {
                    if (qAlpha(ln[x]) > 8) {
                        ++op;
                        if (x < minx) minx = x;
                        if (x > maxx) maxx = x;
                        if (y < miny) miny = y;
                        if (y > maxy) maxy = y;
                    }
                }
            }
            const double pct =
                100.0 * op / (static_cast<double>(frame.width()) * frame.height());
            if (maxx < 0) {
                cubism_log("[cubism] 第%ld帧 非透明=0 (0.0%%) 画面为空 zoom=%.2f off=(%.2f,%.2f)\n",
                           n, zoom_, offset_x_, offset_y_);
            } else {
                cubism_log("[cubism] 第%ld帧 非透明=%ld (%.1f%%) 包围盒=[%d,%d]-[%d,%d] %dx%d "
                           "zoom=%.2f off=(%.2f,%.2f)\n",
                           n, op, pct, minx, miny, maxx, maxy, maxx - minx + 1, maxy - miny + 1,
                           zoom_, offset_x_, offset_y_);
            }
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    front_ = frame;
    ++frames_rendered_;
}

QImage CubismAvatarRenderer::current_frame() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return front_;
}

bool CubismAvatarRenderer::initialize(const Live2dModelInfo& model) {
    if (!model.usable()) return false;

    // GL 上下文必须在调用线程创建（约定为 UI 线程）
    if (!ensure_gl()) return false;
    if (!context_->makeCurrent(surface_.get())) return false;

    if (!build_model(model)) {
        context_->doneCurrent();
        return false;
    }

    QOpenGLFramebufferObjectFormat fmt;
    fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    fmt.setSamples(0);
    fbo_ = std::make_unique<QOpenGLFramebufferObject>(width_, height_, fmt);
    if (!fbo_->isValid()) {
        std::fprintf(stderr, "[cubism] FBO 创建失败\n");
        release_gl();
        context_->doneCurrent();
        return false;
    }

    context_->doneCurrent();
    ready_ = true;
    return true;
}

void CubismAvatarRenderer::apply_params(const lb::core::SemanticParams& sem) {
    // 动作幅度：放大/缩小头颈与躯干的转动量。模型自带的参数范围有限，
    // 摄像头算出的角度偏小时画面里几乎看不出动作，用这个倍率补偿。
    lb::core::SemanticParams s = sem;
    if (motion_gain_ != 1.0f) {
        s.head.yaw *= motion_gain_;
        s.head.pitch *= motion_gain_;
        s.head.roll *= motion_gain_;
        s.body.torso_tilt *= motion_gain_;
        s.body.torso_sway *= motion_gain_;
        s.body.neck_pitch *= motion_gain_;
        s.body.neck_yaw *= motion_gain_;
        s.body.shoulder_shift *= motion_gain_;
    }
    // 表情幅度。摄像头算出的表情系数普遍偏小：自然闭眼时 eyeBlink 只到
    // 0.7~0.9、说话时 jawOpen 通常只有 0.3~0.5，直接映射就会"眼睛闭不严、
    // 张嘴不明显、要瞪大眼才满格"。
    //
    // 关键：增益要作用在"变化量"上而不是原值上。对眼睛开合度直接乘增益
    // 会让眼睛更睁大（方向正好相反），所以这里放大的是"闭合量" (1-open)。
    // 视线不参与：它是"看哪里"，放大后容易一直贴着边缘。
    if (face_gain_ != 1.0f) {
        const float g = face_gain_;
        const auto amp01 = [g](float v) { return std::clamp(v * g, 0.0f, 1.0f); };
        const auto amp11 = [g](float v) { return std::clamp(v * g, -1.0f, 1.0f); };
        s.eye.open_left = 1.0f - amp01(1.0f - s.eye.open_left);
        s.eye.open_right = 1.0f - amp01(1.0f - s.eye.open_right);
        s.eye.brow_left = amp11(s.eye.brow_left);
        s.eye.brow_right = amp11(s.eye.brow_right);
        s.mouth.open = amp01(s.mouth.open);
        s.mouth.smile = amp11(s.mouth.smile);
        s.mouth.press = amp01(s.mouth.press);
    }

    core::CubismParamValue values[core::kCubismParamMax];
    const size_t n = core::map_semantics_to_cubism(s, values, core::kCubismParamMax);
    {
        // 头部角度发生变化时记录一次：用来判断低头抬头的幅度是否够
        // （量程是 ±30°，若自然点头只跑到十几度，说明标定系数还要收紧）
        static float ly = 0.f, lp = 0.f, lr = 0.f;
        static int h_ = 0;
        if (h_ < 200 && (std::abs(s.head.yaw - ly) > 3.0f || std::abs(s.head.pitch - lp) > 3.0f ||
                         std::abs(s.head.roll - lr) > 3.0f)) {
            ly = s.head.yaw;
            lp = s.head.pitch;
            lr = s.head.roll;
            ++h_;
            cubism_log("[头部] 偏航=%.1f 俯仰=%.1f 翻滚=%.1f (度)\n", ly, lp, lr);
        }
    }
    {
        // 表情值发生变化时记录一次：用来判断左右眼是否都跟得上
        // （只记变化，静止时不刷屏）
        static float lo = -1.f, ro = -1.f, lx = 0.f, ly = 0.f, mo = -1.f;
        static int n_ = 0;
        if (n_ < 300 &&
            (std::abs(s.eye.open_left - lo) > 0.05f || std::abs(s.eye.open_right - ro) > 0.05f ||
             std::abs(s.eye.look_x - lx) > 0.15f || std::abs(s.eye.look_y - ly) > 0.15f ||
             std::abs(s.mouth.open - mo) > 0.05f)) {
            lo = s.eye.open_left;
            ro = s.eye.open_right;
            lx = s.eye.look_x;
            ly = s.eye.look_y;
            mo = s.mouth.open;
            ++n_;
            cubism_log("[表情] 左眼开=%.2f 右眼开=%.2f 视线=(%.2f,%.2f) 张嘴=%.2f\n", lo, ro, lx, ly,
                       mo);
        }
    }
    {
        // NaN/Inf 会让模型矩阵变成 NaN，模型被画到画外 —— 表现就是 FBO 全透明
        static int p_ = 0;
        if (p_++ < 4) {
            size_t bad = 0;
            for (size_t i = 0; i < n; ++i) {
                const float v = values[i].value;
                if (!(v >= -1.0e9f && v <= 1.0e9f)) ++bad;
            }
            cubism_log("[cubism] 参数 %d 个，异常值(NaN/Inf)=%d，face=%d\n", static_cast<int>(n),
                       static_cast<int>(bad), sem.face_detected ? 1 : 0);
        }
    }
    std::vector<core::CubismParamValue> params(values, values + n);
    runtime_->apply_params(params);
}

void CubismAvatarRenderer::render_frame() {
    fbo_->bind();
    const GLenum e1 = glGetError();
    glViewport(0, 0, width_, height_);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    const GLenum e2 = glGetError();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const GLenum e3 = glGetError();

    runtime_->draw_frame(width_, height_, zoom_, offset_x_, offset_y_);
    const GLenum e4 = glGetError();
    fbo_->release();
    {
        // 逐段定位 GL 错误：1282=INVALID_OPERATION / 1280=INVALID_ENUM
        static int q = 0;
        if (q++ < 3) {
            cubism_log("[gl] bind=%u clear=%u blend=%u draw=%u fbo=%d\n", e1, e2, e3, e4,
                       fbo_->isValid() ? 1 : 0);
        }
    }
}

void CubismAvatarRenderer::tick(const lb::core::SemanticParams& sem) {
    { static int n_ = 0; if (n_++ < 4) cubism_log("[tick] ready=%d`n", ready_ ? 1 : 0); }
    if (!ready_) return;
    if (!context_->makeCurrent(surface_.get())) return;

    apply_params(sem);
    if (runtime_) runtime_->apply_expression(expression_dt());
    if (runtime_->model()) runtime_->model()->Update();
    render_frame();

    const QImage frame = fbo_->toImage().convertToFormat(QImage::Format_ARGB32);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        front_ = frame;
        { static int m_ = 0; if (m_++ < 4) { long op = 0; for (int y = 0; y < frame.height(); ++y) { const QRgb* ln = reinterpret_cast<const QRgb*>(frame.constScanLine(y)); for (int x = 0; x < frame.width(); ++x) if (qAlpha(ln[x]) > 8) ++op; } cubism_log("[tick] front=%dx%d 非透明=%ld (%.1f%%)\n", frame.width(), frame.height(), op, 100.0 * op / (double(frame.width()) * frame.height())); } }
    }
    context_->doneCurrent();
}

void CubismAvatarRenderer::render(QImage& target, const lb::core::SemanticParams&) {
    std::lock_guard<std::mutex> lock(mutex_);
    { static int k_ = 0; if (k_++ < 4) cubism_log("[render] front_null=%d target=%dx%d\n", front_.isNull() ? 1 : 0, target.width(), target.height()); }
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

// ---- 表情 ----

float CubismAvatarRenderer::expression_dt() {
    const auto now = std::chrono::steady_clock::now();
    double dt = 1.0 / 60.0;
    if (last_expression_ts_.time_since_epoch().count() != 0) {
        dt = std::chrono::duration<double>(now - last_expression_ts_).count();
    }
    last_expression_ts_ = now;
    return static_cast<float>(std::clamp(dt, 0.001, 0.2));
}

void CubismAvatarRenderer::set_expression(int index) {
    if (runtime_) runtime_->set_expression(index);
    cubism_log("[表情] 切换到 %d\n", index);
}

int CubismAvatarRenderer::current_expression() const {
    return runtime_ ? runtime_->current_expression() : -1;
}

std::vector<QString> CubismAvatarRenderer::expression_names() const {
    std::vector<QString> out;
    if (runtime_) {
        const int n = runtime_->expression_count();
        out.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            out.push_back(QString::fromStdString(runtime_->expression_name(i)));
        }
    }
    return out;
}

bool CubismAvatarRenderer::model_bound() const { return ready_; }

void CubismAvatarRenderer::set_render_size(int w, int h) {
    if (w > 0 && h > 0) {
        width_ = w;
        height_ = h;
    }
}

void CubismAvatarRenderer::set_zoom(float z) { zoom_ = z; }

void CubismAvatarRenderer::set_offset(float x, float y) {
    offset_x_ = x;
    offset_y_ = y;
}

// 取景：UI 的取景控件走这个入口（基类 AvatarRenderer 的虚函数）
void CubismAvatarRenderer::set_view(float zoom, float offset_x, float offset_y) {
    zoom_ = zoom;
    offset_x_ = offset_x;
    offset_y_ = offset_y;
    // 带上已渲染帧数：若拖动滑块时帧数不增长，说明是渲染没有重跑（不是取景值没送到）
    cubism_log("[view] zoom=%.2f off=(%.2f,%.2f) 已渲染=%ld\n", zoom_, offset_x_, offset_y_,
               frames_rendered_);
}

void CubismAvatarRenderer::set_motion_gain(float g) {
    motion_gain_ = (g > 0.0f) ? g : 1.0f;
    cubism_log("[view] 动作幅度=%.2f\n", motion_gain_);
}

void CubismAvatarRenderer::set_face_gain(float g) {
    face_gain_ = (g > 0.0f) ? g : 1.0f;
    cubism_log("[view] 表情幅度=%.2f\n", face_gain_);
}

} // namespace lb::ui
