#include "settings_page.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "widgets/toggle_switch.h"

namespace lb::ui {

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("设置"), this);
    title->setObjectName("PageTitle");
    root->addWidget(title);

    // ---- 通用 ----
    auto* general_box = new QGroupBox(QStringLiteral("通用"), this);
    auto* general_layout = new QFormLayout(general_box);
    general_layout->setContentsMargins(10, 14, 10, 10);
    general_layout->setHorizontalSpacing(10);
    general_layout->setVerticalSpacing(8);

    auto* performance = new QComboBox(general_box);
    performance->addItems({ QStringLiteral("性能"),
                            QStringLiteral("均衡"),
                            QStringLiteral("画质") });
    performance->setCurrentIndex(1);
    general_layout->addRow(QStringLiteral("性能档位"), performance);

    auto* blur_switch = new ToggleSwitch(general_box);
    general_layout->addRow(QStringLiteral("背景虚化"), blur_switch);

    auto* log_level = new QComboBox(general_box);
    log_level->addItems({ QStringLiteral("调试"), QStringLiteral("信息"),
                          QStringLiteral("警告"), QStringLiteral("错误") });
    general_layout->addRow(QStringLiteral("日志级别"), log_level);
    root->addWidget(general_box);

    // ---- 模型目录 ----
    auto* model_box = new QGroupBox(QStringLiteral("模型"), this);
    auto* model_layout = new QHBoxLayout(model_box);
    model_layout->setContentsMargins(10, 14, 10, 10);
    model_layout->setSpacing(8);

    model_dir_edit_ = new QLineEdit(model_box);
    model_dir_edit_->setPlaceholderText(QStringLiteral("默认模型目录"));
    auto* browse_btn = new QPushButton(QStringLiteral("浏览"), model_box);
    connect(browse_btn, &QPushButton::clicked, this, &SettingsPage::browse_model_dir);
    model_layout->addWidget(model_dir_edit_, 1);
    model_layout->addWidget(browse_btn);
    root->addWidget(model_box);

    // ---- 预设 ----
    auto* preset_box = new QGroupBox(QStringLiteral("预设"), this);
    auto* preset_layout = new QHBoxLayout(preset_box);
    preset_layout->setContentsMargins(10, 14, 10, 10);
    preset_layout->setSpacing(8);

    preset_combo_ = new QComboBox(preset_box);
    preset_combo_->addItem(QStringLiteral("默认"));
    auto* save_btn = new QPushButton(QStringLiteral("保存预设"), preset_box);
    connect(save_btn, &QPushButton::clicked, this, &SettingsPage::save_preset);
    preset_layout->addWidget(preset_combo_, 1);
    preset_layout->addWidget(save_btn);
    root->addWidget(preset_box);

    auto* hint = new QLabel(
        QStringLiteral("配置持久化将在配置模块接入后生效"), this);
    hint->setObjectName("HintText");
    root->addWidget(hint);
    root->addStretch(1);
}

void SettingsPage::browse_model_dir() {
    const QString dir =
        QFileDialog::getExistingDirectory(this, QStringLiteral("选择模型目录"));
    if (!dir.isEmpty()) model_dir_edit_->setText(dir);
}

void SettingsPage::save_preset() {
    // TODO(M3): 将当前配置序列化并写入预设列表（方案 3.6 预设管理）。
}

} // namespace lb::ui