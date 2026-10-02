#include "float_window.h"

#include "render/avatar_painter.h"
#include "render/avatar_renderer.h"

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

    apply_mouse_transparency();
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
    apply_mouse_transparency();
    sync_toolbar();
    emit click_through_changed(enabled);
}

void FloatWindowWidget::set_drag_mode(bool on) {
    if (drag_mode_ == on) return;
    drag_mode_ = on;
    apply_mouse_transparency();
    emit drag_mode_changed(on);
}

void FloatWindowWidget::apply_mouse_transparency() {
    // 拖动模式优先：穿透开着也要能拖，否则浮窗一旦设成穿透就再也移不动
    setAttribute(Qt::WA_TransparentForMouseEvents, click_through_ && !drag_mode_);
}

void FloatWindowWidget::set_scale(double scale) {
    scale_ = std::clamp(scale, 0.3, 3.0);
    apply_size();
}

void FloatWindowWidget::set_overall_scale(double scale) {
    overall_scale_ = std::clamp(scale, 0.5, 2.5);
    apply_size();
}

void FloatWindowWidget::apply_size() {
    const QPoint center = geometry().center();
    const int w = static_cast<int>(kBaseWidth * scale_ * overall_scale_);
    const int h = static_cast<int>(kBaseHeight * scale_ * overall_scale_);
    resize(QSize(std::max(w, 80), std::max(h, 80)));
    move(center - QPoint(width() / 2, height() / 2));
    update();
}

void FloatWindowWidget::set_opacity(double opacity) {
    setWindowOpacity(std::clamp(opacity, 0.1, 1.0));
}

void FloatWindowWidget::set_avatar_renderer(AvatarRenderer* r) {
    avatar_renderer_ = r;
    update();
}

void FloatWindowWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    lb::core::SemanticParams sem;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sem = semantic_;
    }

    QPainter p(this);

    // 形象绘制走渲染器（与虚拟摄像头合成共用实现）；未设置时回退内置占位绘制
    if (avatar_renderer_) {
        if (canvas_.size() != size() || canvas_.format() != QImage::Format_ARGB32) {
            canvas_ = QImage(size(), QImage::Format_ARGB32);
        }
        canvas_.fill(Qt::transparent);
        avatar_renderer_->render(canvas_, sem);
        if (overall_scale_ == 1.0) {
            p.drawImage(0, 0, canvas_);
        } else {
            // 整体缩放：以窗口中心为锚点放大/缩小画面，超出部分由窗口裁掉。
            // 这里不碰模型的取景参数，只改显示大小。
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.save();
            p.translate(width() * 0.5, height() * 0.5);
            p.scale(overall_scale_, overall_scale_);
            p.translate(-width() * 0.5, -height() * 0.5);
            p.drawImage(0, 0, canvas_);
            p.restore();
        }
    } else {
        p.setRenderHint(QPainter::Antialiasing, true);
        paint_avatar(p, width(), height(), sem);
    }
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
    settings.setValue(QStringLiteral("float/overall_scale"), overall_scale_);
}

void FloatWindowWidget::load_geometry() {
    QSettings settings(QStringLiteral("YunoLab"), QStringLiteral("livebridge"));
    const QByteArray geo = settings.value(QStringLiteral("float/geometry")).toByteArray();
    if (!geo.isEmpty()) restoreGeometry(geo);
    scale_ = settings.value(QStringLiteral("float/scale"), 1.0).toDouble();
    overall_scale_ = settings.value(QStringLiteral("float/overall_scale"), 1.0).toDouble();
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

    // 激活态样式统一处理，避免每个按钮各写一遍
    const auto set_active = [](QPushButton* b, bool on) {
        b->setChecked(on);
        b->setStyleSheet(
            on ? QStringLiteral("QPushButton { background: transparent; border: none;"
                                " color: #4DB8FF; font-size: 11px; padding: 0 6px; }"
                                "QPushButton:hover { color: #E8EAED; }")
               : QStringLiteral("QPushButton { background: transparent; border: none;"
                                " color: #9BA1A8; font-size: 11px; padding: 0 6px; }"
                                "QPushButton:hover { color: #E8EAED; }"));
    };

    connect(drag_btn, &QPushButton::clicked, this,
            [this]() { host_->set_drag_mode(!host_->drag_mode()); });
    connect(through_btn, &QPushButton::clicked, this,
            [this]() { host_->set_click_through(!host_->click_through()); });
    connect(close_btn, &QPushButton::clicked, this, [this]() { host_->hide(); });

    host_->connect(host_, &FloatWindowWidget::click_through_changed, this,
                   [set_active, through_btn](bool on) { set_active(through_btn, on); });
    host_->connect(host_, &FloatWindowWidget::drag_mode_changed, this,
                   [set_active, drag_btn](bool on) { set_active(drag_btn, on); });
    through_btn->setCheckable(true);
    drag_btn->setCheckable(true);
    reposition();
}

void FloatToolbar::reposition() {
    if (!host_ || !host_->isVisible()) return;
    const QPoint top_left = host_->frameGeometry().topLeft();
    move(top_left + QPoint(12, 4));
    raise();
}

} // namespace lb::ui