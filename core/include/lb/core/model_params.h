#pragma once

#include <string>
#include <vector>

namespace lb::core {

struct SemanticParams;

struct ModelParam {
    std::string name;
    float value = 0.f;
};
using ModelParamList = std::vector<ModelParam>;

// 一条映射：语义通道 -> Live2D 参数，带映射曲线（scale/offset/clamp）。
struct MappingEntry {
    std::string semantic;      // 如 "head.pitch"
    std::string model_param;   // 如 "ParamAngleX"
    float scale = 1.f;
    float offset = 0.f;
    float min = -1.f;
    float max = 1.f;
    bool  invert = false;
};
using MappingTable = std::vector<MappingEntry>;

// 常见 Cubism 3/4/5 模型的默认映射表（方案 3.1）。
const MappingTable& default_mapping_table();

ModelParamList apply_mapping(const SemanticParams& params, const MappingTable& table);

} // namespace lb::core