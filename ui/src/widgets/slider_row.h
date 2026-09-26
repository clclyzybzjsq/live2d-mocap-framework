#pragma once

#include <QWidget>

class QLabel;
class QSlider;

namespace lb::ui {

// 参数行：名称 + 滑杆 + 数值 + 重置。数值以等宽字体显示。
class SliderRow final : public QWidget {
    Q_OBJECT
public:
    SliderRow(const QString& title, double min, double max, double value,
              QWidget* parent = nullptr);

    double value() const;
    void set_value(double v);

signals:
    void value_changed(double value);

private:
    QLabel* value_label_ = nullptr;
    QSlider* slider_ = nullptr;
    double min_ = 0.0;
};

} // namespace lb::ui