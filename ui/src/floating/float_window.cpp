#include "float_window.h"

#include "render/avatar_painter.h"

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
    // 形象绘制与虚拟摄像头合成共用同一实现（render/avatar_painter）
    paint_avatar(p, width(), height(), sem);
    p.end();

    // 调试信息（左下角：分辨率帧率）
    if (stats_.infer_fps > 0) {
        const int h = height();
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