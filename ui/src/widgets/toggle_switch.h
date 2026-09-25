#pragma once

#include <QAbstractButton>

namespace lb::ui {

// 扁平黑色拨动开关：开 = 强调色填充，关 = 控件底色。
// 全应用统一使用本控件表达开关状态。
class ToggleSwitch final : public QAbstractButton {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
};

} // namespace lb::ui