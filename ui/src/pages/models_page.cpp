#include "models_page.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "widgets/slider_row.h"

namespace lb::ui {

ModelsPage::ModelsPage(QWidget* parent) : QWidget(parent) {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(12);

    // 左侧：模型列表
    auto* left = new QWidget(this);
    auto* left_layout = new QVBoxLayout(left);
    left_layout->setContentsMargins(0, 0, 0, 0);
    left_layout->setSpacing(8);

    auto* title = new QLabel(QStringLiteral("模型"), left);
    title->setObjectName("PageTitle");
    left_layout->addWidget(title);

    model_list_ = new QListWidget(left);
    model_list_->setObjectName("ModelList");
    left_layout->addWidget(model_list_, 1);

    auto* import_btn = new QPushButton(QStringLiteral("导入模型"), left);
    connect(import_btn, &QPushButton::clicked, this, &ModelsPage::add_model_folder);
    left_layout->addWidget(import_btn);

    root->addWidget(left, 2);

    // 右侧：分组参数
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* host = new QWidget(scroll);
    auto* params_layout = new QVBoxLayout(host);
    params_layout->setContentsMargins(0, 0, 6, 0);
    params_layout->setSpacing(6);

    auto* ptitle = new QLabel(QStringLiteral("参数"), host);
    ptitle->setObjectName("PageTitle");
    params_layout->addWidget(ptitle);

    struct Group {
        QString title;
        QVector<QPair<QString, double>> rows;
    };
    const QVector<Group> groups = {
        { QStringLiteral("表情"),
          { { QStringLiteral("眨眼左"), 1.0 },
            { QStringLiteral("眨眼右"), 1.0 },
            { QStringLiteral("眉毛左"), 0.0 },
            { QStringLiteral("眉毛右"), 0.0 },
            { QStringLiteral("嘴开合"), 0.0 },
            { QStringLiteral("微笑"), 0.0 } } },
        { QStringLiteral("头部"),
          { { QStringLiteral("左右偏转"), 0.0 },
            { QStringLiteral("上下俯仰"), 0.0 },
            { QStringLiteral("倾斜"), 0.0 } } },
        { QStringLiteral("身体"),
          { { QStringLiteral("躯干前倾"), 0.0 },
            { QStringLiteral("躯干侧倾"), 0.0 },
            { QStringLiteral("颈部俯仰"), 0.0 },
            { QStringLiteral("颈部偏转"), 0.0 },
            { QStringLiteral("肩膀起伏"), 0.0 } } },
    };

    for (const auto& group : groups) {
        auto* box = new QGroupBox(group.title, host);
        auto* box_layout = new QVBoxLayout(box);
        box_layout->setContentsMargins(10, 14, 10, 8);
        box_layout->setSpacing(4);
        for (const auto& row : group.rows) {
            box_layout->addWidget(
                new SliderRow(row.first, 0.0, 1.0, row.second, box));
        }
        params_layout->addWidget(box);
    }

    auto* hint = new QLabel(
        QStringLiteral("参数面板将在渲染引擎接入后驱动模型"), host);
    hint->setObjectName("HintText");
    params_layout->addWidget(hint);
    params_layout->addStretch(1);

    scroll->setWidget(host);
    root->addWidget(scroll, 3);
}

void ModelsPage::add_model_folder() {
    const QString dir =
        QFileDialog::getExistingDirectory(this, QStringLiteral("选择 Live2D 模型目录"));
    if (dir.isEmpty()) return;
    const QFileInfo info(dir);
    const QString name = info.fileName().isEmpty() ? dir : info.fileName();
    model_list_->addItem(name);
    model_list_->setCurrentRow(model_list_->count() - 1);
    emit model_changed(name);
}

} // namespace lb::ui