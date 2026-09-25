#pragma once

#include <QWidget>

#include "lb/core/capture_source.h"

class QComboBox;
class QDialog;
class QLabel;
class QProgressBar;

namespace lb::ui {

class ToggleSwitch;

// 动捕页：摄像头选择、分辨率/帧率、镜像、灵敏度、置信度、预览。
class MocapPage final : public QWidget {
    Q_OBJECT
public:
    explicit MocapPage(QWidget* parent = nullptr);

private:
    void refresh_devices();
    void apply_config();
    void open_preview();

    std::shared_ptr<lb::core::ICaptureSource> capture_;
    QComboBox* camera_combo_ = nullptr;
    QComboBox* resolution_combo_ = nullptr;
    QComboBox* fps_combo_ = nullptr;
    ToggleSwitch* mirror_switch_ = nullptr;
    QProgressBar* confidence_bar_ = nullptr;
    QLabel* status_label_ = nullptr;
};

} // namespace lb::ui