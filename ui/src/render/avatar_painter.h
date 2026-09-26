#pragma once

#include <QPainter>

#include "lb/core/semantic_params.h"

namespace lb::ui {

// 绘制 M1 占位 Live2D 形象（由语义参数驱动：头部朝向、躯体摆动、眼、眉、嘴）。
// 桌面浮窗与虚拟摄像头合成共用同一实现，保证两处画面一致。
// 画布为 w x h，人物水平居中、垂直偏上（头部中心约在 0.42h 处）。
void paint_avatar(QPainter& p, int w, int h, const lb::core::SemanticParams& sem);

} // namespace lb::ui
