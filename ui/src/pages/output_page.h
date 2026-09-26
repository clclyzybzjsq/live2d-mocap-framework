#pragma once

#include <QWidget>

#include "lb/core/virtual_camera.h"

class QComboBox;
class QLabel;

namespace lb::ui {

class ToggleSwitch;

// 输出页：两个通道分区（方案 3.3 / 3.4 / 4.0）。
// 虚拟摄像头零配置：首次运行自动注册，此处无安装/配置操作，只有开关与参数。
// vcam 实例的所有权在 MainWindow（管线回调线程需喂帧）；本页只负责开关意图与状态展示。
class OutputPage final : public QWidget {
    Q_OBJECT
public:
    explicit OutputPage(QWidget* parent = nullptr);

    lb::core::VirtualCameraFormat vcam_config() const;
    // 画面内容：默认输出 Live2D 形象 + 虚化背景；该开关打开时只输出背景（无 Live2D）
    bool vcam_background_only() const;
    void set_vcam_checked(bool on);
    void set_vcam_status(const QString& text);

signals:
    void floating_window_toggled(bool visible);
    void floating_window_click_through(bool enabled);
    void floating_window_scale(double scale);
    void floating_window_opacity(double opacity);
    void virtual_camera_toggled(bool on);
    void vcam_background_only_toggled(bool on);

private:
    QComboBox* vcam_resolution_ = nullptr;
    QComboBox* vcam_fps_ = nullptr;
    ToggleSwitch* vcam_switch_ = nullptr;
    ToggleSwitch* vcam_bg_only_ = nullptr;
    QLabel* vcam_status_ = nullptr;
};

} // namespace lb::ui