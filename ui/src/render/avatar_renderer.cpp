#include "avatar_renderer.h"

#include <QPainter>

#include <cstdio>

#include "avatar_painter.h"
#ifdef LB_WITH_CUBISM
#include "cubism_avatar_renderer.h"
#endif

namespace lb::ui {

bool PlaceholderAvatarRenderer::initialize(const Live2dModelInfo& model) {
    (void)model; // 占位渲染与模型无关
    return true;
}

void PlaceholderAvatarRenderer::render(QImage& target, const lb::core::SemanticParams& sem) {
    QPainter p(&target);
    p.setRenderHint(QPainter::Antialiasing, true);
    paint_avatar(p, target.width(), target.height(), sem);
}

QString PlaceholderAvatarRenderer::name() const { return QStringLiteral("占位渲染"); }

bool PlaceholderAvatarRenderer::model_bound() const { return true; }

bool cubism_available() {
#ifdef LB_WITH_CUBISM
    return true;
#else
    return false;
#endif
}

std::unique_ptr<AvatarRenderer> create_avatar_renderer(const Live2dModelInfo& model) {
                 cubism_available() ? 1 : 0, model.usable() ? 1 : 0,
                 model.moc3.toLocal8Bit().constData());
#ifdef LB_WITH_CUBISM
    if (model.usable()) {
        auto cubism = std::make_unique<CubismAvatarRenderer>();
        if (cubism->initialize(model)) {
            return cubism;
        }
    }
#else
    (void)model;
#endif
    auto fallback = std::make_unique<PlaceholderAvatarRenderer>();
    fallback->initialize(model);
    return fallback;
}

} // namespace lb::ui
