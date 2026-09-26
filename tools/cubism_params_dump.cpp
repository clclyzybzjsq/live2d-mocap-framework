// cubism_params_dump —— 打印模型全部参数名与取值范围（用于确认手臂/手部参数是否存在）。
// 用法: lb_cubism_params_dump <model3.json> [关键字过滤]
#include <GL/glew.h>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>

#include <cstdio>
#include <cstring>
#include <string>

#include "CubismFramework.hpp"
#include "CubismModelSettingJson.hpp"
#include "Model/CubismModel.hpp"
#include "Model/CubismUserModel.hpp"

#include "render/cubism_runtime.h"

using namespace Live2D::Cubism::Framework;

namespace {
class DumpModel final : public CubismUserModel {
public:
    bool load(const std::string& json_path) {
        const size_t slash = json_path.find_last_of("/\\");
        const std::string dir = (slash == std::string::npos) ? std::string() : json_path.substr(0, slash + 1);
        const auto json = lb::ui::CubismRuntime::read_file(json_path);
        if (!json.ok()) return false;
        CubismModelSettingJson setting(json.data.data(), static_cast<csmSizeInt>(json.data.size()));
        const auto moc = lb::ui::CubismRuntime::read_file(dir + setting.GetModelFileName());
        if (!moc.ok()) return false;
        LoadModel(const_cast<csmByte*>(moc.data.data()), static_cast<csmSizeInt>(moc.data.size()));
        return GetModel() != nullptr;
    }
};
} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    setvbuf(stdout, nullptr, _IONBF, 0);

    {
        QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
        fmt.setVersion(2, 1);
        fmt.setProfile(QSurfaceFormat::NoProfile);
        QSurfaceFormat::setDefaultFormat(fmt);
        QOpenGLContext ctx;
        ctx.setFormat(fmt);
        ctx.create();
        QOffscreenSurface surface;
        surface.setFormat(ctx.format());
        surface.create();
        ctx.makeCurrent(&surface);
        glewExperimental = GL_TRUE;
        glewInit();
        glGetError();
        ctx.doneCurrent();
    }

    if (!lb::ui::CubismRuntime::acquire()) { printf("FAIL: runtime\n"); return 1; }
    const QStringList args = QGuiApplication::arguments();
    if (args.size() < 2) { printf("用法: lb_cubism_params_dump <model3.json> [过滤关键字]\n"); return 1; }

    DumpModel model;
    if (!model.load(args.at(1).toLocal8Bit().toStdString())) { printf("FAIL: 模型加载失败\n"); return 1; }

    CubismModel* m = model.GetModel();
    const char* filter = (args.size() >= 3) ? args.at(2).toLocal8Bit().constData() : nullptr;
    printf("参数总数: %d\n", m->GetParameterCount());
    for (csmInt32 i = 0; i < m->GetParameterCount(); ++i) {
        const char* id = m->GetParameterId(i)->GetString().GetRawString();
        if (filter && !std::strstr(id, filter)) continue;
        printf("  %-28s min=%8.2f max=%8.2f default=%8.2f\n", id, m->GetParameterMinimumValue(i),
               m->GetParameterMaximumValue(i), m->GetParameterDefaultValue(i));
    }
    model.DeleteRenderer();
    lb::ui::CubismRuntime::release();
    return 0;
}
