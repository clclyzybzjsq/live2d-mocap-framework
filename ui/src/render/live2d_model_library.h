#pragma once

#include <QString>
#include <QStringList>

#include <vector>

namespace lb::ui {

// 一个 Live2D 模型（由 .model3.json 描述）。
// 只做文件定位与引用解析，不加载 moc3（加载由渲染器负责，SDK 接入后消费本结构）。
struct Live2dModelInfo {
    QString name;             // 显示名：model3.json 的 Name，缺失时用目录名
    QString model_json;       // .model3.json 绝对路径
    QString dir;              // 模型所在目录
    QString moc3;             // .moc3 绝对路径
    QStringList textures;     // 纹理绝对路径
    QStringList physics;      // 物理 json 绝对路径
    QStringList motions;      // 动作 json 绝对路径
    QStringList expressions;  // 表情 json 绝对路径
    bool moc3_exists = false; // moc3 是否真实存在（决定模型是否可用）

    bool usable() const { return moc3_exists; }
};

// 模型库：扫描目录树收集 *.model3.json 并解析引用。
// 目录组织与 Live2D 官方导出结构一致（model3.json 与 moc3/纹理同目录或其子目录）。
class Live2dModelLibrary {
public:
    // 扫描 root 下（含子目录，深度上限 max_depth）全部模型，按名称排序。
    static std::vector<Live2dModelInfo> scan(const QString& root, int max_depth = 8);

    // 解析单个 .model3.json；失败时返回的 moc3_exists 为 false。
    static Live2dModelInfo parse(const QString& model_json_path);
};

} // namespace lb::ui
