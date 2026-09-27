#pragma once

#include <QWidget>

#include "lb/core/capture_source.h"

class QComboBox;
class QDialog;
class QLabel;
class QProgressBar;

namespace lb::ui {

class ToggleSwitch;
class SliderRow;

// 动捕页：摄像头选择、分辨率/帧率、镜像、灵敏度、置信度、预览。
class MocapPage final : public QWidget {
    Q_OBJECT
public:
    explicit MocapPage(QWidget* parent = nullptr);

    QString device_id() const;
    lb::core::CaptureConfig capture_config() const;
    void set_confidence(int percent);
    // 动作幅度（头颈/躯干转动量倍率，1.0 为原始幅度）
    double motion_gain() const;
    void set_motion_gain(double g);
    // 平滑强度（0 接近原样透传，越大越平滑）
    double smoothing() const;
    void set_smoothing(double s);
    // 表情幅度（眼睛开合/眉毛/嘴部表情倍率，1.0 为原始幅度）
    double face_gain() const;
    void set_face_gain(double g);
    // 镜像（采集端水平翻转）
    void set_mirror(bool enabled);

signals:
    void motion_gain_changed(double gain);
    void smoothing_changed(double smoothing);
    void face_gain_changed(double gain);
    void mirror_changed(bool enabled);
    // 请求按当前配置重启动捕（摄像头/分辨率/帧率只在启动时下发）
    void apply_requested();

private:
    void refresh_devices();
    void apply_config();
    void open_preview();

    std::shared_ptr<lb::core::ICaptureSource> capture_;
    QComboBox* camera_combo_ = nullptr;
    QComboBox* resolution_combo_ = nullptr;
    QComboBox* fps_combo_ = nullptr;
    ToggleSwitch* mirror_switch_ = nullptr;
    SliderRow* motion_gain_ = nullptr;
    SliderRow* smoothing_ = nullptr;
    SliderRow* face_gain_ = nullptr;
    QProgressBar* confidence_bar_ = nullptr;
    QLabel* status_label_ = nullptr;
};

} // namespace lb::ui