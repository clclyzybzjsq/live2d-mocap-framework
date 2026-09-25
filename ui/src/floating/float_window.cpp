#include "float_window.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace lb::ui {

namespace {
constexpr int kBaseWidth = 320;
constexpr int kBaseHeight = 420;
constexpr int kToolbarHeight = 30;

QColor mix(QColor a, QColor b, double t) {
    return QColor(static_cast<int>(a.red() + (b.red() - a.red()) * t),
                  static_cast<int>(a.green() + (b.green() - a.green()) * t),
                  static_cast<int>(a.blue() + (b.blue() - a.blue()) * t));
}
} // namespace

// ---------------- FloatWindowWidget ----------------

FloatWindowWidget::FloatWindowWidget(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle(QStringLiteral("浮窗"));

    toolbar_ = new FloatToolbar(this);
    toolbar_->hide();

    load_geometry();
}

FloatWindowWidget::~FloatWindowWidget() = default;

QSize FloatWindowWidget::sizeHint() const {
    return QSize(kBaseWidth, kBaseHeight);
}

void FloatWindowWidget::set_frame(const FrameSnapshot& snap) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        semantic_ = snap.semantic;
        stats_ = snap.stats;
    }
    update();
}

void FloatWindowWidget::set_click_through(bool enabled) {
    click_through_ = enabled;
    setAttribute(Qt::WA_TransparentForMouseEvents, enabled);
    sync_toolbar();
    emit click_through_changed(enabled);
}

void FloatWindowWidget::set_scale(double scale) {
    scale_ = std::clamp(scale, 0.3, 3.0);
    const QPoint center = geometry().center();
    resize(QSize(static_cast<int>(kBaseWidth * scale_),
                 static_cast<int>(kBaseHeight * scale_)));
    move(center - QPoint(width() / 2, height() / 2));
    update();
}

void FloatWindowWidget::set_opacity(double opacity) {
    setWindowOpacity(std::clamp(opacity, 0.1, 1.0));
}

void FloatWindowWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    lb::core::SemanticParams sem;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sem = semantic_;
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int w = width();
    const int h = height();
    const double u = static_cast<double>(std::min(w, h)) / 420.0; // 单位长度

    // 头部旋转：roll 直接旋转，yaw/pitch 用剪切近似
    const double roll_deg = sem.head.roll;
    const double yaw_n = std::clamp(sem.head.yaw / 3.14159265 * 180.0 / 18.0, -1.0, 1.0);
    const double pitch_n = std::clamp(sem.head.pitch / 18.0, -1.0, 1.0);

    const QPointF center(w * 0.5, h * 0.42);
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
            p.drawLine(QPointF(x - eye_w * 0.7, eye_dy),
                       QPointF(x + eye_w * 0.7, eye_dy));
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
    p.end();

    // 调试信息（左下角：分辨率帧率）
    if (stats_.infer_fps > 0) {
        p.begin(this);
        p.setPen(QColor(107, 114, 128));
        QFont f = p.font();
        f.setPointSize(7);
        p.setFont(f);
        p.drawText(QPointF(4, h - 6),
            QStringLiteral("%1 FPS  %2 ms")
                .arg(stats_.infer_fps, 0, 'f', 0)
                .arg(stats_.latency_ms, 0, 'f', 0));
        p.end();
    }
}

void FloatWindowWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && draggable_) {
        dragging_ = true;
        drag_offset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
    }
    QWidget::mousePressEvent(event);
}

void FloatWindowWidget::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_) {
        move(event->globalPosition().toPoint() - drag_offset_);
    }
    QWidget::mouseMoveEvent(event);
}

void FloatWindowWidget::mouseReleaseEvent(QMouseEvent* event) {
    dragging_ = false;
    QWidget::mouseReleaseEvent(event);
}

void FloatWindowWidget::wheelEvent(QWheelEvent* event) {
    const double delta = event->angleDelta().y() > 0 ? 1.1 : 0.9;
    set_scale(scale_ * delta);
    event->accept();
}

void FloatWindowWidget::moveEvent(QMoveEvent* event) {
    QWidget::moveEvent(event);
    sync_toolbar();
}

void FloatWindowWidget::hideEvent(QHideEvent* event) {
    save_geometry();
    if (toolbar_) toolbar_->hide();
    QWidget::hideEvent(event);
}

void FloatWindowWidget::sync_toolbar() {
    if (!toolbar_) return;
    if (isVisible()) {
        toolbar_->reposition();
        toolbar_->show();
        toolbar_->raise();
    } else {
        toolbar_->hide();
    }
}

void FloatWindowWidget::save_geometry() {
    QSettings settings(QStringLiteral("YunoLab"), QStringLiteral("livebridge"));
    settings.setValue(QStringLiteral("float/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("float/scale"), scale_);
}

void FloatWindowWidget::load_geometry() {
    QSettings settings(QStringLiteral("YunoLab"), QStringLiteral("livebridge"));
    const QByteArray geo = settings.value(QStringLiteral("float/geometry")).toByteArray();
    if (!geo.isEmpty()) restoreGeometry(geo);
    scale_ = settings.value(QStringLiteral("float/scale"), 1.0).toDouble();
}

// ---------------- FloatToolbar ----------------

FloatToolbar::FloatToolbar(FloatWindowWidget* host, QWidget* parent)
    : QWidget(parent), host_(host) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedHeight(kToolbarHeight);

    auto* panel = new QFrame(this);
    panel->setObjectName("ToolbarPanel");
    panel->setStyleSheet(
        QStringLiteral("#ToolbarPanel { background-color: #14171C;"
                       " border: 1px solid #232830; border-radius: 2px; }"));
    auto* layout = new QHBoxLayout(panel);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(2);

    auto* drag_btn = new QPushButton(QStringLiteral("拖动"), panel);
    auto* through_btn = new QPushButton(QStringLiteral("穿透"), panel);
    auto* close_btn = new QPushButton(QStringLiteral("关闭"), panel);
    for (auto* b : { drag_btn, through_btn, close_btn }) {
        b->setFixedHeight(20);
        b->setStyleSheet(QStringLiteral(
            "QPushButton { background: transparent; border: none; color: #9BA1A8;"
            " font-size: 11px; padding: 0 6px; }"
            "QPushButton:hover { color: #E8EAED; }"));
        layout->addWidget(b);
    }

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(panel);

    connect(drag_btn, &QPushButton::clicked, this, [this]() {
        // 由 host 拖动处理；此处仅占位（M1 默认即可拖动）。
    });
    connect(through_btn, &QPushButton::clicked, this, [this]() {
        host_->set_click_through(!host_->click_through());
    });
    connect(close_btn, &QPushButton::clicked, this, [this]() { host_->hide(); });

    host_->connect(host_, &FloatWindowWidget::click_through_changed, this,
                   [through_btn](bool on) {
                       through_btn->setChecked(on);
                       through_btn->setStyleSheet(on
                           ? QStringLiteral("QPushButton { background: transparent;"
                                            " border: none; color: #4DB8FF;"
                                            " font-size: 11px; padding: 0 6px; }")
                           : QStringLiteral("QPushButton { background: transparent;"
                                            " border: none; color: #9BA1A8;"
                                            " font-size: 11px; padding: 0 6px; }"));
                   });
    through_btn->setCheckable(true);
    reposition();
}

void FloatToolbar::reposition() {
    if (!host_ || !host_->isVisible()) return;
    const QPoint top_left = host_->frameGeometry().topLeft();
    move(top_left + QPoint(12, 4));
    raise();
}

} // namespace lb::ui