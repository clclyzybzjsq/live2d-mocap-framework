#pragma once

#include <QWidget>

class QLineEdit;
class QComboBox;

namespace lb::ui {

class ToggleSwitch;

// 设置页：性能档位、背景虚化、模型目录、预设管理、日志级别。
class SettingsPage final : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget* parent = nullptr);

private:
    void browse_model_dir();
    void save_preset();

    QLineEdit* model_dir_edit_ = nullptr;
    QComboBox* preset_combo_ = nullptr;
};

} // namespace lb::ui