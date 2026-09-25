#include "output_page.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "lb/core/factory.h"

#include "widgets/slider_row.h"
#include "widgets/toggle_switch.h"

namespace lb::ui {

OutputPage::OutputPage(QWidget* parent) : QWidget(parent) {
    vcam_ = lb::core::create_virtual_camera();

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

    auto* vcam_switch = new ToggleSwitch(vcam_box);
    vcam_layout->addRow(QStringLiteral("启用"), vcam_switch);

    auto* resolution = new QComboBox(vcam_box);
    resolution->addItems({ QStringLiteral("1920 x 1080"),
                           QStringLiteral("1280 x 720") });
    vcam_layout->addRow(QStringLiteral("分辨率"), resolution);

    auto* fps = new QComboBox(vcam_box);
    fps->addItems({ QStringLiteral("30"), QStringLiteral("60") });
    vcam_layout->addRow(QStringLiteral("帧率"), fps);

    auto* format = new QComboBox(vcam_box);
    format->addItems({ QStringLiteral("YUY2"), QStringLiteral("NV12") });
    vcam_layout->addRow(QStringLiteral("色彩格式"), format);

    vcam_status_ = new QLabel(vcam_box);
    vcam_status_->setObjectName("HintText");
    vcam_layout->addRow(QStringLiteral("设备状态"), vcam_status_);

    connect(vcam_switch, &ToggleSwitch::clicked, this, [this](bool on) {
        if (!on) { vcam_->stop(); return; }
        lb::core::VirtualCameraFormat fmt;
        vcam_->start(fmt); // 桩实现；M2 接入后驱动
        vcam_status_->setText(vcam_->is_running()
            ? QStringLiteral("运行中")
            : QStringLiteral("未就绪"));
    });
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

    vcam_status_->setText(vcam_->is_registered()
        ? QStringLiteral("就绪")
        : QStringLiteral("未就绪"));
}

} // namespace lb::ui