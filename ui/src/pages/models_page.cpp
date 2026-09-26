#include "models_page.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QVBoxLayout>

#include <cstdlib>

#include "widgets/slider_row.h"

namespace lb::ui {

namespace {
constexpr const char* kSettingsDir = "models/dir";
constexpr const char* kSettingsCurrent = "models/current";
} // namespace

ModelsPage::ModelsPage(QWidget* parent) : QWidget(parent) {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(12);

    // ---- 左侧：模型目录与列表 ----
    auto* left = new QWidget(this);
    auto* left_layout = new QVBoxLayout(left);
    left_layout->setContentsMargins(0, 0, 0, 0);
    left_layout->setSpacing(8);

    auto* title = new QLabel(QStringLiteral("模型"), left);
    title->setObjectName("PageTitle");
    left_layout->addWidget(title);

    dir_label_ = new QLabel(left);
    dir_label_->setObjectName("HintText");
    dir_label_->setWordWrap(true);
    left_layout->addWidget(dir_label_);

    model_list_ = new QListWidget(left);
    model_list_->setObjectName("ModelList");
    left_layout->addWidget(model_list_, 1);

    status_label_ = new QLabel(left);
    status_label_->setObjectName("HintText");
    status_label_->setWordWrap(true);
    left_layout->addWidget(status_label_);

    auto* btn_row = new QHBoxLayout();
    auto* choose_btn = new QPushButton(QStringLiteral("选择模型目录"), left);
    connect(choose_btn, &QPushButton::clicked, this, &ModelsPage::choose_folder);
    btn_row->addWidget(choose_btn);
    auto* rescan_btn = new QPushButton(QStringLiteral("重新扫描"), left);
    connect(rescan_btn, &QPushButton::clicked, this, &ModelsPage::rescan);
    btn_row->addWidget(rescan_btn);
    left_layout->addLayout(btn_row);

    root->addWidget(left, 2);

    // ---- 右侧：分组参数（接入渲染后由模型参数驱动） ----
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
            box_layout->addWidget(new SliderRow(row.first, 0.0, 1.0, row.second, box));
        }
        params_layout->addWidget(box);
    }

    auto* hint = new QLabel(
        QStringLiteral("参数面板由动捕实时驱动；此处为手动查看用"), host);
    hint->setObjectName("HintText");
    params_layout->addWidget(hint);
    params_layout->addStretch(1);

    scroll->setWidget(host);
    root->addWidget(scroll, 3);

    // ---- 恢复上次的目录与选择 ----
    QSettings settings;
    QString dir = settings.value(QLatin1String(kSettingsDir)).toString();
    if (dir.isEmpty()) {
        // 环境变量指定的默认目录
        const QByteArray env = qgetenv("LB_MODELS_DIR");
        if (!env.isEmpty()) dir = QString::fromLocal8Bit(env);
    }
    if (!dir.isEmpty() && QFileInfo::exists(dir)) {
        model_dir_ = dir;
        rescan();
    } else {
        dir_label_->setText(QStringLiteral("未选择模型目录"));
        status_label_->setText(
            QStringLiteral("请选择包含 Live2D 模型（.model3.json）的目录"));
    }

    connect(model_list_, &QListWidget::currentRowChanged, this, &ModelsPage::on_row_activated);
}

void ModelsPage::choose_folder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择 Live2D 模型目录"), model_dir_);
    if (dir.isEmpty()) return;
    model_dir_ = dir;
    QSettings().setValue(QLatin1String(kSettingsDir), dir);
    rescan();
}

void ModelsPage::rescan() {
    if (model_dir_.isEmpty()) return;
    dir_label_->setText(QStringLiteral("模型目录：%1").arg(model_dir_));
    models_ = Live2dModelLibrary::scan(model_dir_);

    const QSignalBlocker blocker(model_list_);
    model_list_->clear();
    for (const auto& m : models_) {
        QString label = m.name;
        if (!m.usable()) label += QStringLiteral("（缺少 moc3）");
        model_list_->addItem(label);
    }

    if (models_.empty()) {
        status_label_->setText(
            QStringLiteral("该目录下未找到可用模型（需要 .model3.json 与 .moc3）"));
        return;
    }

    // 恢复上次选中的模型（不重复发信号，由调用方决定）
    QSettings settings;
    const QString last = settings.value(QLatin1String(kSettingsCurrent)).toString();
    int row = -1;
    for (size_t i = 0; i < models_.size(); ++i) {
        if (models_[i].model_json == last) { row = static_cast<int>(i); break; }
    }
    status_label_->setText(QStringLiteral("发现 %1 个模型").arg(models_.size()));

    if (row < 0) {
        // 没有历史选择：自动选第一个可用模型并通知
        for (size_t i = 0; i < models_.size(); ++i) {
            if (models_[i].usable()) { row = static_cast<int>(i); break; }
        }
        if (row >= 0) {
            model_list_->setCurrentRow(row);
            apply_selection(models_[static_cast<size_t>(row)].model_json, true);
        }
        return;
    }
    model_list_->setCurrentRow(row);
    apply_selection(models_[static_cast<size_t>(row)].model_json, true);
}

void ModelsPage::on_row_activated(int row) {
    if (row < 0 || static_cast<size_t>(row) >= models_.size()) return;
    apply_selection(models_[static_cast<size_t>(row)].model_json, true);
}

void ModelsPage::apply_selection(const QString& model_json, bool emit_signal) {
    if (model_json.isEmpty() || model_json == selected_json_) return;
    selected_json_ = model_json;
    QSettings().setValue(QLatin1String(kSettingsCurrent), model_json);
    for (const auto& m : models_) {
        if (m.model_json == model_json) {
            emit model_changed(m.name);
            break;
        }
    }
    if (emit_signal) emit model_activated(model_json);
}

} // namespace lb::ui
