#include "lb/core/cubism_param_map.h"

#include <algorithm>

namespace lb::core {

namespace {
float clampf(float v, float lo, float hi) { return std::clamp(v, lo, hi); }
} // namespace

size_t map_semantics_to_cubism(const SemanticParams& sem, CubismParamValue* out, size_t cap) {
    if (!out || cap == 0) return 0;

    size_t n = 0;
    const auto put = [&](const char* id, float v) {
        if (n < cap) out[n++] = CubismParamValue{id, v};
    };

    // 头部朝向：语义参数已是角度
    put(cubism_param::kAngleX, clampf(sem.head.yaw, -30.f, 30.f));
    put(cubism_param::kAngleY, clampf(sem.head.pitch, -30.f, 30.f));
    put(cubism_param::kAngleZ, clampf(sem.head.roll, -30.f, 30.f));

    // 眼（开合、视线、眉）
    put(cubism_param::kEyeLOpen, clampf(sem.eye.open_left, 0.f, 1.f));
    put(cubism_param::kEyeROpen, clampf(sem.eye.open_right, 0.f, 1.f));
    put(cubism_param::kEyeBallX, clampf(sem.eye.look_x, -1.f, 1.f));
    put(cubism_param::kEyeBallY, clampf(sem.eye.look_y, -1.f, 1.f));
    put(cubism_param::kBrowLY, clampf(sem.eye.brow_left, -1.f, 1.f));
    put(cubism_param::kBrowRY, clampf(sem.eye.brow_right, -1.f, 1.f));

    // 口
    put(cubism_param::kMouthOpenY, clampf(sem.mouth.open, 0.f, 1.f));
    put(cubism_param::kMouthForm, clampf(sem.mouth.smile, -1.f, 1.f));

    // 身体。
    //   ParamBodyAngleZ = 屏幕平面内的左右摇摆（肩线倾斜）
    //   ParamBodyAngleY = 前后倾
    //   ParamBodyAngleX = 绕纵轴扭转 —— 不写
    //
    // 扭转必须留空：2D 摄像头算不出躯干真实的绕纵轴旋转，只能拿
    // "肩中点与鼻尖的水平差"之类的东西近似，而那个量在转头时会剧烈变化，
    // 结果是上半身跟着头一起甩，观感很怪。留空让它保持模型默认值。
    // （此前把摇摆与扭转写反了：摇摆给了 X、与头强耦合的量给了 Z，
    //   于是转头时上半身跟着甩。）
    put(cubism_param::kBodyAngleZ, clampf(sem.body.torso_sway * 10.f, -10.f, 10.f));
    // ParamBodyAngleY：本模型里它是躯干的上下平动（蹲下/站起），
    // 由肩中点的竖直位移驱动，而不是"前后倾"—— 前后倾是三维动作，
    // 单目画面里几乎测不出来，接上去等于没接。
    put(cubism_param::kBodyAngleY, clampf(sem.body.position_y * 10.f, -10.f, 10.f));

    return n;
}

} // namespace lb::core
