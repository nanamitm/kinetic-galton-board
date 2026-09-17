#include "Geometry.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace {

QPointF pt(const QJsonArray &a)
{
    return QPointF(a.at(0).toDouble(), a.at(1).toDouble());
}

} // namespace

bool Geometry::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot open %1").arg(path);
        return false;
    }

    QJsonParseError perr {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &perr);
    if (doc.isNull()) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(path, perr.errorString());
        return false;
    }

    const QJsonObject root = doc.object();
    source = root.value(QStringLiteral("source")).toString(
        QStringLiteral("MaxwellBoltzmann775.3mf"));
    blenderVersion = root.value(QStringLiteral("blender")).toString();
    ballDiameter = root.value(QStringLiteral("ball_diameter")).toDouble(4.0);

    const QJsonObject body = root.value(QStringLiteral("body")).toObject();
    bboxMin = pt(body.value(QStringLiteral("bbox_min")).toArray());
    bboxMax = pt(body.value(QStringLiteral("bbox_max")).toArray());
    const QJsonArray depth = body.value(QStringLiteral("depth")).toArray();
    if (depth.size() == 2) {
        depthMin = depth.at(0).toDouble(depthMin);
        depthMax = depth.at(1).toDouble(depthMax);
    }
    const QJsonArray iz = body.value(QStringLiteral("interior_z")).toArray();
    if (iz.size() == 2) {
        interiorZ0 = iz.at(0).toDouble(interiorZ0);
        interiorZ1 = iz.at(1).toDouble(interiorZ1);
    }

    rings.clear();
    for (const QJsonValue &rv : body.value(QStringLiteral("rings")).toArray()) {
        QVector<QPointF> ring;
        const QJsonArray pts = rv.toArray();
        ring.reserve(pts.size());
        for (const QJsonValue &pv : pts)
            ring.push_back(pt(pv.toArray()));
        if (ring.size() >= 3)
            rings.push_back(ring);
    }

    const QJsonObject ag = root.value(QStringLiteral("agitator")).toObject();
    agitatorCentre = pt(ag.value(QStringLiteral("center")).toArray());
    agitatorRadius = ag.value(QStringLiteral("radius")).toDouble();
    agitatorHubRadius = ag.value(QStringLiteral("hub_radius")).toDouble();
    const QJsonArray az = ag.value(QStringLiteral("z")).toArray();
    if (az.size() == 2) {
        // only the part inside the chamber can touch a ball
        agitatorZ0 = std::max(interiorZ0, az.at(0).toDouble());
        agitatorZ1 = std::min(interiorZ1, az.at(1).toDouble());
    }
    agitatorProfile.clear();
    for (const QJsonValue &pv : ag.value(QStringLiteral("profile")).toArray())
        agitatorProfile.push_back(pt(pv.toArray()));

    const QJsonObject ft = root.value(QStringLiteral("features")).toObject();
    f.floorY = ft.value(QStringLiteral("floor_y")).toDouble();
    f.ceilingY = ft.value(QStringLiteral("ceiling_y")).toDouble();
    f.leftWallX = ft.value(QStringLiteral("left_wall_x")).toDouble();
    f.rightWallX = ft.value(QStringLiteral("right_wall_x")).toDouble();
    f.dividerX0 = ft.value(QStringLiteral("divider_x0")).toDouble();
    f.dividerX1 = ft.value(QStringLiteral("divider_x1")).toDouble();
    f.dividerTopY = ft.value(QStringLiteral("divider_top_y")).toDouble();
    f.effusionGap = ft.value(QStringLiteral("effusion_gap")).toDouble();
    f.finTopY = ft.value(QStringLiteral("fin_top_y")).toDouble();
    f.finPitch = ft.value(QStringLiteral("fin_pitch")).toDouble();
    f.dropHeight = ft.value(QStringLiteral("drop_height")).toDouble();

    f.fins.clear();
    for (const QJsonValue &v : ft.value(QStringLiteral("fins")).toArray()) {
        const QJsonObject o = v.toObject();
        f.fins.push_back({o.value(QStringLiteral("x0")).toDouble(),
                          o.value(QStringLiteral("x1")).toDouble(),
                          o.value(QStringLiteral("top_y")).toDouble()});
    }
    f.bins.clear();
    for (const QJsonValue &v : ft.value(QStringLiteral("bins")).toArray()) {
        const QJsonObject o = v.toObject();
        f.bins.push_back({o.value(QStringLiteral("x0")).toDouble(),
                          o.value(QStringLiteral("x1")).toDouble()});
    }

    f.hasSlider = ft.contains(QStringLiteral("slider_slot_y0"));
    if (f.hasSlider) {
        f.sliderSlotY0 = ft.value(QStringLiteral("slider_slot_y0")).toDouble();
        f.sliderSlotY1 = ft.value(QStringLiteral("slider_slot_y1")).toDouble();
    }

    if (rings.isEmpty() || f.bins.isEmpty()) {
        if (error)
            *error = QStringLiteral("%1 contains no usable outline").arg(path);
        return false;
    }
    return true;
}
