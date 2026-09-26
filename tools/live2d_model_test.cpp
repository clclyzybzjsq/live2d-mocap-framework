// live2d_model_test —— 验证通用模型库与 Cubism 参数映射（不依赖 Cubism SDK）。
// 用法: lb_live2d_model_test [模型目录...]
//   给定目录时扫描其中的 .model3.json 并打印解析结果；
//   无参数时只做参数映射自检。
#include <QCoreApplication>
#include <QString>

#include <cmath>
#include <cstdio>

#include "lb/core/cubism_param_map.h"
#include "lb/core/semantic_params.h"
#include "render/live2d_model_library.h"

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "OK" : "FAIL", what);
    if (!ok) ++g_fail;
}

void test_param_map() {
    printf("== Cubism 参数映射 ==\n");
    lb::core::SemanticParams sem;
    sem.head.yaw = 45.f;    // 超出 ±30，应被 clamp
    sem.head.pitch = -12.f;
    sem.head.roll = 8.f;
    sem.eye.open_left = 0.25f;
    sem.eye.open_right = 1.5f; // 超范围，应被 clamp 到 1
    sem.eye.look_x = -0.6f;
    sem.eye.look_y = 0.3f;
    sem.eye.brow_left = -0.4f;
    sem.eye.brow_right = 0.9f;
    sem.mouth.open = 0.7f;
    sem.mouth.smile = 0.5f;
    sem.body.torso_sway = 20.f;  // 超出 ±10，应被 clamp
    sem.body.torso_tilt = -3.f;

    lb::core::CubismParamValue vals[lb::core::kCubismParamMax];
    const size_t n = lb::core::map_semantics_to_cubism(sem, vals, lb::core::kCubismParamMax);
    printf("  映射条目数: %zu\n", n);
    for (size_t i = 0; i < n; ++i) {
        printf("    %-16s = %7.2f\n", vals[i].id, vals[i].value);
    }

    auto find = [&](const char* id) -> float {
        for (size_t i = 0; i < n; ++i) {
            if (QString::fromLatin1(vals[i].id) == QString::fromLatin1(id)) return vals[i].value;
        }
        return NAN;
    };

    check(n == 13, "映射条目数为 13");
    check(std::fabs(find(lb::core::cubism_param::kAngleX) - 30.f) < 0.01f, "yaw 被 clamp 到 +30");
    check(std::fabs(find(lb::core::cubism_param::kAngleY) + 12.f) < 0.01f, "pitch 透传 -12");
    check(std::fabs(find(lb::core::cubism_param::kEyeROpen) - 1.f) < 0.01f, "eye open 超范围被 clamp 到 1");
    check(std::fabs(find(lb::core::cubism_param::kMouthOpenY) - 0.7f) < 0.01f, "mouth open 透传");
    check(std::fabs(find(lb::core::cubism_param::kBodyAngleX) - 10.f) < 0.01f, "torso_sway 被 clamp 到 +10");
    check(std::isnan(find("ParamNotExist")), "未映射的参数返回 NaN（存在性检查有效）");

    // 容量不足时必须安全返回 0 而不是越界写
    const size_t zero = lb::core::map_semantics_to_cubism(sem, nullptr, 0);
    check(zero == 0, "空缓冲区安全返回 0");
}

void test_model_library(const QString& root) {
    printf("== 模型库扫描 ==\n  目录: %s\n", qPrintable(root));
    const std::vector<lb::ui::Live2dModelInfo> models = lb::ui::Live2dModelLibrary::scan(root);
    printf("  发现可用模型: %zu\n", models.size());
    for (const auto& m : models) {
        printf("    - %-32s moc3=%s 纹理=%lld 动作=%lld 表情=%lld\n",
               qPrintable(m.name), m.moc3_exists ? "有" : "无",
               static_cast<long long>(m.textures.size()),
               static_cast<long long>(m.motions.size()),
               static_cast<long long>(m.expressions.size()));
    }
    if (models.empty()) {
        printf("  （该目录下没有可用的 .model3.json）\n");
        return;
    }
    const lb::ui::Live2dModelInfo& first = models.front();
    check(first.moc3_exists, "首个模型的 moc3 存在");
    check(!first.model_json.isEmpty(), "model_json 路径非空");
    check(!first.textures.isEmpty(), "纹理列表非空");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    setvbuf(stdout, nullptr, _IONBF, 0);

    test_param_map();

    // 用 Qt 的参数列表（Windows 下能正确处理中文路径，argv 是本地代码页）
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        test_model_library(args.at(i));
    }

    if (args.size() == 1) {
        printf("（未指定模型目录，仅做了参数映射自检）\n");
    }

    printf("\n%s\n", g_fail == 0 ? "RESULT OK" : "RESULT FAIL");
    return g_fail == 0 ? 0 : 1;
}
