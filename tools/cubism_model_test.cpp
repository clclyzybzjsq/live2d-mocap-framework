// cubism_model_test —— 离屏验证 Live2D Cubism 真实渲染（不依赖 UI/相机/设备）。
//
// 流程：Qt 离屏 GL 上下文 -> 加载 .model3.json + .moc3 + 纹理 -> 设置参数 ->
//       渲染到 FBO -> 读回像素 -> 校验（覆盖占比、颜色多样性、参数是否真的生效）。
//
// 用法: lb_cubism_model_test <model3.json> [输出png]
// 注意：glew.h 必须在任何 Qt/系统 GL 头之前包含（否则报
// "gl.h included before glew.h"，且会导致 GL 类型重复定义）
#include <GL/glew.h>

#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QSurfaceFormat>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "CubismFramework.hpp"
#include "Id/CubismIdManager.hpp"
#include "Math/CubismMatrix44.hpp"
#include "Math/CubismModelMatrix.hpp"
#include "Model/CubismModel.hpp"
#include "Model/CubismUserModel.hpp"
#include "Rendering/OpenGL/CubismOffscreenManager_OpenGLES2.hpp"
#include "Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp"
#include "CubismModelSettingJson.hpp"

using namespace Live2D::Cubism::Framework;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "OK" : "FAIL", what);
    if (!ok) ++g_fail;
}

// ---- Cubism 内存分配器 ----
class TestAllocator final : public ICubismAllocator {
public:
    void* Allocate(const csmSizeType size) override { return std::malloc(size); }
    void Deallocate(void* memory) override { std::free(memory); }
    void* AllocateAligned(const csmSizeType size, const csmUint32 alignment) override {
        return _aligned_malloc(size, alignment);
    }
    void DeallocateAligned(void* alignedMemory) override { _aligned_free(alignedMemory); }
};

// ---- Framework 文件加载回调（Cubism 全程通过它读 json/moc3） ----
// 注意 5-r.5 的回调签名：路径为 std::string、释放接 csmByte*
// （SDK 自己的 csmString 是另一个类型，取值用 GetRawString()）
csmByte* load_file_as_bytes(std::string path, csmSizeInt* size) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        printf("  load_file 失败: %s\n", path.c_str());
        if (size) *size = 0;
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) {
        fclose(f);
        if (size) *size = 0;
        return nullptr;
    }
    auto* buf = static_cast<csmByte*>(std::malloc(static_cast<size_t>(len)));
    const size_t got = fread(buf, 1, static_cast<size_t>(len), f);
    fclose(f);
    if (size) *size = static_cast<csmSizeInt>(got);
    return buf;
}

void release_bytes(csmByte* bytes) { std::free(bytes); }

void log_message(const csmChar* msg) {
    // Cubism 的日志较啰嗦，仅转发错误级信息
    if (msg && std::strstr(msg, "Error")) printf("  [cubism] %s\n", msg);
}

// ---- 纹理上传（Qt 读 PNG -> GL 纹理） ----
// 路径以本地 8bit（ANSICodePage）传入：命令行与 fopen 都按本地代码页处理，
// 而 QImage 需要正确的 QString（尤其含中文的路径）
GLuint upload_texture(const std::string& path) {
    QImage img(QString::fromLocal8Bit(path.c_str()));
    if (img.isNull()) {
        printf("  纹理读取失败: %s\n", path.c_str());
        return 0;
    }
    img = img.convertToFormat(QImage::Format_RGBA8888);
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());
    // 与官方 Sample 一致：生成 mipmap 并用 mipmap 过滤（否则纹理可能被判为不完整）
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) printf("  纹理上传 GL 错误: 0x%04X (%s)\n", err, path.c_str());
    return tex;
}

// ---- 模型运行时：加载 + 参数 + 绘制 ----
class ModelRuntime final : public CubismUserModel {
public:
    bool setup(const std::string& model_json, int w, int h) {
        const size_t slash = model_json.find_last_of("/\\");
        dir_ = (slash == std::string::npos) ? std::string() : model_json.substr(0, slash + 1);

        csmSizeInt json_size = 0;
        csmByte* json_buf = load_file_as_bytes(model_json.c_str(), &json_size);
        if (!json_buf) return false;
        CubismModelSettingJson setting(json_buf, json_size);

        // moc3
        csmSizeInt moc_size = 0;
        const std::string moc_path = dir_ + setting.GetModelFileName();
        csmByte* moc_buf = load_file_as_bytes(moc_path.c_str(), &moc_size);
        if (!moc_buf) {
            release_bytes(json_buf);
            return false;
        }
        LoadModel(moc_buf, moc_size);
        release_bytes(moc_buf);

        if (!GetModel()) {
            release_bytes(json_buf);
            return false;
        }

        // 渲染器（GL 必须已 current）
        CreateRenderer(w, h);

        // 纹理
        for (csmInt32 i = 0; i < setting.GetTextureCount(); ++i) {
            const csmChar* tex_name = setting.GetTextureFileName(i);
            if (!tex_name || tex_name[0] == '\0') continue;
            const GLuint tex = upload_texture(dir_ + tex_name);
            if (tex != 0) {
                GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->BindTexture(i, tex);
                textures_.push_back(tex);
                printf("  纹理[%d] id=%u 已绑定\n", i, tex);
            }
        }
        GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->IsPremultipliedAlpha(false);

        // 布局
        csmMap<csmString, csmFloat32> layout;
        setting.GetLayoutMap(layout);
        GetModelMatrix()->SetupFromLayout(layout);

        GetModel()->SaveParameters();
        release_bytes(json_buf);
        return true;
    }

    // 渲染到当前 FBO；width/height 为画布尺寸
    void draw(int width, int height) {
        if (!GetModel()) return;
        Rendering::CubismOffscreenManager_OpenGLES2::GetInstance()->BeginFrameProcess();

        CubismMatrix44 projection;
        projection.LoadIdentity();
        if (GetModel()->GetCanvasWidth() > 1.0f && width < height) {
            GetModelMatrix()->SetWidth(2.0f);
            projection.Scale(1.0f, static_cast<float>(width) / static_cast<float>(height));
        } else {
            projection.Scale(static_cast<float>(height) / static_cast<float>(width), 1.0f);
        }
        projection.MultiplyByMatrix(GetModelMatrix());

        GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->SetMvpMatrix(&projection);
        GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->DrawModel();

        Rendering::CubismOffscreenManager_OpenGLES2::GetInstance()->EndFrameProcess();
        Rendering::CubismOffscreenManager_OpenGLES2::GetInstance()->ReleaseStaleRenderTextures();
    }

    void apply_params() { GetModel()->SaveParameters(); }

    Csm::CubismModel* model() { return GetModel(); }

    void release_textures() {
        for (GLuint t : textures_) glDeleteTextures(1, &t);
        textures_.clear();
    }

private:
    std::string dir_;
    std::vector<GLuint> textures_;
};

// ---- 统计：alpha 覆盖率与颜色多样性 ----
struct Stats {
    double coverage = 0.0; // 非透明像素占比
    int distinct = 0;      // 粗略颜色种类（8 级量化）
};

Stats analyze(const QImage& img) {
    Stats s;
    long opaque = 0;
    bool seen[4096] = {false};
    int distinct = 0;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb p = line[x];
            if (qAlpha(p) > 8) ++opaque;
            const int idx = ((qRed(p) >> 4) << 8) | ((qGreen(p) >> 4) << 4) | (qBlue(p) >> 4);
            if (!seen[idx]) {
                seen[idx] = true;
                ++distinct;
            }
        }
    }
    const long total = static_cast<long>(img.width()) * img.height();
    s.coverage = total ? static_cast<double>(opaque) / total : 0.0;
    s.distinct = distinct;
    return s;
}

double image_diff(const QImage& a, const QImage& b) {
    if (a.size() != b.size()) return 1.0;
    long diff = 0;
    long n = 0;
    for (int y = 0; y < a.height(); ++y) {
        const QRgb* la = reinterpret_cast<const QRgb*>(a.constScanLine(y));
        const QRgb* lb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x) {
            diff += std::abs(qRed(la[x]) - qRed(lb[x]));
            diff += std::abs(qGreen(la[x]) - qGreen(lb[x]));
            diff += std::abs(qBlue(la[x]) - qBlue(lb[x]));
            n += 3;
        }
    }
    return n ? static_cast<double>(diff) / n : 0.0;
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    setvbuf(stdout, nullptr, _IONBF, 0);

    if (argc < 2) {
        printf("用法: lb_cubism_model_test <model3.json> [输出png]\n");
        return 1;
    }
    // 路径统一用本地 8bit（含中文路径时 argv 是本地代码页，QString 需相应转换）
    const std::string model_json = QGuiApplication::arguments().value(1).toLocal8Bit().toStdString();
    printf("cubism model test\n  模型: %s\n", model_json.c_str());

    const int W = 640, H = 480;

    // ---- 离屏 GL 上下文 ----
    QOpenGLContext ctx;
    ctx.setFormat(QSurfaceFormat::defaultFormat());
    if (!ctx.create()) {
        printf("FAIL: 创建 GL 上下文失败\n");
        return 1;
    }
    QOffscreenSurface surface;
    surface.setFormat(ctx.format());
    surface.create();
    if (!surface.isValid() || !ctx.makeCurrent(&surface)) {
        printf("FAIL: 离屏 surface/current 失败\n");
        return 1;
    }
    glewExperimental = GL_TRUE;
    const GLenum glew_err = glewInit();
    if (glew_err != GLEW_OK) {
        printf("FAIL: glewInit: %s\n", reinterpret_cast<const char*>(glewGetErrorString(glew_err)));
        return 1;
    }
    printf("  GL: %s | %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
           reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    // ---- Cubism 初始化 ----
    TestAllocator allocator;
    CubismFramework::Option option;
    option.LogFunction = log_message;
    option.LoggingLevel = CubismFramework::Option::LogLevel_Warning;
    option.LoadFileFunction = load_file_as_bytes;
    option.ReleaseBytesFunction = release_bytes;
    if (!CubismFramework::StartUp(&allocator, &option)) {
        printf("FAIL: StartUp\n");
        return 1;
    }
    CubismFramework::Initialize();

    // ---- 模型 ----
    ModelRuntime runtime;
    if (!runtime.setup(model_json, W, H)) {
        printf("FAIL: 模型加载失败\n");
        CubismFramework::Dispose();
        return 1;
    }
    Csm::CubismModel* model = runtime.model();
    printf("  模型参数数: %d, 绘制对象: %d, 画布: %.2f x %.2f\n",
           model->GetParameterCount(), model->GetDrawableCount(),
           model->GetCanvasWidth(), model->GetCanvasHeight());

    // ---- 渲染 ----
    QOpenGLFramebufferObjectFormat fmt;
    fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    fmt.setSamples(0);
    QOpenGLFramebufferObject fbo(W, H, fmt);
    if (!fbo.isValid()) {
        printf("FAIL: FBO 创建失败\n");
        return 1;
    }

    auto render_frame = [&](const char* param_id, float value) -> QImage {
        fbo.bind();
        glViewport(0, 0, W, H);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        if (param_id) {
            const CubismIdHandle id = CubismFramework::GetIdManager()->GetId(param_id);
            const csmInt32 index = model->GetParameterIndex(id);
            if (index >= 0) model->SetParameterValue(index, value, 1.0f);
        }
        model->Update();
        runtime.draw(W, H);
        fbo.release();
        return fbo.toImage().convertToFormat(QImage::Format_ARGB32);
    };

    const auto t0 = std::chrono::steady_clock::now();
    const QImage neutral = render_frame(nullptr, 0.0f);
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

    const Stats s = analyze(neutral);
    printf("  渲染耗时: %.1f ms (640x480)\n", ms);
    printf("  非透明像素占比: %.1f%%\n", s.coverage * 100.0);
    printf("  颜色种类(量化): %d\n", s.distinct);

    // 采样若干非透明像素的实际 RGBA，便于判断"全黑"这类问题
    {
        int printed = 0;
        for (int y = 0; y < neutral.height() && printed < 4; y += 7) {
            const QRgb* line = reinterpret_cast<const QRgb*>(neutral.constScanLine(y));
            for (int x = 0; x < neutral.width() && printed < 4; x += 7) {
                if (qAlpha(line[x]) > 128) {
                    printf("    采样(%d,%d) = R%d G%d B%d A%d\n", x, y, qRed(line[x]),
                           qGreen(line[x]), qBlue(line[x]), qAlpha(line[x]));
                    ++printed;
                }
            }
        }
        if (printed == 0) printf("    （没有 alpha>128 的像素）\n");
    }

    check(s.coverage > 0.05, "画面有实际内容（非透明像素 > 5%）");
    check(s.distinct > 20, "画面有细节（颜色种类 > 20）");

    // 参数是否真的驱动画面：同一模型在 ParamAngleX 两端渲染结果应明显不同
    const QImage left = render_frame("ParamAngleX", -30.0f);
    const QImage right = render_frame("ParamAngleX", 30.0f);
    const double diff = image_diff(left, right);
    printf("  ParamAngleX ±30 渲染差异: %.2f (0-255)\n", diff);
    check(diff > 0.5, "参数驱动画面变化");

    if (argc >= 3) {
        const QString out = QString::fromUtf8(argv[2]);
        neutral.save(out);
        printf("  已保存: %s\n", out.toUtf8().constData());
    }

    runtime.release_textures();
    runtime.DeleteRenderer();
    CubismFramework::Dispose();
    printf("\n%s\n", g_fail == 0 ? "RESULT OK" : "RESULT FAIL");
    return g_fail == 0 ? 0 : 1;
}
