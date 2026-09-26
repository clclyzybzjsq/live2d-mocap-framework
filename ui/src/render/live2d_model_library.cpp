#include "live2d_model_library.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>

#include <algorithm>
#include <utility>

namespace lb::ui {

namespace {

QString resolve(const QDir& base, const QString& rel) {
    if (rel.isEmpty()) return QString();
    const QFileInfo fi(rel);
    if (fi.isAbsolute()) return QDir::cleanPath(rel);
    return QDir::cleanPath(base.absoluteFilePath(rel));
}

// 收集 Motions / Expressions 里引用到的文件（结构随导出选项略有差异）
void collect_references(const QJsonObject& refs, Live2dModelInfo* info, const QDir& base) {
    if (refs.contains(QStringLiteral("Physics"))) {
        const QString p = resolve(base, refs.value(QStringLiteral("Physics")).toString());
        if (!p.isEmpty()) info->physics << p;
    }
    if (refs.contains(QStringLiteral("Pose"))) {
        const QString p = resolve(base, refs.value(QStringLiteral("Pose")).toString());
        if (!p.isEmpty()) info->physics << p;
    }

    const QJsonObject motions = refs.value(QStringLiteral("Motions")).toObject();
    for (auto it = motions.begin(); it != motions.end(); ++it) {
        const QJsonArray arr = it.value().toArray();
        for (const QJsonValue& v : arr) {
            const QString f = resolve(base, v.toObject().value(QStringLiteral("File")).toString());
            if (!f.isEmpty()) info->motions << f;
        }
    }

    const QJsonArray exprs = refs.value(QStringLiteral("Expressions")).toArray();
    for (const QJsonValue& v : exprs) {
        const QString f = resolve(base, v.toObject().value(QStringLiteral("File")).toString());
        if (!f.isEmpty()) info->expressions << f;
    }
}

} // namespace

Live2dModelInfo Live2dModelLibrary::parse(const QString& model_json_path) {
    Live2dModelInfo info;
    info.model_json = QDir::cleanPath(model_json_path);
    const QFileInfo json_fi(info.model_json);
    info.dir = json_fi.absolutePath();
    const QDir base(info.dir);

    QFile f(info.model_json);
    if (!f.open(QIODevice::ReadOnly)) return info;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return info;

    const QJsonObject root = doc.object();

    // 显示名：优先 Name，其次目录名
    info.name = root.value(QStringLiteral("Name")).toString();
    if (info.name.isEmpty()) info.name = QFileInfo(info.dir).fileName();

    const QJsonObject refs = root.value(QStringLiteral("FileReferences")).toObject();
    info.moc3 = resolve(base, refs.value(QStringLiteral("Moc")).toString());

    const QJsonArray textures = refs.value(QStringLiteral("Textures")).toArray();
    for (const QJsonValue& v : textures) {
        const QString t = resolve(base, v.toString());
        if (!t.isEmpty()) info.textures << t;
    }

    collect_references(refs, &info, base);

    info.moc3_exists = !info.moc3.isEmpty() && QFileInfo::exists(info.moc3);
    return info;
}

std::vector<Live2dModelInfo> Live2dModelLibrary::scan(const QString& root, int max_depth) {
    std::vector<Live2dModelInfo> out;
    const QDir root_dir(root);
    if (!root_dir.exists()) return out;

    QStringList filters;
    filters << QStringLiteral("*.model3.json");

    // 受限深度的递归扫描，避免误入巨深的无关目录
    QStringList pending{root_dir.absolutePath()};
    int depth = 0;
    QSet<QString> seen;
    while (!pending.isEmpty() && depth <= max_depth) {
        QStringList next;
        for (const QString& d : pending) {
            if (seen.contains(d)) continue;
            seen.insert(d);

            QDirIterator it(d, filters, QDir::Files, QDirIterator::NoIteratorFlags);
            while (it.hasNext()) {
                const QString path = it.next();
                Live2dModelInfo info = parse(path);
                if (info.usable()) out.push_back(std::move(info));
            }

            const QDir cur(d);
            const QFileInfoList subs =
                cur.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QFileInfo& sub : subs) {
                // 跳过模型自身的数据子目录（*.2048 之类）以加快扫描
                if (sub.fileName().endsWith(QStringLiteral(".2048"))) continue;
                next << sub.absoluteFilePath();
            }
        }
        pending = next;
        ++depth;
    }

    std::sort(out.begin(), out.end(), [](const Live2dModelInfo& a, const Live2dModelInfo& b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return out;
}

} // namespace lb::ui
