#pragma once

#include "Geometry.h"

#include <QPointF>
#include <QVector>
#include <cmath>

// 3D rigid-sphere simulation of the kinetic Galton board.
//
// The left chamber is a driven granular gas: a two-finned rotor pumps energy
// in, the spheres thermalise against each other, and the few that reach the
// gap over the divider with enough speed effuse into the bin field.  Because
// the gap is one ball wide the escaping sphere has no vertical velocity, so
// its fall time to the fin tips is fixed and the bin it lands in is
// proportional to the horizontal speed it escaped with.
//
// The tray is a printed open box, so every interior wall, the divider and all
// fourteen fins are straight extrusions along the depth axis.  That is what
// makes the 3D solver cheap: the walls are handled by the same 2D outline test
// as before, applied to a sphere's (x, y), and the depth axis only needs the
// inner back face and the cover.

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;

    Vec3() = default;
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3 &operator+=(const Vec3 &o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3 &operator-=(const Vec3 &o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3 &operator*=(double s) { x *= s; y *= s; z *= s; return *this; }

    double dot(const Vec3 &o) const { return x * o.x + y * o.y + z * o.z; }
    double lengthSquared() const { return dot(*this); }
    double length() const { return std::sqrt(dot(*this)); }
};

struct Ball {
    Vec3 p;
    Vec3 v;
    bool escaped = false;      // has crossed the divider into the bin field
    double escapeSpeed = 0.0;  // horizontal speed at effusion, mm/s - the
                               // component the landing position actually reads
};

struct SimParams {
    double rpm = 2600.0;           // agitator speed
    double gravity = 9810.0;       // mm/s^2
    double ballRestitution = 0.80;
    double wallRestitution = 0.55;
    double rotorRestitution = 0.60;
    double tangentFriction = 0.06;
    double linearDrag = 0.02;      // 1/s, stands in for air losses
    // A sphere pinned between the rotor and a wall would be pumped without
    // limit by a kinematic rotor, so cap how fast anything is allowed to get.
    // Also keeps a substep short enough that nothing tunnels through a 2 mm fin.
    double maxSpeed = 6000.0;      // mm/s
    int ballCount = 400;           // the real machine holds about this many
    bool motorOn = true;
    bool sliderIn = true;          // the shelf that holds the balls up on start
};

struct SimStats {
    int inChamber = 0;
    int escaped = 0;
    double meanSpeed = 0.0;        // chamber spheres, mm/s
    double rmsSpeed = 0.0;
    double temperature = 0.0;      // <E_kin> per sphere in mm^2/s^2 (m = 1)
    double simTime = 0.0;
};

class Simulation
{
public:
    static constexpr double kFixedDt = 1.0 / 2000.0;

    void setGeometry(const Geometry *g);
    const Geometry *geometry() const { return m_geo; }

    SimParams &params() { return m_prm; }
    const SimParams &params() const { return m_prm; }

    void reset();
    void advance(double seconds);   // seconds of simulated time
    void step(double dt);           // a single fixed substep

    const QVector<Ball> &balls() const { return m_balls; }
    const QVector<int> &binCounts() const { return m_binCounts; }
    const QVector<double> &escapeSpeeds() const { return m_escapeSpeeds; }
    const SimStats &stats() const { return m_stats; }

    double rotorAngle() const { return m_rotorAngle; }
    double ballRadius() const { return m_radius; }
    int depthLayers() const { return m_layers; }   // spheres across the depth

    // Fall time from the escape hole down to the fin tips: the constant that
    // turns a landing position into a speed measurement.
    double fallTime() const;
    double landingOffset(double speed) const { return speed * fallTime(); }
    double speedForOffset(double dx) const { return dx / fallTime(); }

    // Width of the distribution the bins are holding.
    //
    // A bin records only the horizontal velocity a sphere effused with, and
    // the flux through the hole is itself proportional to that component, so
    // the landing positions follow f(v) = (v / sigma^2) exp(-v^2 / 2 sigma^2)
    // -- a Rayleigh profile, which is also the Maxwell-Boltzmann speed
    // distribution of a two-dimensional gas.  For it <v^2> = 2 sigma^2, so one
    // moment of the histogram fixes the curve.  Depth does not change this:
    // the out-of-plane component never reaches a bin.
    // Returns false while too few spheres have landed to say anything.
    bool fitDistribution(double *sigma, double *vRms) const;

    // Diagnostics: distance from a point to the nearest wall surface in the
    // (x, y) cross-section, negative when the point is inside the solid.
    // A resting sphere should sit at about +radius, never below it.
    double signedWallDistance(const Vec3 &p) const;

    // Can the rotor reach this point at all?  It sweeps a disc of
    // agitatorRadius about its axis and spans only part of the depth.
    bool withinRotorReach(const Vec3 &p) const;

private:
    struct Segment {
        QPointF a, b;
        QPointF n;         // unit normal pointing away from the printed material
        QPointF na, nb;    // pseudonormals at the two endpoints (see buildStatic)
    };

    void buildStatic();
    void rebuildBallGrid();
    void collideWalls(Ball &ball);
    void collideCoverAndBack(Ball &ball);
    void collideRotor(Ball &ball);
    void collideSlider(Ball &ball);
    void collideBalls();
    void contain(Ball &ball);
    void updateStats();

    const Geometry *m_geo = nullptr;
    SimParams m_prm;
    SimStats m_stats;

    QVector<Ball> m_balls;
    QVector<Segment> m_segments;
    QVector<QVector<int>> m_segGrid;   // static segments per (x,y) cell
    QVector<QVector<int>> m_ballGrid;  // ball indices per (x,y,z) cell
    QVector<QPointF> m_rotor;          // rotor outline in its own frame

    QVector<int> m_binCounts;
    QVector<double> m_escapeSpeeds;

    double m_radius = 2.0;
    double m_rotorAngle = 0.0;
    double m_accumulator = 0.0;
    int m_layers = 1;

    double m_cell = 6.0;
    QPointF m_gridOrigin;
    double m_gridZ0 = 0.0;
    int m_nx = 1;
    int m_ny = 1;
    int m_nz = 1;

    int cellOf(const Vec3 &p) const;
};
