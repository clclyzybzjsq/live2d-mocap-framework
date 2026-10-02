#pragma once

#include <QWidget>

class QLabel;
class QSlider;

namespace lb::ui {

// 参数行：名称 + 滑杆 + 数值 + 重置。数值以等宽字体显示。
//
// logarithmic：滑杆位置按对数映射到数值。缩放这类跨数量级的参数必须用对数，
// 否则线性刻度下「细调大倍率」和「粗调小倍率」无法兼顾 —— 要么低端动一像素
// 就跳好几个单位，要么高端几乎拖不动。
class SliderRow final : public QWidget {
    Q_OBJECT
public:
    SliderRow(const QString& title, double min, double max, double value,
              QWidget* parent = nullptr, bool logarithmic = false);

    double value() const;
    void set_value(double v);

signals:
    void value_changed(double value);
    // 用户开始/结束拖动滑杆。竞争式仲裁用它区分"人在操作"和"程序在刷新"：
    // 只有拖动期间手动值优先，松手就交还给动捕。
    void drag_state_changed(bool dragging);

private:
    double to_value(int pos) const;
    int to_pos(double v) const;

    QLabel* value_label_ = nullptr;
    QSlider* slider_ = nullptr;
    double min_ = 0.0;
    double max_ = 1.0;
    double default_ = 0.0; // 「重置」回到构造时给的默认值（不是量程中点）
    bool logarithmic_ = false;
};

} // namespace lb::ui
