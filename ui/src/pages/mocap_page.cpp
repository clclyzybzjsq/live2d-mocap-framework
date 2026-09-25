#include "mocap_page.h"

#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include "lb/core/factory.h"

#include "widgets/slider_row.h"
#include "widgets/toggle_switch.h"

namespace lb::ui {

MocapPage::MocapPage(QWidget* parent) : QWidget(parent) {
    capture_ = lb::core::create_capture_source();

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("动捕"), this);
    title->setObjectName("PageTitle");
    root->addWidget(title);

    auto* form = new QWidget(this);
    auto* form_layout = new QFormLayout(form);
    form_layout->setContentsMargins(0, 4, 0, 4);
    form_layout->setHorizontalSpacing(10);
    form_layout->setVerticalSpacing(8);
    form_layout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // 摄像头
    auto* camera_row = new QHBoxLayout();
    camera_combo_ = new QComboBox(form);
    camera_combo_->setMinimumWidth(260);
    auto* refresh_btn = new QPushButton(QStringLiteral("刷新"), form);
    connect(refresh_btn, &QPushButton::clicked, this, &MocapPage::refresh_devices);
    camera_row->addWidget(camera_combo_, 1);
    camera_row->addWidget(refresh_btn);
    form_layout->addRow(QStringLiteral("摄像头"), camera_row);

    // 分辨率与帧率
    resolution_combo_ = new QComboBox(form);
    resolution_combo_->addItems({ QStringLiteral("1280 x 720"),
                                  QStringLiteral("1920 x 1080"),
                                  QStringLiteral("640 x 480") });
    form_layout->addRow(QStringLiteral("分辨率"), resolution_combo_);

    fps_combo_ = new QComboBox(form);
    fps_combo_->addItems({ QStringLiteral("30"), QStringLiteral("60"),
                           QStringLiteral("25") });
    form_layout->addRow(QStringLiteral("帧率"), fps_combo_);

    // 镜像
    mirror_switch_ = new ToggleSwitch(form);
    form_layout->addRow(QStringLiteral("镜像"), mirror_switch_);

    // 灵敏度
    auto* sensitivity = new SliderRow(QStringLiteral("灵敏度"), 0.0, 1.0, 0.5, form);
    form_layout->addRow(QStringLiteral("参数"), sensitivity);

    // 置信度
    confidence_bar_ = new QProgressBar(form);
    confidence_bar_->setRange(0, 100);
    confidence_bar_->setValue(0);
    form_layout->addRow(QStringLiteral("置信度"), confidence_bar_);
    root->addWidget(form);

    // 按钮
    auto* buttons = new QHBoxLayout();
    auto* apply_btn = new QPushButton(QStringLiteral("应用"), this);
    auto* test_btn = new QPushButton(QStringLiteral("测试预览"), this);
    connect(apply_btn, &QPushButton::clicked, this, &MocapPage::apply_config);
    connect(test_btn, &QPushButton::clicked, this, &MocapPage::open_preview);
    buttons->addWidget(apply_btn);
    buttons->addWidget(test_btn);
    buttons->addStretch(1);
    root->addLayout(buttons);

    status_label_ = new QLabel(this);
    status_label_->setObjectName("HintText");
    root->addWidget(status_label_);
    root->addStretch(1);

    refresh_devices();
}

void MocapPage::refresh_devices() {
    camera_combo_->clear();
    const auto devices = capture_->enumerate_devices();
    if (devices.empty()) {
        camera_combo_->addItem(QStringLiteral("未检测到摄像头"));
        camera_combo_->setEnabled(false);
        return;
    }
    camera_combo_->setEnabled(true);
    for (const auto& device : devices) {
        camera_combo_->addItem(QString::fromStdString(device.name),
                               QString::fromStdString(device.id));
    }
    if (devices.front().is_default) camera_combo_->setCurrentIndex(0);
}

void MocapPage::apply_config() {
    lb::core::CaptureConfig config;
    const int idx = resolution_combo_->currentIndex();
    if (idx == 0)      { config.width = 1280; config.height = 720; }
    else if (idx == 1) { config.width = 1920; config.height = 1080; }
    else               { config.width = 640;  config.height = 480; }
    config.fps = fps_combo_->currentText().toInt();
    config.mirror = mirror_switch_->isChecked();
    // TODO(M1): 传入采集层并持久化到配置。
    status_label_->setText(
        QStringLiteral("已应用: %1x%2 @ %3 FPS")
            .arg(config.width).arg(config.height).arg(config.fps));
}

void MocapPage::open_preview() {
    auto* dialog = new QDialog(this);
    dialog->setWindowTitle(QStringLiteral("摄像头预览"));
    dialog->setFixedSize(640, 400);
    dialog->setStyleSheet(QStringLiteral("background-color: #0F1115;"));
    auto* layout = new QVBoxLayout(dialog);
    auto* label = new QLabel(QStringLiteral("预览未就绪"), dialog);
    label->setObjectName("HintText");
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);
    // TODO(M1): 接入采集流后在此显示实时画面。
    dialog->exec();
    dialog->deleteLater();
}

} // namespace lb::ui