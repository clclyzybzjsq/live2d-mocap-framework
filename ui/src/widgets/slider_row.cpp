#include "slider_row.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>

#include <cmath>

namespace lb::ui {

namespace {
constexpr int kPrecision = 1000;

QString format_value(double v) {
    if (std::abs(v) < 10.0) return QString::number(v, 'f', 2);
    return QString::number(v, 'f', 0);
}
} // namespace

SliderRow::SliderRow(const QString& title, double min, double max, double value,
                     QWidget* parent)
    : QWidget(parent), min_(min) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 2, 0, 2);
    layout->setSpacing(8);

    auto* name = new QLabel(title, this);
    name->setFixedWidth(104);

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setRange(static_cast<int>(min * kPrecision),
                      static_cast<int>(max * kPrecision));
    slider_->setValue(static_cast<int>(value * kPrecision));

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

    connect(slider_, &QSlider::valueChanged, this, [this](int v) {
        const double dv = v / static_cast<double>(kPrecision);
        value_label_->setText(format_value(dv));
        emit value_changed(dv);
    });
    connect(reset, &QPushButton::clicked, this, [this]() {
        slider_->setValue(static_cast<int>((min_ + 0.5) * kPrecision));
    });
}

double SliderRow::value() const {
    return slider_->value() / static_cast<double>(kPrecision);
}

void SliderRow::set_value(double v) {
    slider_->setValue(static_cast<int>(v * kPrecision));
}

} // namespace lb::ui