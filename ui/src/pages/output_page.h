#pragma once

#include <QWidget>

#include "lb/core/virtual_camera.h"

class QComboBox;
class QLabel;

namespace lb::ui {

class ToggleSwitch;

// 输出页：两个通道分区（方案 3.3 / 3.4 / 4.0）。
// 虚拟摄像头零配置：首次运行自动注册，此处无安装/配置操作，只有开关与参数。
class OutputPage final : public QWidget {
    Q_OBJECT
public:
    explicit OutputPage(QWidget* parent = nullptr);

signals:
    void floating_window_toggled(bool visible);
    void floating_window_click_through(bool enabled);
    void floating_window_scale(double scale);
    void floating_window_opacity(double opacity);

private:
    std::shared_ptr<lb::core::IVirtualCamera> vcam_;
    QLabel* vcam_status_ = nullptr;
};

} // namespace lb::ui