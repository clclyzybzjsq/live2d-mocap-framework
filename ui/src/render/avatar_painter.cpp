#include "avatar_painter.h"

#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace lb::ui {

void paint_avatar(QPainter& p, int w, int h, const lb::core::SemanticParams& sem) {
    const double u = static_cast<double>(std::min(w, h)) / 420.0; // 单位长度

    // 头部旋转：roll 直接旋转，yaw/pitch 用剪切近似
    const double roll_deg = sem.head.roll;
    const double yaw_n = std::clamp(sem.head.yaw / 3.14159265 * 180.0 / 18.0, -1.0, 1.0);
    const double pitch_n = std::clamp(sem.head.pitch / 18.0, -1.0, 1.0);

    const QPointF center(w * 0.5, h * 0.42);
    p.save();
    p.translate(center);
    p.rotate(roll_deg);
    p.shear(yaw_n * 0.18, pitch_n * 0.12);

    // 身体（肩部弧线，随躯干摆动）
    const double sway = std::clamp(static_cast<double>(sem.body.torso_sway), -1.0, 1.0);
    const double tilt = std::clamp(static_cast<double>(sem.body.torso_tilt), -1.0, 1.0);
    p.save();
    p.translate(sway * 26.0 * u, tilt * 14.0 * u);
    QPainterPath body;
    body.moveTo(-120 * u, 60 * u);
    body.cubicTo(-120 * u, 150 * u, 120 * u, 150 * u, 120 * u, 60 * u);
    p.fillPath(body, QColor(qRgb(64, 64, 72)));
    p.restore();

    // 头
    const double r = 96.0 * u;
    const QRectF head(-r, -r, 2 * r, 2 * r);
    p.setPen(QPen(QColor(40, 42, 48), std::max(2.0, 2.0 * u)));
    p.setBrush(QColor(120, 116, 118));
    p.drawEllipse(head);

    // 眼
    const double eye_dx = r * 0.42;
    const double eye_dy = -r * 0.18;
    const double eye_w = r * 0.38;
    const double open_l = std::clamp(static_cast<double>(sem.eye.open_left), 0.0, 1.0);
    const double open_r = std::clamp(static_cast<double>(sem.eye.open_right), 0.0, 1.0);
    const double look = std::clamp(static_cast<double>(sem.eye.look_x), -1.0, 1.0) * r * 0.10;
    const double brow_l = std::clamp(static_cast<double>(sem.eye.brow_left), -1.0, 1.0) * r * 0.10;
    const double brow_r = std::clamp(static_cast<double>(sem.eye.brow_right), -1.0, 1.0) * r * 0.10;

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(30, 30, 34));
    const auto draw_eye = [&](double x, double open) {
        if (open < 0.15) {
            p.drawLine(QPointF(x - eye_w * 0.7, eye_dy), QPointF(x + eye_w * 0.7, eye_dy));
        } else {
            p.drawEllipse(QRectF(x - eye_w, eye_dy - eye_w * open * 0.9,
                                 2 * eye_w, 2 * eye_w * open * 0.9));
        }
    };
    draw_eye(-eye_dx + look, open_l);
    draw_eye(eye_dx + look, open_r);

    // 眉
    p.setPen(QPen(QColor(30, 30, 34), std::max(2.0, 3.0 * u)));
    p.drawLine(QPointF(-eye_dx - eye_w * 0.8, eye_dy - r * 0.34 - brow_l),
               QPointF(-eye_dx + eye_w * 0.8, eye_dy - r * 0.30 - brow_l));
    p.drawLine(QPointF(eye_dx - eye_w * 0.8, eye_dy - r * 0.30 - brow_r),
               QPointF(eye_dx + eye_w * 0.8, eye_dy - r * 0.34 - brow_r));

    // 嘴
    const double mouth_cy = r * 0.42;
    const double open = std::clamp(static_cast<double>(sem.mouth.open), 0.0, 1.0);
    const double smile = std::clamp(static_cast<double>(sem.mouth.smile), 0.0, 1.0);
    const double mouth_w = r * 0.42 * (0.75 + 0.35 * smile);
    p.setPen(QPen(QColor(40, 38, 40), std::max(2.0, 2.5 * u)));
    if (open < 0.08) {
        QPainterPath mouth;
        mouth.moveTo(-mouth_w, mouth_cy);
        mouth.cubicTo(-mouth_w * 0.4, mouth_cy - smile * r * 0.08,
                      mouth_w * 0.4, mouth_cy - smile * r * 0.08,
                      mouth_w, mouth_cy);
        p.drawPath(mouth);
    } else {
        p.drawEllipse(QRectF(-mouth_w, mouth_cy, 2 * mouth_w, r * 0.22 * open));
    }
    p.restore();
}

} // namespace lb::ui
