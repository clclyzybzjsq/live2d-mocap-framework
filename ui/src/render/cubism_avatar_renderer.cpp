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

#include <cstdio>
#include <cstring>

#include "CubismFramework.hpp"
#include "CubismModelSettingJson.hpp"
#include "Id/CubismIdManager.hpp"
#include "Math/CubismMatrix44.hpp"
#include "Math/CubismModelMatrix.hpp"
#include "Model/CubismModel.hpp"
#include "Model/CubismUserModel.hpp"
#include "Rendering/OpenGL/CubismOffscreenManager_OpenGLES2.hpp"
#include "Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp"

#include "cubism_runtime.h"

using namespace Live2D::Cubism::Framework;

namespace lb::ui {

// ---- 模型运行时：加载 moc3/纹理 + 应用参数 + 绘制 ----
class CubismModelRuntime final : public CubismUserModel {
public:
    bool load(const std::string& model_json, int w, int h) {
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

        CreateRenderer(static_cast<csmUint32>(w), static_cast<csmUint32>(h));

        for (csmInt32 i = 0; i < setting.GetTextureCount(); ++i) {
            const csmChar* name = setting.GetTextureFileName(i);
            if (!name || name[0] == '\0') continue;
            const unsigned int tex = upload_texture(dir_ + name);
            if (tex != 0) {
                GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->BindTexture(i, tex);
                textures_.push_back(tex);
            }
        }
        GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->IsPremultipliedAlpha(false);

        setting.GetLayoutMap(layout_);
        GetModelMatrix()->SetupFromLayout(layout_);

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

        Rendering::CubismOffscreenManager_OpenGLES2::GetInstance()->BeginFrameProcess();

        // 每帧从布局重建模型矩阵：SetWidth/Translate 都是把变换乘到当前矩阵上，
        // 若直接连续调用，偏移会逐帧累加导致模型迅速移出画面
        GetModelMatrix()->SetupFromLayout(layout_);
        GetModelMatrix()->SetWidth(2.0f * zoom);
        if (offset_x != 0.0f || offset_y != 0.0f) {
            GetModelMatrix()->Translate(offset_x, offset_y);
        }

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
        projection.MultiplyByMatrix(GetModelMatrix());

        GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->SetMvpMatrix(&projection);
        GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->DrawModel();

        Rendering::CubismOffscreenManager_OpenGLES2::GetInstance()->EndFrameProcess();
        Rendering::CubismOffscreenManager_OpenGLES2::GetInstance()->ReleaseStaleRenderTextures();
    }

    Csm::CubismModel* model() { return GetModel(); }

    void release_textures() {
        for (unsigned int t : textures_) glDeleteTextures(1, &t);
        textures_.clear();
    }

private:
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
    csmMap<csmString, csmFloat32> layout_; // 布局信息：每帧据此重建模型矩阵
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
    if (runtime_->model()) runtime_->model()->Update();
    render_frame();
    const QImage frame = fbo_->toImage().convertToFormat(QImage::Format_ARGB32);
    {
        static int dbg = 0;
        if (dbg++ < 3) {
            long op = 0;
            for (int y = 0; y < frame.height(); ++y) {
                const QRgb* ln = reinterpret_cast<const QRgb*>(frame.constScanLine(y));
                for (int x = 0; x < frame.width(); ++x) {
                    if (qAlpha(ln[x]) > 8) ++op;
                }
            }
            cubism_log("[cubism] render_once 非透明=%ld (%.1f%%)\n", op,
                       100.0 * op / (static_cast<double>(frame.width()) * frame.height()));
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    front_ = frame;
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
    core::CubismParamValue values[core::kCubismParamMax];
    const size_t n = core::map_semantics_to_cubism(sem, values, core::kCubismParamMax);
    std::vector<core::CubismParamValue> params(values, values + n);
    runtime_->apply_params(params);
}

void CubismAvatarRenderer::render_frame() {
    fbo_->bind();
    glViewport(0, 0, width_, height_);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    runtime_->draw_frame(width_, height_, zoom_, offset_x_, offset_y_);
    fbo_->release();
}

void CubismAvatarRenderer::tick(const lb::core::SemanticParams& sem) {
    { static int n_ = 0; if (n_++ < 4) cubism_log("[tick] ready=%d`n", ready_ ? 1 : 0); }
    if (!ready_) return;
    if (!context_->makeCurrent(surface_.get())) return;

    apply_params(sem);
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
    { static int k_ = 0; if (k_++ < 4) cubism_log("[render] front_null=%d target=%dx%d`n", front_.isNull() ? 1 : 0, target.width(), target.height()); }
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

void CubismAvatarRenderer::set_zoom(float z) { zoom_ = z; }

void CubismAvatarRenderer::set_offset(float x, float y) {
    offset_x_ = x;
    offset_y_ = y;
}

} // namespace lb::ui
