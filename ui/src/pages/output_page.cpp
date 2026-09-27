#include "output_page.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "widgets/slider_row.h"
#include "widgets/toggle_switch.h"

namespace lb::ui {

namespace {
// 默认取景预设：VTuber 直播常见的头胸像（实测标定值）。
// zoom 是倍率（SetWidth 的基准，模型画布宽 1.0，故模型在取景空间里的高度为 1.5*zoom）；
// offset 以「半个模型尺寸」为单位，±1 即可覆盖模型从顶到底的全部范围。
constexpr double kDefaultViewZoom = 3.0;
constexpr double kDefaultViewOffsetX = 0.0;
constexpr double kDefaultViewOffsetY = -0.5;
} // namespace

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

    float_switch_ = new ToggleSwitch(float_box);
    connect(float_switch_, &ToggleSwitch::clicked, this,
            &OutputPage::floating_window_toggled);
    float_layout->addRow(QStringLiteral("显示"), float_switch_);

    auto* through_switch = new ToggleSwitch(float_box);
    connect(through_switch, &ToggleSwitch::clicked, this,
            &OutputPage::floating_window_click_through);
    float_layout->addRow(QStringLiteral("点击穿透"), through_switch);

    // 注意：这一项控制的是浮窗自身的窗口大小，与下面"画面取景"的缩放无关，
    // 因此标签直接写成"窗口大小"，避免和取景缩放混淆
    auto* scale = new SliderRow(QStringLiteral("窗口大小"), 0.2, 1.5, 1.0, float_box);
    connect(scale, &SliderRow::value_changed, this,
            &OutputPage::floating_window_scale);
    float_layout->addRow(QStringLiteral("浮窗"), scale);

    // 整体缩放：窗口与窗内画面一起放大，模型显示变大但取景裁剪不变
    auto* overall = new SliderRow(QStringLiteral("整体缩放"), 0.5, 2.5, 1.0, float_box);
    connect(overall, &SliderRow::value_changed, this,
            &OutputPage::floating_window_overall_scale);
    float_layout->addRow(QStringLiteral("浮窗"), overall);

    auto* opacity = new SliderRow(QStringLiteral("透明度"), 0.1, 1.0, 1.0, float_box);
    connect(opacity, &SliderRow::value_changed, this,
            &OutputPage::floating_window_opacity);
    float_layout->addRow(QStringLiteral("浮窗"), opacity);

    auto* hint = new QLabel(
        QStringLiteral("浮窗位置与大小在退出时自动记忆"), float_box);
    hint->setObjectName("HintText");
    float_layout->addRow(QString(), hint);
    root->addWidget(float_box);

    // ---- 画面取景：同时作用于浮窗与虚拟摄像头（二者共用渲染器） ----
    auto* view_box = new QGroupBox(QStringLiteral("画面取景"), this);
    auto* view_layout = new QFormLayout(view_box);
    view_layout->setContentsMargins(10, 14, 10, 10);
    view_layout->setHorizontalSpacing(10);
    view_layout->setVerticalSpacing(8);

    // 取景缩放用对数刻度：范围跨两个数量级（0.2 全身 ~ 40 特写），
    // 线性刻度下小倍率会几乎拖不动
    view_zoom_ = new SliderRow(QStringLiteral("缩放"), 0.2, 40.0, kDefaultViewZoom, view_box, true);
    view_layout->addRow(QStringLiteral("取景"), view_zoom_);
    // 平移以"半个模型尺寸"为单位：±1 即可覆盖模型从顶到底的全部范围，±2 留余量。
    // 必须在任何缩放倍率下都够用，所以不能是固定的取景空间单位。
    view_ox_ = new SliderRow(QStringLiteral("水平"), -2.0, 2.0, kDefaultViewOffsetX, view_box);
    view_layout->addRow(QStringLiteral("位置"), view_ox_);
    view_oy_ = new SliderRow(QStringLiteral("垂直"), -2.0, 2.0, kDefaultViewOffsetY, view_box);
    view_layout->addRow(QString(), view_oy_);

    auto* view_hint = new QLabel(
        QStringLiteral("缩放：对数刻度，0.2 全身 ~ 40 特写；位置：±1 等于移动半个模型尺寸"), view_box);
    view_hint->setObjectName("HintText");
    view_layout->addRow(QString(), view_hint);

    const auto emit_view = [this]() {
        emit view_changed(view_zoom_->value(), view_ox_->value(), view_oy_->value());
    };
    connect(view_zoom_, &SliderRow::value_changed, this, [emit_view](double) { emit_view(); });
    connect(view_ox_, &SliderRow::value_changed, this, [emit_view](double) { emit_view(); });
    connect(view_oy_, &SliderRow::value_changed, this, [emit_view](double) { emit_view(); });
    root->addWidget(view_box);

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

void OutputPage::set_float_checked(bool on) {
    if (float_switch_) float_switch_->setChecked(on);
}

double OutputPage::view_zoom() const { return view_zoom_ ? view_zoom_->value() : kDefaultViewZoom; }

double OutputPage::view_offset_x() const {
    return view_ox_ ? view_ox_->value() : kDefaultViewOffsetX;
}

double OutputPage::view_offset_y() const {
    return view_oy_ ? view_oy_->value() : kDefaultViewOffsetY;
}

void OutputPage::set_view(double zoom, double offset_x, double offset_y) {
    if (view_zoom_) {
        const QSignalBlocker b1(view_zoom_);
        const QSignalBlocker b2(view_ox_);
        const QSignalBlocker b3(view_oy_);
        view_zoom_->set_value(zoom);
        view_ox_->set_value(offset_x);
        view_oy_->set_value(offset_y);
    }
}

void OutputPage::set_vcam_status(const QString& text) {
    vcam_status_->setText(text);
}

} // namespace lb::ui