#pragma once

#include <QString>
#include <QWidget>

#include <vector>

#include "render/live2d_model_library.h"

class QComboBox;
class QLabel;
class QListWidget;
class QVBoxLayout;

namespace lb::ui {

// 模型页：左侧模型列表（扫描模型目录得到），右侧分组参数面板。
// 模型目录与当前选择通过 QSettings 记忆；目录可用环境变量 LB_MODELS_DIR 指定默认值。
class ModelsPage final : public QWidget {
    Q_OBJECT
public:
    explicit ModelsPage(QWidget* parent = nullptr);

    // 重新扫描当前目录
    void rescan();
    QString model_dir() const { return model_dir_; }
    // 当前选中模型的 .model3.json 路径（未选中时为空）
    QString current_model_json() const { return selected_json_; }
    // 模型自带的表达式列表（exp_xxx），首项由页面自行补「无表情」
    void set_expressions(const QStringList& names);

signals:
    void model_changed(const QString& name);
    // 选中了可用模型（携带 .model3.json 绝对路径）
    void model_activated(const QString& model_json);
    // 表情切换（-1 表示无表情）
    void expression_changed(int index);

private:
    void choose_folder();
    void on_row_activated(int row);
    void apply_selection(const QString& model_json, bool emit_signal);

    QListWidget* model_list_ = nullptr;
    QLabel* dir_label_ = nullptr;
    QLabel* status_label_ = nullptr;
    QComboBox* expression_combo_ = nullptr;
    bool updating_expressions_ = false;
    QString model_dir_;
    QString selected_json_;
    std::vector<Live2dModelInfo> models_;
};

} // namespace lb::ui
