#include "Simulation.h"

#include <QRandomGenerator>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace {

constexpr double kTwoPi = 6.283185307179586;

inline double dot2(const QPointF &a, const QPointF &b)
{
    return a.x() * b.x() + a.y() * b.y();
}

inline double len2(const QPointF &a)
{
    return std::sqrt(dot2(a, a));
}

// Closest point to `p` on the segment a-b, and how far along it that is.
QPointF closestOnSegment(const QPointF &p, const QPointF &a, const QPointF &b,
                         double *tOut = nullptr)
{
    const QPointF ab = b - a;
    const double denom = dot2(ab, ab);
    if (denom < 1e-12) {
        if (tOut)
            *tOut = 0.0;
        return a;
    }
    double t = dot2(p - a, ab) / denom;
    t = std::clamp(t, 0.0, 1.0);
    if (tOut)
        *tOut = t;
    return a + ab * t;
}

bool pointInPolygon(const QPointF &p, const QVector<QPointF> &poly)
{
    bool in = false;
    for (int i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const QPointF &pi = poly[i];
        const QPointF &pj = poly[j];
        if ((pi.y() > p.y()) != (pj.y() > p.y())) {
            const double x = pi.x() + (p.y() - pi.y()) / (pj.y() - pi.y()) * (pj.x() - pi.x());
            if (p.x() < x)
                in = !in;
        }
    }
    return in;
}

} // namespace

void Simulation::setGeometry(const Geometry *g)
{
    m_geo = g;
    if (!g)
        return;
    m_radius = 0.5 * g->ballDiameter;
    m_rotor = g->agitatorProfile;
    m_layers = std::max(1, int(g->interiorDepth() / g->ballDiameter));
    buildStatic();
    reset();
}

int Simulation::cellOf(const Vec3 &p) const
{
    int cx = int((p.x - m_gridOrigin.x()) / m_cell);
    int cy = int((p.y - m_gridOrigin.y()) / m_cell);
    int cz = int((p.z - m_gridZ0) / m_cell);
    cx = std::clamp(cx, 0, m_nx - 1);
    cy = std::clamp(cy, 0, m_ny - 1);
    cz = std::clamp(cz, 0, m_nz - 1);
    return (cz * m_ny + cy) * m_nx + cx;
}

void Simulation::buildStatic()
{
    m_segments.clear();
    for (const QVector<QPointF> &ring : m_geo->rings) {
        // Each ring is a simple closed curve whose enclosed region is the
        // material, so with a counter-clockwise winding the material is always
        // on the left and (ey, -ex) points out of it.  Storing that normal is
        // what lets a contact be resolved correctly even when a sphere has
        // ended up on the wrong side of the surface.
        double area = 0.0;
        for (int i = 0; i < ring.size(); ++i) {
            const QPointF &p0 = ring[i];
            const QPointF &p1 = ring[(i + 1) % ring.size()];
            area += p0.x() * p1.y() - p1.x() * p0.y();
        }
        const double sgn = area >= 0.0 ? 1.0 : -1.0;
        const int m = ring.size();
        QVector<QPointF> normals(m);
        for (int i = 0; i < m; ++i) {
            const QPointF e = ring[(i + 1) % m] - ring[i];
            const double l = len2(e);
            normals[i] = l > 1e-9 ? QPointF(e.y(), -e.x()) * (sgn / l)
                                  : QPointF(0.0, 1.0);
        }
        // At a sharp vertex - every fin ends in one - the edge normal alone
        // cannot say which side of the surface a point is on: a sphere flying
        // just past a fin tip sits outside the material yet on the inner side
        // of one flank's infinite line.  The angle-weighted pseudonormal (in
        // 2D simply the normalised sum of the two adjacent edge normals) is
        // the correct thing to test against there.
        for (int i = 0; i < m; ++i) {
            const QPointF pa = normals[(i - 1 + m) % m] + normals[i];
            const QPointF pb = normals[i] + normals[(i + 1) % m];
            const double la = len2(pa);
            const double lb = len2(pb);
            m_segments.push_back({ring[i], ring[(i + 1) % m], normals[i],
                                  la > 1e-9 ? pa / la : normals[i],
                                  lb > 1e-9 ? pb / lb : normals[i]});
        }
    }

    m_cell = std::max(4.0, 3.0 * m_radius);
    m_gridOrigin = m_geo->bboxMin - QPointF(m_cell, m_cell);
    m_gridZ0 = m_geo->interiorZ0 - m_cell;
    const QPointF span = m_geo->bboxMax - m_gridOrigin + QPointF(m_cell, m_cell);
    m_nx = std::max(1, int(std::ceil(span.x() / m_cell)));
    m_ny = std::max(1, int(std::ceil(span.y() / m_cell)));
    m_nz = std::max(1, int(std::ceil((m_geo->interiorZ1 + m_cell - m_gridZ0) / m_cell)));

    // The walls are extrusions, so one (x, y) index serves every depth.
    m_segGrid.assign(m_nx * m_ny, {});
    for (int s = 0; s < m_segments.size(); ++s) {
        const Segment &seg = m_segments[s];
        const double pad = m_radius + 0.5;
        const double x0 = std::min(seg.a.x(), seg.b.x()) - pad;
        const double x1 = std::max(seg.a.x(), seg.b.x()) + pad;
        const double y0 = std::min(seg.a.y(), seg.b.y()) - pad;
        const double y1 = std::max(seg.a.y(), seg.b.y()) + pad;
        const int cx0 = std::clamp(int((x0 - m_gridOrigin.x()) / m_cell), 0, m_nx - 1);
        const int cx1 = std::clamp(int((x1 - m_gridOrigin.x()) / m_cell), 0, m_nx - 1);
        const int cy0 = std::clamp(int((y0 - m_gridOrigin.y()) / m_cell), 0, m_ny - 1);
        const int cy1 = std::clamp(int((y1 - m_gridOrigin.y()) / m_cell), 0, m_ny - 1);
        for (int cy = cy0; cy <= cy1; ++cy)
            for (int cx = cx0; cx <= cx1; ++cx)
                m_segGrid[cy * m_nx + cx].push_back(s);
    }
}

void Simulation::reset()
{
    if (!m_geo)
        return;

    m_balls.clear();
    m_escapeSpeeds.clear();
    m_binCounts.assign(m_geo->f.bins.size(), 0);
    m_rotorAngle = 0.0;
    m_accumulator = 0.0;
    m_stats = SimStats();

    // Fill the holding area above the slider, the way the machine is loaded.
    const Features &f = m_geo->f;
    const double pitch = 2.15 * m_radius;
    const double x0 = f.leftWallX + m_radius + 0.4;
    const double x1 = f.dividerX0 - m_radius - 0.4;
    const double z0 = m_geo->interiorZ0 + m_radius + 0.3;
    const double z1 = m_geo->interiorZ1 - m_radius - 0.3;
    const double yBase = (f.hasSlider ? f.sliderSlotY1 : f.floorY) + m_radius + 0.4;

    const int nx = std::max(1, int((x1 - x0) / pitch) + 1);
    const int nz = std::max(1, int((z1 - z0) / pitch) + 1);

    auto *rng = QRandomGenerator::global();
    m_balls.reserve(m_prm.ballCount);
    for (int i = 0; i < m_prm.ballCount; ++i) {
        const int row = i / (nx * nz);
        const int rem = i % (nx * nz);
        const int col = rem % nx;
        const int lay = rem / nx;

        Ball b;
        b.p = Vec3(x0 + (col + 0.5 * (row & 1)) * pitch,
                   yBase + row * pitch * 0.92,
                   z0 + (lay + 0.5 * (row & 1)) * pitch);
        b.p += Vec3(rng->bounded(0.2) - 0.1, rng->bounded(0.2) - 0.1,
                    rng->bounded(0.2) - 0.1);
        if (b.p.y > f.ceilingY - m_radius)
            break;                       // chamber is full; honour the geometry
        b.p.x = std::min(b.p.x, x1);
        b.p.z = std::min(b.p.z, z1);
        m_balls.push_back(b);
    }
}

double Simulation::fallTime() const
{
    if (!m_geo)
        return 0.0;
    const double h = std::max(1e-6, m_geo->f.dropHeight);
    return std::sqrt(2.0 * h / m_prm.gravity);
}

void Simulation::advance(double seconds)
{
    if (!m_geo || m_balls.isEmpty())
        return;
    m_accumulator += seconds;
    const double maxAccum = 40.0 * kFixedDt;     // never spiral
    if (m_accumulator > maxAccum)
        m_accumulator = maxAccum;
    while (m_accumulator >= kFixedDt) {
        step(kFixedDt);
        m_accumulator -= kFixedDt;
    }
    updateStats();
}

void Simulation::step(double dt)
{
    const Features &f = m_geo->f;
    const double omega = m_prm.motorOn ? m_prm.rpm * kTwoPi / 60.0 : 0.0;
    m_rotorAngle = std::fmod(m_rotorAngle + omega * dt, kTwoPi);

    const double drag = std::max(0.0, 1.0 - m_prm.linearDrag * dt);
    for (Ball &b : m_balls) {
        b.v.y -= m_prm.gravity * dt;
        b.v *= drag;
        b.p += b.v * dt;
    }

    rebuildBallGrid();
    collideBalls();

    for (Ball &b : m_balls) {
        // the rotor moves, so let it act first and give the static walls the
        // last word on where a sphere may actually be
        collideRotor(b);
        if (m_prm.sliderIn)
            collideSlider(b);
        // The box backstop knows nothing about the tray's 20 mm corner
        // fillets, so it runs first and the outline gets the final word;
        // otherwise it would park spheres inside a filleted corner.
        contain(b);
        collideWalls(b);
        collideWalls(b);
        collideCoverAndBack(b);

        const double s2 = b.v.lengthSquared();
        if (s2 > m_prm.maxSpeed * m_prm.maxSpeed)
            b.v *= m_prm.maxSpeed / std::sqrt(s2);

        // effusion: crossing the divider above its tip is a one-way trip
        if (!b.escaped && b.p.x > f.dividerX1 && b.p.y > f.finTopY) {
            b.escaped = true;
            b.escapeSpeed = std::abs(b.v.x);
            m_escapeSpeeds.push_back(b.escapeSpeed);
        }
    }

    m_stats.simTime += dt;
}

// Hard backstop: the machine is a closed box, so nothing may leave its
// interior no matter how a contact was resolved.
void Simulation::contain(Ball &ball)
{
    const Features &f = m_geo->f;
    const double x0 = f.leftWallX + m_radius;
    const double x1 = f.rightWallX - m_radius;
    const double y0 = f.floorY + m_radius;
    const double y1 = f.ceilingY - m_radius;
    const double z0 = m_geo->interiorZ0 + m_radius;
    const double z1 = m_geo->interiorZ1 - m_radius;
    const double e = m_prm.wallRestitution;

    if (ball.p.x < x0) { ball.p.x = x0; ball.v.x = std::abs(ball.v.x) * e; }
    else if (ball.p.x > x1) { ball.p.x = x1; ball.v.x = -std::abs(ball.v.x) * e; }
    if (ball.p.y < y0) { ball.p.y = y0; ball.v.y = std::abs(ball.v.y) * e; }
    else if (ball.p.y > y1) { ball.p.y = y1; ball.v.y = -std::abs(ball.v.y) * e; }
    if (ball.p.z < z0) { ball.p.z = z0; ball.v.z = std::abs(ball.v.z) * e; }
    else if (ball.p.z > z1) { ball.p.z = z1; ball.v.z = -std::abs(ball.v.z) * e; }
}

void Simulation::rebuildBallGrid()
{
    m_ballGrid.assign(m_nx * m_ny * m_nz, {});
    for (int i = 0; i < m_balls.size(); ++i)
        m_ballGrid[cellOf(m_balls[i].p)].push_back(i);
}

// The inner back face of the tray and the acrylic cover.
void Simulation::collideCoverAndBack(Ball &ball)
{
    const double e = m_prm.wallRestitution;
    const double k = 1.0 - m_prm.tangentFriction;
    const double z0 = m_geo->interiorZ0 + m_radius;
    const double z1 = m_geo->interiorZ1 - m_radius;
    if (ball.p.z < z0) {
        ball.p.z = z0;
        if (ball.v.z < 0.0) {
            ball.v.z = -e * ball.v.z;
            ball.v.x *= k;
            ball.v.y *= k;
        }
    } else if (ball.p.z > z1) {
        ball.p.z = z1;
        if (ball.v.z > 0.0) {
            ball.v.z = -e * ball.v.z;
            ball.v.x *= k;
            ball.v.y *= k;
        }
    }
}

void Simulation::collideWalls(Ball &ball)
{
    const double e = m_prm.wallRestitution;
    const QPointF here(ball.p.x, ball.p.y);
    const int cx = std::clamp(int((here.x() - m_gridOrigin.x()) / m_cell), 0, m_nx - 1);
    const int cy = std::clamp(int((here.y() - m_gridOrigin.y()) / m_cell), 0, m_ny - 1);

    // Gather the nearby segments once, tracking which one is closest.
    int candidates[96];
    int count = 0;
    double bestD2 = std::numeric_limits<double>::max();
    int nearest = -1;
    double nearestT = 0.0;
    QPointF nearestPt;
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            const int gx = cx + ox;
            const int gy = cy + oy;
            if (gx < 0 || gy < 0 || gx >= m_nx || gy >= m_ny)
                continue;
            for (int s : m_segGrid[gy * m_nx + gx]) {
                const Segment &seg = m_segments[s];
                double t = 0.0;
                const QPointF c = closestOnSegment(here, seg.a, seg.b, &t);
                const double d2 = dot2(here - c, here - c);
                if (d2 < bestD2) {
                    bestD2 = d2;
                    nearest = s;
                    nearestPt = c;
                    nearestT = t;
                }
                if (count < int(std::size(candidates)))
                    candidates[count++] = s;
            }
        }
    }
    if (nearest < 0)
        return;

    auto reflect = [&](const QPointF &n) {
        const double vn = ball.v.x * n.x() + ball.v.y * n.y();
        if (vn >= 0.0)
            return;
        const double k = 1.0 - m_prm.tangentFriction;
        const double tx = ball.v.x - n.x() * vn;
        const double ty = ball.v.y - n.y() * vn;
        ball.v.x = tx * k - n.x() * (e * vn);
        ball.v.y = ty * k - n.y() * (e * vn);
        ball.v.z *= k;
    };

    // Has the centre ended up inside the solid?  Judge that by the nearest
    // surface only: consulting every nearby segment would misread a sphere
    // sitting in a bin as being "behind" the far face of the next fin.  A
    // buried sphere is further from the surface than its own radius, so the
    // ordinary contact test below would skip it and it would stay stuck.
    {
        const Segment &seg = m_segments[nearest];
        const QPointF feature = nearestT <= 1e-6   ? seg.na
                              : nearestT >= 1.0 - 1e-6 ? seg.nb
                                                       : seg.n;
        const QPointF d = here - nearestPt;
        const double sn = d.x() * feature.x() + d.y() * feature.y();
        if (sn < 0.0) {
            const double dist = len2(d);
            // push out along the surface normal, not the pseudonormal
            const QPointF n = seg.n;
            ball.p.x += n.x() * (m_radius + dist);
            ball.p.y += n.y() * (m_radius + dist);
            reflect(n);
            return;
        }
    }

    // Ordinary contacts.
    for (int i = 0; i < count; ++i) {
        const Segment &seg = m_segments[candidates[i]];
        const QPointF p(ball.p.x, ball.p.y);
        const QPointF c = closestOnSegment(p, seg.a, seg.b);
        const QPointF d = p - c;
        double dist = len2(d);
        if (dist >= m_radius)
            continue;
        const QPointF n = dist > 1e-9 ? d / dist : seg.n;
        if (dist <= 1e-9)
            dist = 0.0;
        ball.p.x += n.x() * (m_radius - dist);
        ball.p.y += n.y() * (m_radius - dist);
        reflect(n);
    }
}

void Simulation::collideSlider(Ball &ball)
{
    const Features &f = m_geo->f;
    if (!f.hasSlider)
        return;
    const double y = f.sliderY();
    const double half = std::max(0.5, f.sliderHalfThickness());
    if (ball.p.x < f.leftWallX - m_radius || ball.p.x > f.dividerX0 + m_radius)
        return;
    const double dy = ball.p.y - y;
    if (std::abs(dy) >= half + m_radius)
        return;

    const double sign = dy >= 0.0 ? 1.0 : -1.0;
    ball.p.y = y + sign * (half + m_radius);
    const double vn = ball.v.y * sign;
    if (vn < 0.0) {
        ball.v.y -= (1.0 + m_prm.wallRestitution) * vn * sign;
        ball.v.x *= (1.0 - m_prm.tangentFriction);
        ball.v.z *= (1.0 - m_prm.tangentFriction);
    }
}

// The rotor is an extrusion as well: an (x, y) profile spinning about the
// depth axis, present only over its own slice of the chamber.
void Simulation::collideRotor(Ball &ball)
{
    if (m_rotor.isEmpty())
        return;
    const double rz0 = m_geo->agitatorZ0;
    const double rz1 = m_geo->agitatorZ1;
    if (ball.p.z < rz0 - m_radius || ball.p.z > rz1 + m_radius)
        return;

    const QPointF rel(ball.p.x - m_geo->agitatorCentre.x(),
                      ball.p.y - m_geo->agitatorCentre.y());
    const double reach = m_geo->agitatorRadius + m_radius;
    if (dot2(rel, rel) > reach * reach)
        return;

    const double ca = std::cos(m_rotorAngle);
    const double sa = std::sin(m_rotorAngle);
    const QPointF q(rel.x() * ca + rel.y() * sa, -rel.x() * sa + rel.y() * ca);

    double best = std::numeric_limits<double>::max();
    QPointF bestPt;
    for (int i = 0; i < m_rotor.size(); ++i) {
        const QPointF c = closestOnSegment(q, m_rotor[i], m_rotor[(i + 1) % m_rotor.size()]);
        const double d2 = dot2(q - c, q - c);
        if (d2 < best) {
            best = d2;
            bestPt = c;
        }
    }
    const bool insideProfile = pointInPolygon(q, m_rotor);
    const double dist = std::sqrt(best);

    // Depth: a sphere sitting beyond an end face can only be pushed out along
    // z, and only when its (x, y) is over the rotor at all.
    if (insideProfile && (ball.p.z < rz0 || ball.p.z > rz1)) {
        const bool below = ball.p.z < rz0;
        const double face = below ? rz0 : rz1;
        if (std::abs(ball.p.z - face) < m_radius) {
            ball.p.z = face + (below ? -m_radius : m_radius);
            if ((below && ball.v.z > 0.0) || (!below && ball.v.z < 0.0))
                ball.v.z = -m_prm.rotorRestitution * ball.v.z;
        }
        return;
    }
    if (!insideProfile && dist >= m_radius)
        return;

    QPointF nLocal = q - bestPt;
    const double nl = len2(nLocal);
    nLocal = nl > 1e-9 ? nLocal / nl : QPointF(1, 0);
    if (insideProfile)
        nLocal = -nLocal;

    const double push = insideProfile ? (m_radius + dist) : (m_radius - dist);

    // back to world space
    const QPointF n(nLocal.x() * ca - nLocal.y() * sa, nLocal.x() * sa + nLocal.y() * ca);
    const QPointF contact(bestPt.x() * ca - bestPt.y() * sa,
                          bestPt.x() * sa + bestPt.y() * ca);

    ball.p.x += n.x() * push;
    ball.p.y += n.y() * push;

    const double omega = m_prm.motorOn ? m_prm.rpm * kTwoPi / 60.0 : 0.0;
    const double ux = -omega * contact.y();
    const double uy = omega * contact.x();
    const double rvx = ball.v.x - ux;
    const double rvy = ball.v.y - uy;
    const double vn = rvx * n.x() + rvy * n.y();
    if (vn < 0.0) {
        const double k = 1.0 - m_prm.tangentFriction;
        const double tx = rvx - n.x() * vn;
        const double ty = rvy - n.y() * vn;
        ball.v.x = tx * k - n.x() * (m_prm.rotorRestitution * vn) + ux;
        ball.v.y = ty * k - n.y() * (m_prm.rotorRestitution * vn) + uy;
        ball.v.z *= k;
    }
}

void Simulation::collideBalls()
{
    const double d0 = 2.0 * m_radius;
    const double e = m_prm.ballRestitution;

    for (int cz = 0; cz < m_nz; ++cz) {
        for (int cy = 0; cy < m_ny; ++cy) {
            for (int cx = 0; cx < m_nx; ++cx) {
                const QVector<int> &here = m_ballGrid[(cz * m_ny + cy) * m_nx + cx];
                if (here.isEmpty())
                    continue;
                // half neighbourhood, so each unordered pair is visited once
                for (int oz = 0; oz <= 1; ++oz) {
                    for (int oy = (oz == 0 ? 0 : -1); oy <= 1; ++oy) {
                        for (int ox = (oz == 0 && oy == 0 ? 0 : -1); ox <= 1; ++ox) {
                            const int gx = cx + ox;
                            const int gy = cy + oy;
                            const int gz = cz + oz;
                            if (gx < 0 || gy < 0 || gz < 0
                                || gx >= m_nx || gy >= m_ny || gz >= m_nz)
                                continue;
                            const QVector<int> &other =
                                m_ballGrid[(gz * m_ny + gy) * m_nx + gx];
                            if (other.isEmpty())
                                continue;
                            const bool same = (ox == 0 && oy == 0 && oz == 0);
                            for (int ia = 0; ia < here.size(); ++ia) {
                                for (int ib = same ? ia + 1 : 0; ib < other.size(); ++ib) {
                                    Ball &A = m_balls[here[ia]];
                                    Ball &B = m_balls[other[ib]];
                                    const Vec3 d = B.p - A.p;
                                    const double dist = d.length();
                                    if (dist >= d0 || dist < 1e-9)
                                        continue;
                                    const Vec3 n = d * (1.0 / dist);
                                    const double overlap = d0 - dist;
                                    A.p -= n * (0.5 * overlap);
                                    B.p += n * (0.5 * overlap);
                                    const double vn = (B.v - A.v).dot(n);
                                    if (vn < 0.0) {
                                        const double j = -(1.0 + e) * vn * 0.5;
                                        A.v -= n * j;
                                        B.v += n * j;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

double Simulation::signedWallDistance(const Vec3 &p) const
{
    const QPointF q(p.x, p.y);
    double best = std::numeric_limits<double>::max();
    for (const Segment &s : m_segments) {
        const QPointF c = closestOnSegment(q, s.a, s.b);
        best = std::min(best, dot2(q - c, q - c));
    }
    best = std::sqrt(best);
    bool inside = false;
    for (const QVector<QPointF> &ring : m_geo->rings) {
        if (pointInPolygon(q, ring))
            inside = !inside;
    }
    return inside ? -best : best;
}

bool Simulation::withinRotorReach(const Vec3 &p) const
{
    if (!m_geo)
        return false;
    if (p.z < m_geo->agitatorZ0 - m_radius || p.z > m_geo->agitatorZ1 + m_radius)
        return false;
    const QPointF rel(p.x - m_geo->agitatorCentre.x(), p.y - m_geo->agitatorCentre.y());
    const double reach = m_geo->agitatorRadius + m_radius;
    return dot2(rel, rel) <= reach * reach;
}

bool Simulation::fitDistribution(double *sigma, double *vRms) const
{
    if (!m_geo)
        return false;
    const Features &f = m_geo->f;
    const double t = fallTime();
    double n = 0.0;
    double m2 = 0.0;
    for (int i = 0; i < m_binCounts.size() && i < f.bins.size(); ++i) {
        const double v = (f.bins[i].centre() - f.dividerX1) / std::max(1e-9, t);
        n += m_binCounts[i];
        m2 += m_binCounts[i] * v * v;
    }
    if (n < 8.0)
        return false;
    m2 /= n;
    if (sigma)
        *sigma = std::sqrt(std::max(1e-9, m2 / 2.0));
    if (vRms)
        *vRms = std::sqrt(m2);
    return true;
}

void Simulation::updateStats()
{
    const Features &f = m_geo->f;
    m_binCounts.assign(f.bins.size(), 0);

    int inChamber = 0;
    int escaped = 0;
    double sum = 0.0;
    double sum2 = 0.0;

    for (const Ball &b : m_balls) {
        if (b.escaped) {
            ++escaped;
            if (b.p.y < f.finTopY) {
                for (int i = 0; i < f.bins.size(); ++i) {
                    if (b.p.x >= f.bins[i].x0 && b.p.x < f.bins[i].x1) {
                        ++m_binCounts[i];
                        break;
                    }
                }
            }
        } else {
            ++inChamber;
            const double s = b.v.length();
            sum += s;
            sum2 += s * s;
        }
    }

    m_stats.inChamber = inChamber;
    m_stats.escaped = escaped;
    m_stats.meanSpeed = inChamber ? sum / inChamber : 0.0;
    m_stats.rmsSpeed = inChamber ? std::sqrt(sum2 / inChamber) : 0.0;
    m_stats.temperature = 0.5 * (inChamber ? sum2 / inChamber : 0.0);
}
