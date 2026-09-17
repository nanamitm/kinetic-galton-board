#pragma once

#include "Geometry.h"
#include "Simulation.h"

#include <QElapsedTimer>
#include <QMainWindow>

class BoardGLView;
class DistributionView;
class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const QString &geometryPath, QWidget *parent = nullptr);

private slots:
    void tick();
    void resetRun();
    void togglePause();

private:
    QWidget *buildControls();
    QWidget *buildFacts();
    void refreshReadout();

    Geometry m_geo;
    Simulation m_sim;

    BoardGLView *m_board = nullptr;
    DistributionView *m_dist = nullptr;
    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock;

    QSlider *m_rpm = nullptr;
    QSlider *m_count = nullptr;
    QSlider *m_speed = nullptr;
    QSlider *m_restBall = nullptr;
    QSlider *m_restWall = nullptr;
    QPushButton *m_pause = nullptr;
    QPushButton *m_slider = nullptr;
    QPushButton *m_motor = nullptr;
    QCheckBox *m_theory = nullptr;
    QCheckBox *m_heat = nullptr;
    QCheckBox *m_cover = nullptr;

    QLabel *m_readout = nullptr;
    QLabel *m_rpmLabel = nullptr;
    QLabel *m_countLabel = nullptr;
    QLabel *m_speedLabel = nullptr;
    QLabel *m_restBallLabel = nullptr;
    QLabel *m_restWallLabel = nullptr;

    bool m_running = true;
    double m_timeScale = 1.0;
};
