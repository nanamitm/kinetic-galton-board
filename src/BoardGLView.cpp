#include "BoardGLView.h"
#include "Simulation.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVector3D>
#include <QVector4D>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {

constexpr double kRadToDeg = 57.29577951308232;

const char *kSolidVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uMvp;
uniform mat4 uModel;
out vec3 vNormal;
out vec3 vWorld;
void main()
{
    vNormal = mat3(uModel) * aNormal;
    vWorld = (uModel * vec4(aPos, 1.0)).xyz;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

const char *kSolidFrag = R"(#version 330 core
in vec3 vNormal;
in vec3 vWorld;
uniform vec3 uColour;
uniform float uAlpha;
out vec4 fragColour;

// The extruded shell is an open surface, so light it two-sided.
vec3 shade(vec3 base, vec3 n)
{
    vec3 l1 = normalize(vec3(-0.35, 0.55, 0.85));
    vec3 l2 = normalize(vec3(0.7, -0.25, 0.4));
    float d = 0.78 * abs(dot(n, l1)) + 0.30 * abs(dot(n, l2));
    return base * (0.24 + d);
}

void main()
{
    fragColour = vec4(shade(uColour, normalize(vNormal)), uAlpha);
}
)";

const char *kInstVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aOffset;
layout(location = 3) in vec3 aColour;
uniform mat4 uMvp;
uniform float uRadius;
out vec3 vNormal;
out vec3 vColour;
void main()
{
    vNormal = aNormal;
    vColour = aColour;
    gl_Position = uMvp * vec4(aPos * uRadius + aOffset, 1.0);
}
)";

const char *kInstFrag = R"(#version 330 core
in vec3 vNormal;
in vec3 vColour;
out vec4 fragColour;
void main()
{
    vec3 n = normalize(vNormal);
    vec3 l1 = normalize(vec3(-0.35, 0.55, 0.85));
    vec3 l2 = normalize(vec3(0.7, -0.25, 0.4));
    float d = 0.85 * max(dot(n, l1), 0.0) + 0.25 * max(dot(n, l2), 0.0);
    float rim = pow(1.0 - max(n.z, 0.0), 3.0) * 0.18;
    fragColour = vec4(vColour * (0.26 + d) + rim, 1.0);
}
)";

void pushVertex(QVector<float> &out, const QVector3D &p, const QVector3D &n)
{
    out << p.x() << p.y() << p.z() << n.x() << n.y() << n.z();
}

void pushQuad(QVector<float> &out, const QVector3D &a, const QVector3D &b,
              const QVector3D &c, const QVector3D &d)
{
    const QVector3D n = QVector3D::crossProduct(b - a, c - a).normalized();
    pushVertex(out, a, n); pushVertex(out, b, n); pushVertex(out, c, n);
    pushVertex(out, a, n); pushVertex(out, c, n); pushVertex(out, d, n);
}

void pushBox(QVector<float> &out, const QVector3D &lo, const QVector3D &hi)
{
    const float x0 = lo.x(), y0 = lo.y(), z0 = lo.z();
    const float x1 = hi.x(), y1 = hi.y(), z1 = hi.z();
    pushQuad(out, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1});
    pushQuad(out, {x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0});
    pushQuad(out, {x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0});
    pushQuad(out, {x0, y0, z1}, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1});
    pushQuad(out, {x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0});
    pushQuad(out, {x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1});
}

// Icosphere, so every sphere is one instanced draw.
void buildIcosphere(QVector<float> &out, int subdivisions)
{
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    QVector<QVector3D> v {
        {-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0},
        {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},
        {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (QVector3D &p : v)
        p.normalize();
    QVector<int> idx {
        0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11,
        1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
        3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9,
        4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};

    QVector<QVector3D> tris;
    for (int i = 0; i < idx.size(); i += 3) {
        tris << v[idx[i]] << v[idx[i + 1]] << v[idx[i + 2]];
    }
    for (int s = 0; s < subdivisions; ++s) {
        QVector<QVector3D> next;
        next.reserve(tris.size() * 4);
        for (int i = 0; i < tris.size(); i += 3) {
            const QVector3D &a = tris[i];
            const QVector3D &b = tris[i + 1];
            const QVector3D &c = tris[i + 2];
            const QVector3D ab = (a + b).normalized();
            const QVector3D bc = (b + c).normalized();
            const QVector3D ca = (c + a).normalized();
            next << a << ab << ca << b << bc << ab << c << ca << bc << ab << bc << ca;
        }
        tris = next;
    }
    out.reserve(tris.size() * 6);
    for (const QVector3D &p : tris)
        pushVertex(out, p, p);     // unit sphere: position doubles as normal
}

QVector3D speedColour(double s, double scale)
{
    const float t = float(std::clamp(scale > 1e-6 ? s / scale : 0.0, 0.0, 1.0));
    const QColor c = QColor::fromHsvF(0.62 * (1.0 - t), 0.70, 0.55 + 0.45 * t);
    return {float(c.redF()), float(c.greenF()), float(c.blueF())};
}

} // namespace

void BoardGLView::Mesh::destroy()
{
    if (vbo.isCreated())
        vbo.destroy();
    if (vao.isCreated())
        vao.destroy();
    vertexCount = 0;
}

BoardGLView::BoardGLView(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(640, 360);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);
}

BoardGLView::~BoardGLView()
{
    if (!m_ready)
        return;
    makeCurrent();
    m_body.destroy();
    m_rotor.destroy();
    m_slider.destroy();
    m_cover.destroy();
    if (m_sphereVbo.isCreated())
        m_sphereVbo.destroy();
    if (m_instanceVbo.isCreated())
        m_instanceVbo.destroy();
    if (m_sphereVao.isCreated())
        m_sphereVao.destroy();
    doneCurrent();
}

void BoardGLView::setSimulation(const Simulation *sim)
{
    m_sim = sim;
    if (sim && sim->geometry()) {
        const Geometry &g = *sim->geometry();
        m_target = QVector3D(float(0.5 * (g.f.leftWallX + g.f.rightWallX)),
                             float(0.5 * (g.f.floorY + g.f.ceilingY)),
                             float(0.5 * (g.interiorZ0 + g.interiorZ1)));
        m_distance = float(g.bboxMax.x() - g.bboxMin.x()) * 1.15f;
    }
    if (m_ready) {
        makeCurrent();
        buildBodyMesh();
        buildRotorMesh();
        buildSliderMesh();
        doneCurrent();
    }
    update();
}

void BoardGLView::resetView() { setFrontView(); }

void BoardGLView::setFrontView()
{
    m_yaw = 0.0f;
    m_pitch = 0.0f;
    if (m_sim && m_sim->geometry())
        m_distance = float(m_sim->geometry()->bboxMax.x()
                           - m_sim->geometry()->bboxMin.x()) * 1.15f;
    update();
}

void BoardGLView::setAngledView()
{
    m_yaw = -0.62f;
    m_pitch = 0.42f;
    if (m_sim && m_sim->geometry())
        m_distance = float(m_sim->geometry()->bboxMax.x()
                           - m_sim->geometry()->bboxMin.x()) * 1.25f;
    update();
}

void BoardGLView::initializeGL()
{
    initializeOpenGLFunctions();
    m_ready = true;

    glClearColor(0.055f, 0.063f, 0.078f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_solid.addShaderFromSourceCode(QOpenGLShader::Vertex, kSolidVert);
    m_solid.addShaderFromSourceCode(QOpenGLShader::Fragment, kSolidFrag);
    m_solid.link();

    m_instanced.addShaderFromSourceCode(QOpenGLShader::Vertex, kInstVert);
    m_instanced.addShaderFromSourceCode(QOpenGLShader::Fragment, kInstFrag);
    m_instanced.link();

    buildSphereMesh();
    buildBodyMesh();
    buildRotorMesh();
    buildSliderMesh();
}

void BoardGLView::uploadMesh(Mesh &mesh, const QVector<float> &data)
{
    if (!mesh.vao.isCreated())
        mesh.vao.create();
    if (!mesh.vbo.isCreated())
        mesh.vbo.create();
    mesh.vao.bind();
    mesh.vbo.bind();
    mesh.vbo.allocate(data.constData(), int(data.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void *>(3 * sizeof(float)));
    mesh.vbo.release();
    mesh.vao.release();
    mesh.vertexCount = int(data.size() / 6);
}

void BoardGLView::buildBodyMesh()
{
    m_body.vertexCount = 0;
    m_cover.vertexCount = 0;
    if (!m_sim || !m_sim->geometry())
        return;
    const Geometry &g = *m_sim->geometry();

    QVector<float> data;
    // Sweep the extracted outline along the depth axis: that sweep *is* the
    // printed part, walls, divider and all fourteen fins included.
    const float z0 = float(g.depthMin);
    const float z1 = float(g.depthMax);
    for (const QVector<QPointF> &ring : g.rings) {
        for (int i = 0; i < ring.size(); ++i) {
            const QPointF &a = ring[i];
            const QPointF &b = ring[(i + 1) % ring.size()];
            pushQuad(data,
                     {float(a.x()), float(a.y()), z0},
                     {float(b.x()), float(b.y()), z0},
                     {float(b.x()), float(b.y()), z1},
                     {float(a.x()), float(a.y()), z1});
        }
    }
    // The inner back face the balls rest against.
    const float bx0 = float(g.f.leftWallX);
    const float bx1 = float(g.f.rightWallX);
    const float by0 = float(g.f.floorY);
    const float by1 = float(g.f.ceilingY);
    const float bz = float(g.interiorZ0);
    pushQuad(data, {bx0, by0, bz}, {bx1, by0, bz}, {bx1, by1, bz}, {bx0, by1, bz});
    uploadMesh(m_body, data);

    // The laser-cut acrylic cover, drawn translucent.
    QVector<float> cover;
    const float cz = float(g.interiorZ1);
    pushQuad(cover, {float(g.bboxMin.x()), float(g.bboxMin.y()), cz},
             {float(g.bboxMax.x()), float(g.bboxMin.y()), cz},
             {float(g.bboxMax.x()), float(g.bboxMax.y()), cz},
             {float(g.bboxMin.x()), float(g.bboxMax.y()), cz});
    uploadMesh(m_cover, cover);
}

void BoardGLView::buildRotorMesh()
{
    m_rotor.vertexCount = 0;
    if (!m_sim || !m_sim->geometry())
        return;
    const Geometry &g = *m_sim->geometry();
    if (g.agitatorProfile.isEmpty())
        return;

    QVector<float> data;
    const float z0 = float(g.agitatorZ0);
    const float z1 = float(g.agitatorZ1);
    const QVector<QPointF> &pr = g.agitatorProfile;
    for (int i = 0; i < pr.size(); ++i) {
        const QPointF &a = pr[i];
        const QPointF &b = pr[(i + 1) % pr.size()];
        pushQuad(data,
                 {float(a.x()), float(a.y()), z0},
                 {float(b.x()), float(b.y()), z0},
                 {float(b.x()), float(b.y()), z1},
                 {float(a.x()), float(a.y()), z1});
        // the profile is star-shaped about its axis, so fan the end caps
        const QVector3D nUp(0, 0, 1);
        pushVertex(data, {0, 0, z1}, nUp);
        pushVertex(data, {float(a.x()), float(a.y()), z1}, nUp);
        pushVertex(data, {float(b.x()), float(b.y()), z1}, nUp);
        const QVector3D nDn(0, 0, -1);
        pushVertex(data, {0, 0, z0}, nDn);
        pushVertex(data, {float(b.x()), float(b.y()), z0}, nDn);
        pushVertex(data, {float(a.x()), float(a.y()), z0}, nDn);
    }
    uploadMesh(m_rotor, data);
}

void BoardGLView::buildSliderMesh()
{
    m_slider.vertexCount = 0;
    if (!m_sim || !m_sim->geometry())
        return;
    const Geometry &g = *m_sim->geometry();
    if (!g.f.hasSlider)
        return;
    QVector<float> data;
    pushBox(data,
            {float(g.f.leftWallX - 8.0), float(g.f.sliderSlotY0), float(g.interiorZ0)},
            {float(g.f.dividerX0), float(g.f.sliderSlotY1), float(g.interiorZ1)});
    uploadMesh(m_slider, data);
}

void BoardGLView::buildSphereMesh()
{
    QVector<float> data;
    buildIcosphere(data, 2);
    m_sphereVerts = int(data.size() / 6);

    if (!m_sphereVao.isCreated())
        m_sphereVao.create();
    if (!m_sphereVbo.isCreated())
        m_sphereVbo.create();
    if (!m_instanceVbo.isCreated())
        m_instanceVbo.create();

    m_sphereVao.bind();
    m_sphereVbo.bind();
    m_sphereVbo.allocate(data.constData(), int(data.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void *>(3 * sizeof(float)));

    m_instanceVbo.bind();
    m_instanceVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void *>(3 * sizeof(float)));
    glVertexAttribDivisor(3, 1);

    m_sphereVao.release();
    m_sphereVbo.release();
    m_instanceVbo.release();
}

void BoardGLView::resizeGL(int w, int h)
{
    m_proj.setToIdentity();
    m_proj.perspective(38.0f, h > 0 ? float(w) / float(h) : 1.0f, 5.0f, 4000.0f);
}

QMatrix4x4 BoardGLView::viewMatrix() const
{
    const float cp = std::cos(m_pitch);
    const QVector3D dir(std::sin(m_yaw) * cp, std::sin(m_pitch),
                        std::cos(m_yaw) * cp);
    const QVector3D eye = m_target + dir * m_distance;
    QMatrix4x4 v;
    v.lookAt(eye, m_target, QVector3D(0, 1, 0));
    return v;
}

void BoardGLView::drawMesh(Mesh &mesh, const QMatrix4x4 &model,
                           const QVector3D &colour, float alpha)
{
    if (mesh.vertexCount == 0)
        return;
    m_solid.bind();
    m_solid.setUniformValue("uMvp", m_mvp * model);
    m_solid.setUniformValue("uModel", model);
    m_solid.setUniformValue("uColour", colour);
    m_solid.setUniformValue("uAlpha", alpha);
    mesh.vao.bind();
    glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
    mesh.vao.release();
    m_solid.release();
}

void BoardGLView::drawSpheres()
{
    const QVector<Ball> &balls = m_sim->balls();
    if (balls.isEmpty())
        return;

    const double scale = std::max(200.0, 2.2 * m_sim->stats().rmsSpeed);
    m_instanceData.resize(balls.size() * 6);
    float *out = m_instanceData.data();
    for (const Ball &b : balls) {
        *out++ = float(b.p.x);
        *out++ = float(b.p.y);
        *out++ = float(b.p.z);
        QVector3D c(0.91f, 0.89f, 0.84f);
        if (m_colourBySpeed && !b.escaped)
            c = speedColour(b.v.length(), scale);
        *out++ = c.x();
        *out++ = c.y();
        *out++ = c.z();
    }

    m_instanceVbo.bind();
    m_instanceVbo.allocate(m_instanceData.constData(),
                           int(m_instanceData.size() * sizeof(float)));
    m_instanceVbo.release();

    m_instanced.bind();
    m_instanced.setUniformValue("uMvp", m_mvp);
    m_instanced.setUniformValue("uRadius", float(m_sim->ballRadius()));
    m_sphereVao.bind();
    glDrawArraysInstanced(GL_TRIANGLES, 0, m_sphereVerts, balls.size());
    m_sphereVao.release();
    m_instanced.release();
}

void BoardGLView::paintGL()
{
    // The QPainter overlay at the end of this function leaves the context in
    // its own state (premultiplied blending, no depth test), so every frame
    // has to put back what it needs rather than rely on initializeGL.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_MULTISAMPLE);

    glClearColor(0.055f, 0.063f, 0.078f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!m_sim || !m_sim->geometry())
        return;

    const Geometry &g = *m_sim->geometry();
    m_mvp = m_proj * viewMatrix();

    QMatrix4x4 identity;
    drawMesh(m_body, identity, {0.22f, 0.24f, 0.29f}, 1.0f);

    if (g.f.hasSlider && m_sim->params().sliderIn)
        drawMesh(m_slider, identity, {0.78f, 0.55f, 0.24f}, 1.0f);

    QMatrix4x4 rotorModel;
    rotorModel.translate(float(g.agitatorCentre.x()), float(g.agitatorCentre.y()), 0.0f);
    rotorModel.rotate(float(m_sim->rotorAngle() * kRadToDeg), 0.0f, 0.0f, 1.0f);
    drawMesh(m_rotor, rotorModel, {0.52f, 0.57f, 0.65f}, 1.0f);

    drawSpheres();

    if (m_showCover) {
        // a faint veil only: the real part is clear acrylic
        glDepthMask(GL_FALSE);
        drawMesh(m_cover, identity, {0.42f, 0.70f, 0.80f}, 0.055f);
        glDepthMask(GL_TRUE);
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    drawOverlay(painter);
}

// Bin numbers and the fitted curve are flat information, so they go on as a
// 2D overlay projected through the same matrix the scene uses.
void BoardGLView::drawOverlay(QPainter &painter)
{
    const Geometry &g = *m_sim->geometry();
    const Features &f = g.f;
    const float w = float(width());
    const float h = float(height());
    const float zMid = float(0.5 * (g.interiorZ0 + g.interiorZ1));

    auto project = [&](const QVector3D &p, QPointF *out) {
        const QVector4D clip = m_mvp * QVector4D(p, 1.0f);
        if (clip.w() <= 1e-4f)
            return false;
        *out = QPointF((clip.x() / clip.w() * 0.5f + 0.5f) * w,
                       (0.5f - clip.y() / clip.w() * 0.5f) * h);
        return true;
    };

    if (m_showLabels) {
        QFont fnt = font();
        fnt.setPointSizeF(8.0);
        painter.setFont(fnt);
        painter.setPen(QColor(0x7e, 0x8a, 0x9c));
        for (int i = 0; i < f.bins.size(); ++i) {
            QPointF s;
            if (project(QVector3D(float(f.bins[i].centre()),
                                  float(f.floorY) - 4.0f, zMid), &s))
                painter.drawText(QRectF(s.x() - 14, s.y() - 8, 28, 16),
                                 Qt::AlignCenter, QString::number(i + 1));
        }
    }

    double sigma = 0.0;
    if (m_showTheory && m_sim->fitDistribution(&sigma, nullptr)) {
        const double t = m_sim->fallTime();
        const double x0 = f.dividerX1;
        const double d = 2.0 * m_sim->ballRadius();
        const double perLayer =
            std::max(1.0, std::floor(f.bins[0].width() / d))
            * std::max(1.0, std::floor(g.interiorDepth() / d));
        const double layerH = 0.9 * d;

        int maxCount = 1;
        for (int c : m_sim->binCounts())
            maxCount = std::max(maxCount, c);
        double peak = 0.0;
        QVector<double> model(f.bins.size(), 0.0);
        for (int i = 0; i < f.bins.size(); ++i) {
            const double v = (f.bins[i].centre() - x0) / std::max(1e-9, t);
            model[i] = v * std::exp(-0.5 * v * v / (sigma * sigma));
            peak = std::max(peak, model[i]);
        }

        QPainterPath curve;
        bool started = false;
        for (int i = 0; i < f.bins.size(); ++i) {
            const double n = peak > 0 ? model[i] / peak * maxCount : 0.0;
            const double y = f.floorY + (n / perLayer) * layerH;
            QPointF s;
            if (!project(QVector3D(float(f.bins[i].centre()), float(y), zMid), &s))
                continue;
            if (started) {
                curve.lineTo(s);
            } else {
                curve.moveTo(s);
                started = true;
            }
        }
        QPen pen(QColor(0xff, 0xd5, 0x4f), 2.0);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(curve);
    }

    painter.setPen(QColor(0x5c, 0x66, 0x76));
    painter.drawText(QRectF(8, height() - 22, width() - 16, 18),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     tr("drag to orbit  -  wheel to zoom"));
}

void BoardGLView::mousePressEvent(QMouseEvent *e)
{
    m_lastMouse = e->pos();
}

void BoardGLView::mouseMoveEvent(QMouseEvent *e)
{
    if (!(e->buttons() & (Qt::LeftButton | Qt::RightButton)))
        return;
    const QPoint d = e->pos() - m_lastMouse;
    m_lastMouse = e->pos();
    m_yaw += d.x() * 0.008f;
    m_pitch = std::clamp(m_pitch + d.y() * 0.008f, -1.45f, 1.45f);
    update();
}

void BoardGLView::wheelEvent(QWheelEvent *e)
{
    const float factor = std::pow(0.9985f, float(e->angleDelta().y()));
    m_distance = std::clamp(m_distance * factor, 60.0f, 1600.0f);
    update();
}
