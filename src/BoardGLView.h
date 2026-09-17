#pragma once

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector>

class Simulation;

// Orbiting 3D view of the machine.
//
// The tray is a printed open box, so its solid is exactly the extracted 2D
// outline swept along the depth axis; the mesh here is built by extruding that
// outline rather than by loading any 3D model.  Spheres are drawn with one
// instanced icosphere.
class BoardGLView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    explicit BoardGLView(QWidget *parent = nullptr);
    ~BoardGLView() override;

    void setSimulation(const Simulation *sim);

    void setColourBySpeed(bool on) { m_colourBySpeed = on; update(); }
    void setShowTheory(bool on) { m_showTheory = on; update(); }
    void setShowCover(bool on) { m_showCover = on; update(); }
    void setShowLabels(bool on) { m_showLabels = on; update(); }

public slots:
    void resetView();
    void setFrontView();
    void setAngledView();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    QSize sizeHint() const override { return {1000, 560}; }

private:
    struct Mesh {
        QOpenGLVertexArrayObject vao;
        QOpenGLBuffer vbo {QOpenGLBuffer::VertexBuffer};
        int vertexCount = 0;
        void destroy();
    };

    void buildBodyMesh();
    void buildRotorMesh();
    void buildSliderMesh();
    void buildSphereMesh();
    void uploadMesh(Mesh &mesh, const QVector<float> &data);
    void drawMesh(Mesh &mesh, const QMatrix4x4 &model, const QVector3D &colour,
                  float alpha);
    void drawSpheres();
    void drawOverlay(QPainter &painter);

    QMatrix4x4 viewMatrix() const;

    const Simulation *m_sim = nullptr;

    QOpenGLShaderProgram m_solid;
    QOpenGLShaderProgram m_instanced;

    Mesh m_body;
    Mesh m_rotor;
    Mesh m_slider;
    Mesh m_cover;

    QOpenGLVertexArrayObject m_sphereVao;
    QOpenGLBuffer m_sphereVbo {QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer m_instanceVbo {QOpenGLBuffer::VertexBuffer};
    int m_sphereVerts = 0;
    QVector<float> m_instanceData;

    QMatrix4x4 m_proj;
    QMatrix4x4 m_mvp;

    QVector3D m_target;
    float m_distance = 400.0f;
    float m_yaw = 0.0f;         // radians, around +Y
    float m_pitch = 0.0f;       // radians
    QPoint m_lastMouse;

    bool m_ready = false;
    bool m_colourBySpeed = true;
    bool m_showTheory = true;
    bool m_showCover = true;
    bool m_showLabels = true;
};
