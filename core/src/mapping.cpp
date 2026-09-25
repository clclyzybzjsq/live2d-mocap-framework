#include "lb/core/model_params.h"
#include "lb/core/semantic_params.h"

#include <algorithm>

namespace lb::core {

const MappingTable& default_mapping_table() {
    static const MappingTable table = {
        // 头部（Angles 单位为度）
        { "head.yaw",   "ParamAngleY",  30.f, 0.f, -30.f, 30.f, false },
        { "head.pitch", "ParamAngleX",  30.f, 0.f, -30.f, 30.f, false },
        { "head.roll",  "ParamAngleZ",  30.f, 0.f, -30.f, 30.f, false },
        // 眼睛（开合取反：系数 1 = 睁眼 -> Live2D 参数通常 1 = 闭眼）
        { "eye.open_left",  "ParamEyeLOpen", 1.f, 0.f, 0.f, 1.f, true  },
        { "eye.open_right", "ParamEyeROpen", 1.f, 0.f, 0.f, 1.f, true  },
        { "eye.brow_left",  "ParamBrowLY",   1.f, 0.f, 0.f, 1.f, false },
        { "eye.brow_right", "ParamBrowRY",   1.f, 0.f, 0.f, 1.f, false },
        // 嘴
        { "mouth.open",  "ParamMouthOpenY", 1.f, 0.f, 0.f, 1.f, false },
        { "mouth.smile", "ParamMouthForm",  1.f, 0.f, 0.f, 1.f, false },
        // 身体
        { "body.torso_tilt", "ParamBodyAngleX", 1.f, 0.f, -1.f, 1.f, false },
        { "body.torso_sway", "ParamBodyAngleZ", 1.f, 0.f, -1.f, 1.f, false },
        { "body.neck_pitch", "ParamNeckAngleX", 1.f, 0.f, -1.f, 1.f, false },
        { "body.neck_yaw",   "ParamNeckAngleY", 1.f, 0.f, -1.f, 1.f, false },
        { "body.shoulder_shift", "ParamShoulderY", 1.f, 0.f, -1.f, 1.f, false },
    };
    return table;
}

namespace {

float semantic_value(const SemanticParams& p, const std::string& name) {
    if (name == "head.yaw")   return p.head.yaw;
    if (name == "head.pitch") return p.head.pitch;
    if (name == "head.roll")  return p.head.roll;

    if (name == "eye.open_left")  return p.eye.open_left;
    if (name == "eye.open_right") return p.eye.open_right;
    if (name == "eye.look_x")     return p.eye.look_x;
    if (name == "eye.look_y")     return p.eye.look_y;
    if (name == "eye.brow_left")  return p.eye.brow_left;
    if (name == "eye.brow_right") return p.eye.brow_right;

    if (name == "mouth.open")  return p.mouth.open;
    if (name == "mouth.smile") return p.mouth.smile;
    if (name == "mouth.press") return p.mouth.press;

    if (name == "body.torso_tilt")    return p.body.torso_tilt;
    if (name == "body.torso_sway")    return p.body.torso_sway;
    if (name == "body.neck_pitch")    return p.body.neck_pitch;
    if (name == "body.neck_yaw")      return p.body.neck_yaw;
    if (name == "body.shoulder_shift") return p.body.shoulder_shift;
    return 0.f;
}

} // namespace

ModelParamList apply_mapping(const SemanticParams& params, const MappingTable& table) {
    ModelParamList out;
    out.reserve(table.size());
    for (const auto& e : table) {
        float v = semantic_value(params, e.semantic) * e.scale + e.offset;
        if (e.invert) v = -v;
        v = std::clamp(v, e.min, e.max);
        out.push_back(ModelParam{ e.model_param, v });
    }
    return out;
}

} // namespace lb::core