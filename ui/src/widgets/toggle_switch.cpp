#include "toggle_switch.h"

#include <QMouseEvent>
#include <QPainter>

namespace lb::ui {

ToggleSwitch::ToggleSwitch(QWidget* parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
}

QSize ToggleSwitch::sizeHint() const {
    return QSize(44, 22);
}

void ToggleSwitch::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);

    const QRectF track = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setBrush(isChecked() ? QColor("#4DB8FF") : QColor("#232830"));
    p.drawRoundedRect(track, 3.0, 3.0);

    const qreal margin = 3.0;
    const qreal knob = track.height() - 2.0 * margin;
    const qreal x = isChecked() ? track.right() - knob - margin : track.left() + margin;
    p.setBrush(isChecked() ? QColor("#E8EAED") : QColor("#9BA1A8"));
    p.drawRoundedRect(QRectF(x, track.top() + margin, knob, knob), 2.0, 2.0);
}

void ToggleSwitch::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
        setChecked(!isChecked());
        emit clicked(isChecked());
    }
}

} // namespace lb::ui