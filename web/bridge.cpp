#include "Simulation.h"
#include <emscripten/bind.h>
#include <algorithm>
using emscripten::val;
namespace {
Geometry geometry;
Simulation simulation;
std::vector<float> positions;
QPointF point(val p) { return {p[0].as<double>(), p[1].as<double>()}; }
double number(val o, const char *key) { return o[key].as<double>(); }
void initialize(val root) {
    geometry = Geometry{};
    geometry.ballDiameter = number(root, "ball_diameter");
    const val body = root["body"], ag = root["agitator"], f = root["features"];
    geometry.bboxMin = point(body["bbox_min"]);
    geometry.bboxMax = point(body["bbox_max"]);
    geometry.depthMin = body["depth"][0].as<double>();
    geometry.depthMax = body["depth"][1].as<double>();
    geometry.interiorZ0 = body["interior_z"][0].as<double>();
    geometry.interiorZ1 = body["interior_z"][1].as<double>();
    const val rings = body["rings"];
    for (unsigned i=0; i<rings["length"].as<unsigned>(); ++i) {
        QVector<QPointF> ring;
        for (unsigned j=0; j<rings[i]["length"].as<unsigned>(); ++j)
            ring.push_back(point(rings[i][j]));
        geometry.rings.push_back(ring);
    }
    geometry.agitatorCentre = point(ag["center"]);
    geometry.agitatorRadius = number(ag,"radius");
    geometry.agitatorHubRadius = number(ag,"hub_radius");
    geometry.agitatorZ0 = std::max(geometry.interiorZ0, ag["z"][0].as<double>());
    geometry.agitatorZ1 = std::min(geometry.interiorZ1, ag["z"][1].as<double>());
    for (unsigned i=0; i<ag["profile"]["length"].as<unsigned>(); ++i)
        geometry.agitatorProfile.push_back(point(ag["profile"][i]));
#define FIELD(member, key) geometry.f.member = number(f, key)
    FIELD(floorY,"floor_y"); FIELD(ceilingY,"ceiling_y");
    FIELD(leftWallX,"left_wall_x"); FIELD(rightWallX,"right_wall_x");
    FIELD(dividerX0,"divider_x0"); FIELD(dividerX1,"divider_x1");
    FIELD(dividerTopY,"divider_top_y"); FIELD(effusionGap,"effusion_gap");
    FIELD(finTopY,"fin_top_y"); FIELD(finPitch,"fin_pitch"); FIELD(dropHeight,"drop_height");
    geometry.f.hasSlider = !f["slider_slot_y0"].isUndefined();
    if (geometry.f.hasSlider) { FIELD(sliderSlotY0,"slider_slot_y0"); FIELD(sliderSlotY1,"slider_slot_y1"); }
#undef FIELD
    for (unsigned i=0; i<f["bins"]["length"].as<unsigned>(); ++i) {
        val b=f["bins"][i]; geometry.f.bins.push_back({number(b,"x0"),number(b,"x1")});
    }
    for (unsigned i=0; i<f["fins"]["length"].as<unsigned>(); ++i) {
        val b=f["fins"][i]; geometry.f.fins.push_back({number(b,"x0"),number(b,"x1"),number(b,"top_y")});
    }
    simulation.setGeometry(&geometry);
    simulation.advance(0);
}
void configure(double rpm, int count, double restitution, bool motor, bool slider) {
    auto &p = simulation.params();
    const int n=std::clamp(count,50,800);
    const bool reset=n!=p.ballCount;
    p.rpm=std::clamp(rpm,200.0,6000.0); p.ballCount=n;
    p.ballRestitution=std::clamp(restitution,0.3,0.98);
    p.motorOn=motor; p.sliderIn=slider;
    if(reset) simulation.reset();
    simulation.advance(0);
}
void reset() { simulation.reset(); simulation.advance(0); }
void advance(double dt) { simulation.advance(std::clamp(dt,0.0,0.02)); }
val snapshot() {
    positions.clear();
    for (const auto &b: simulation.balls()) {
        positions.insert(positions.end(), {float(b.p.x),float(b.p.y),float(b.p.z), b.escaped?1.f:0.f});
    }
    val out=val::object();
    // The JS caller copies this short-lived view before the next advance.
    out.set("balls",val(emscripten::typed_memory_view(positions.size(),positions.data())));
    val bins=val::array();
    for(unsigned i=0;i<simulation.binCounts().size();++i) bins.set(i,simulation.binCounts()[i]);
    out.set("bins",bins);
    const auto &s=simulation.stats();
    out.set("time",s.simTime); out.set("chamber",s.inChamber); out.set("escaped",s.escaped);
    out.set("rms",s.rmsSpeed/1000); out.set("angle",simulation.rotorAngle());
    out.set("fallTime",simulation.fallTime());
    double sigma=0;
    out.set("fitted",simulation.fitDistribution(&sigma,nullptr)); out.set("sigma",sigma);
    return out;
}
}
EMSCRIPTEN_BINDINGS(galton) {
    emscripten::function("initialize",&initialize);
    emscripten::function("configure",&configure);
    emscripten::function("reset",&reset);
    emscripten::function("advance",&advance);
    emscripten::function("snapshot",&snapshot);
}
