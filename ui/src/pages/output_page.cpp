#include "output_page.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "widgets/slider_row.h"
#include "widgets/toggle_switch.h"

namespace lb::ui {

OutputPage::OutputPage(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("输出"), this);
    title->setObjectName("PageTitle");
    root->addWidget(title);

    // ---- 通道 A：虚拟摄像头 ----
    auto* vcam_box = new QGroupBox(QStringLiteral("虚拟摄像头"), this);
    auto* vcam_layout = new QFormLayout(vcam_box);
    vcam_layout->setContentsMargins(10, 14, 10, 10);
    vcam_layout->setHorizontalSpacing(10);
    vcam_layout->setVerticalSpacing(8);

    vcam_switch_ = new ToggleSwitch(vcam_box);
    vcam_layout->addRow(QStringLiteral("启用"), vcam_switch_);

    vcam_resolution_ = new QComboBox(vcam_box);
    vcam_resolution_->addItems({ QStringLiteral("1920 x 1080"),
                                 QStringLiteral("1280 x 720") });
    vcam_resolution_->setCurrentIndex(1); // 默认 720p
    vcam_layout->addRow(QStringLiteral("分辨率"), vcam_resolution_);

    vcam_fps_ = new QComboBox(vcam_box);
    vcam_fps_->addItems({ QStringLiteral("30"), QStringLiteral("60") });
    vcam_layout->addRow(QStringLiteral("帧率"), vcam_fps_);

    auto* format_label = new QLabel(QStringLiteral("YUY2"), vcam_box);
    auto* format_row = new QHBoxLayout();
    format_row->addWidget(format_label);
    format_row->addStretch(1);
    vcam_layout->addRow(QStringLiteral("色彩格式"), format_row);

    vcam_status_ = new QLabel(vcam_box);
    vcam_status_->setObjectName("HintText");
    vcam_layout->addRow(QStringLiteral("设备状态"), vcam_status_);

    // 画面内容：默认 Live2D 形象 + 背景虚化；打开此开关则只输出背景（无 Live2D）
    vcam_bg_only_ = new ToggleSwitch(vcam_box);
    vcam_layout->addRow(QStringLiteral("仅输出背景"), vcam_bg_only_);
    auto* content_hint = new QLabel(
        QStringLiteral("默认输出 Live2D 形象 + 虚化背景；打开后只输出背景"), vcam_box);
    content_hint->setObjectName("HintText");
    vcam_layout->addRow(QString(), content_hint);

    connect(vcam_switch_, &ToggleSwitch::clicked, this,
            &OutputPage::virtual_camera_toggled);
    connect(vcam_bg_only_, &ToggleSwitch::clicked, this,
            &OutputPage::vcam_background_only_toggled);
    root->addWidget(vcam_box);

    // ---- 通道 B：桌面浮窗 ----
    auto* float_box = new QGroupBox(QStringLiteral("桌面浮窗"), this);
    auto* float_layout = new QFormLayout(float_box);
    float_layout->setContentsMargins(10, 14, 10, 10);
    float_layout->setHorizontalSpacing(10);
    float_layout->setVerticalSpacing(8);

    auto* show_switch = new ToggleSwitch(float_box);
    connect(show_switch, &ToggleSwitch::clicked, this,
            &OutputPage::floating_window_toggled);
    float_layout->addRow(QStringLiteral("显示"), show_switch);

    auto* through_switch = new ToggleSwitch(float_box);
    connect(through_switch, &ToggleSwitch::clicked, this,
            &OutputPage::floating_window_click_through);
    float_layout->addRow(QStringLiteral("点击穿透"), through_switch);

    auto* scale = new SliderRow(QStringLiteral("缩放"), 0.2, 1.5, 1.0, float_box);
    connect(scale, &SliderRow::value_changed, this,
            &OutputPage::floating_window_scale);
    float_layout->addRow(QStringLiteral("浮窗"), scale);

    auto* opacity = new SliderRow(QStringLiteral("透明度"), 0.1, 1.0, 1.0, float_box);
    connect(opacity, &SliderRow::value_changed, this,
            &OutputPage::floating_window_opacity);
    float_layout->addRow(QStringLiteral("浮窗"), opacity);

    auto* hint = new QLabel(
        QStringLiteral("浮窗位置与大小在退出时自动记忆"), float_box);
    hint->setObjectName("HintText");
    float_layout->addRow(QString(), hint);
    root->addWidget(float_box);

    root->addStretch(1);
}

lb::core::VirtualCameraFormat OutputPage::vcam_config() const {
    lb::core::VirtualCameraFormat fmt;
    fmt.width = vcam_resolution_->currentIndex() == 0 ? 1920 : 1280;
    fmt.height = vcam_resolution_->currentIndex() == 0 ? 1080 : 720;
    fmt.fps = vcam_fps_->currentIndex() == 0 ? 30 : 60;
    fmt.preferred_format = lb::core::PixelFormat::YUY2;
    return fmt;
}

bool OutputPage::vcam_background_only() const {
    return vcam_bg_only_ && vcam_bg_only_->isChecked();
}

void OutputPage::set_vcam_checked(bool on) {
    vcam_switch_->setChecked(on);
}

void OutputPage::set_vcam_status(const QString& text) {
    vcam_status_->setText(text);
}

} // namespace lb::ui