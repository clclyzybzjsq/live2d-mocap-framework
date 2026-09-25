#pragma once

#include <QWidget>

class QLabel;
class QListWidget;
class QVBoxLayout;

namespace lb::ui {

// 模型页：左侧模型列表（导入模型目录），右侧分组参数面板。
// 参数面板当前编辑本地值，接入渲染引擎后驱动 Cubism 参数（TODO(M1)）。
class ModelsPage final : public QWidget {
    Q_OBJECT
public:
    explicit ModelsPage(QWidget* parent = nullptr);

signals:
    void model_changed(const QString& name);

private:
    void add_model_folder();

    QListWidget* model_list_ = nullptr;
};

} // namespace lb::ui