#include "slider_row.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>

#include <algorithm>
#include <cmath>

namespace lb::ui {

namespace {
// 滑杆内部统一用 0..kSteps 的整数位置，再按线性/对数映射到实际数值。
// 这样换刻度方式不需要改 UI，也能保证两种刻度下拖动分辨率一致。
constexpr int kSteps = 1000;

QString format_value(double v) {
    if (std::abs(v) < 10.0) return QString::number(v, 'f', 2);
    if (std::abs(v) < 100.0) return QString::number(v, 'f', 1);
    return QString::number(v, 'f', 0);
}
} // namespace

SliderRow::SliderRow(const QString& title, double min, double max, double value,
                     QWidget* parent, bool logarithmic)
    : QWidget(parent), min_(min), max_(max), default_(value), logarithmic_(logarithmic) {
    if (max_ <= min_) max_ = min_ + 1.0;
    // 对数刻度要求区间全为正数；不满足时退回线性，避免出现 NaN
    if (logarithmic_ && (min_ <= 0.0 || max_ <= 0.0)) logarithmic_ = false;

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 2, 0, 2);
    layout->setSpacing(8);

    auto* name = new QLabel(title, this);
    name->setFixedWidth(104);

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setRange(0, kSteps);
    slider_->setValue(to_pos(value));

    value_label_ = new QLabel(format_value(value), this);
    value_label_->setObjectName("SliderValue");
    value_label_->setFixedWidth(72);
    value_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* reset = new QPushButton(QStringLiteral("重置"), this);
    reset->setFixedSize(44, 24);

    layout->addWidget(name);
    layout->addWidget(slider_, 1);
    layout->addWidget(value_label_);
    layout->addWidget(reset);

    connect(slider_, &QSlider::valueChanged, this, [this](int pos) {
        const double dv = to_value(pos);
        value_label_->setText(format_value(dv));
        emit value_changed(dv);
    });
    connect(reset, &QPushButton::clicked, this, [this]() { set_value(default_); });
}

double SliderRow::to_value(int pos) const {
    const double t = std::clamp(pos, 0, kSteps) / static_cast<double>(kSteps);
    if (!logarithmic_) return min_ + (max_ - min_) * t;
    // 对数：value = min * (max/min)^t
    return min_ * std::pow(max_ / min_, t);
}

int SliderRow::to_pos(double v) const {
    const double clamped = std::clamp(v, min_, max_);
    double t = 0.0;
    if (logarithmic_) {
        t = std::log(clamped / min_) / std::log(max_ / min_);
    } else {
        t = (clamped - min_) / (max_ - min_);
    }
    return static_cast<int>(std::lround(t * kSteps));
}

double SliderRow::value() const { return to_value(slider_->value()); }

void SliderRow::set_value(double v) { slider_->setValue(to_pos(v)); }

} // namespace lb::ui
