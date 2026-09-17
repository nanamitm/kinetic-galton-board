#include "MainWindow.h"
#include "BoardGLView.h"
#include "DistributionView.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {

QSlider *makeSlider(int lo, int hi, int value)
{
    auto *s = new QSlider(Qt::Horizontal);
    s->setRange(lo, hi);
    s->setValue(value);
    return s;
}

QLabel *makeValue(const QString &text)
{
    auto *l = new QLabel(text);
    l->setMinimumWidth(74);
    l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return l;
}

} // namespace

MainWindow::MainWindow(const QString &geometryPath, QWidget *parent)
    : QMainWindow(parent)
{
    QString err;
    if (!m_geo.load(geometryPath, &err)) {
        QMessageBox::critical(this, tr("Geometry"),
                              tr("Could not load the machine outline.\n\n%1\n\n"
                                 "Run tools/build_geometry.ps1 to regenerate it "
                                 "from the 3MF with Blender.")
                                  .arg(err));
    }
    m_sim.setGeometry(&m_geo);

    m_board = new BoardGLView;
    m_board->setSimulation(&m_sim);
    m_dist = new DistributionView;
    m_dist->setSimulation(&m_sim);

    auto *right = new QWidget;
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->addWidget(m_dist, 1);
    rl->addWidget(buildFacts(), 0);

    auto *split = new QSplitter(Qt::Vertical);
    split->addWidget(m_board);
    split->addWidget(right);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);

    auto *central = new QWidget;
    auto *cl = new QHBoxLayout(central);
    cl->setContentsMargins(8, 8, 8, 8);
    cl->addWidget(split, 1);
    cl->addWidget(buildControls(), 0);
    setCentralWidget(central);

    setWindowTitle(tr("Maxwell-Boltzmann Kinetic Galton Board - %1")
                       .arg(m_geo.source));
    resize(1420, 900);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &MainWindow::tick);
    m_clock.start();
    m_timer->start(16);
}

QWidget *MainWindow::buildControls()
{
    auto *panel = new QWidget;
    panel->setFixedWidth(320);
    auto *v = new QVBoxLayout(panel);
    v->setContentsMargins(4, 0, 0, 0);

    // ---- run control
    auto *runBox = new QGroupBox(tr("Run"));
    auto *runLayout = new QVBoxLayout(runBox);

    m_pause = new QPushButton(tr("Pause"));
    connect(m_pause, &QPushButton::clicked, this, &MainWindow::togglePause);
    runLayout->addWidget(m_pause);

    m_motor = new QPushButton(tr("Motor: on"));
    m_motor->setCheckable(true);
    m_motor->setChecked(true);
    connect(m_motor, &QPushButton::toggled, this, [this](bool on) {
        m_sim.params().motorOn = on;
        m_motor->setText(on ? tr("Motor: on") : tr("Motor: off"));
    });
    runLayout->addWidget(m_motor);

    m_slider = new QPushButton(tr("Drop the balls (pull slider)"));
    m_slider->setEnabled(m_geo.f.hasSlider);
    connect(m_slider, &QPushButton::clicked, this, [this] {
        const bool in = !m_sim.params().sliderIn;
        m_sim.params().sliderIn = in;
        m_slider->setText(in ? tr("Drop the balls (pull slider)")
                             : tr("Put the slider back"));
    });
    runLayout->addWidget(m_slider);

    auto *reset = new QPushButton(tr("Reset"));
    connect(reset, &QPushButton::clicked, this, &MainWindow::resetRun);
    runLayout->addWidget(reset);
    v->addWidget(runBox);

    // ---- machine parameters
    auto *machBox = new QGroupBox(tr("Machine"));
    auto *mf = new QFormLayout(machBox);

    m_rpm = makeSlider(200, 6000, int(m_sim.params().rpm));
    m_rpmLabel = makeValue(QString());
    auto applyRpm = [this](int v) {
        m_sim.params().rpm = v;
        m_rpmLabel->setText(tr("%1 rpm").arg(v));
    };
    connect(m_rpm, &QSlider::valueChanged, this, applyRpm);
    applyRpm(m_rpm->value());
    mf->addRow(m_rpmLabel, m_rpm);

    m_count = makeSlider(50, 800, m_sim.params().ballCount);
    m_countLabel = makeValue(QString());
    m_countLabel->setText(tr("%1 balls").arg(m_count->value()));
    connect(m_count, &QSlider::valueChanged, this, [this](int v) {
        m_sim.params().ballCount = v;
        m_countLabel->setText(tr("%1 balls").arg(v));
        resetRun();
    });
    mf->addRow(m_countLabel, m_count);

    m_restBall = makeSlider(30, 98, int(m_sim.params().ballRestitution * 100));
    m_restBallLabel = makeValue(QString());
    auto applyRestBall = [this](int v) {
        m_sim.params().ballRestitution = v / 100.0;
        m_restBallLabel->setText(tr("e ball %1").arg(v / 100.0, 0, 'f', 2));
    };
    connect(m_restBall, &QSlider::valueChanged, this, applyRestBall);
    applyRestBall(m_restBall->value());
    mf->addRow(m_restBallLabel, m_restBall);

    m_restWall = makeSlider(10, 95, int(m_sim.params().wallRestitution * 100));
    m_restWallLabel = makeValue(QString());
    auto applyRestWall = [this](int v) {
        m_sim.params().wallRestitution = v / 100.0;
        m_restWallLabel->setText(tr("e wall %1").arg(v / 100.0, 0, 'f', 2));
    };
    connect(m_restWall, &QSlider::valueChanged, this, applyRestWall);
    applyRestWall(m_restWall->value());
    mf->addRow(m_restWallLabel, m_restWall);
    v->addWidget(machBox);

    // ---- view
    auto *viewBox = new QGroupBox(tr("View"));
    auto *vf = new QFormLayout(viewBox);

    m_speed = makeSlider(10, 200, 100);
    m_speedLabel = makeValue(QString());
    auto applySpeed = [this](int v) {
        m_timeScale = v / 100.0;
        m_speedLabel->setText(tr("%1x time").arg(m_timeScale, 0, 'f', 2));
    };
    connect(m_speed, &QSlider::valueChanged, this, applySpeed);
    applySpeed(m_speed->value());
    vf->addRow(m_speedLabel, m_speed);

    m_heat = new QCheckBox(tr("Colour discs by speed"));
    m_heat->setChecked(true);
    connect(m_heat, &QCheckBox::toggled, m_board, &BoardGLView::setColourBySpeed);
    vf->addRow(m_heat);

    m_cover = new QCheckBox(tr("Show the acrylic cover"));
    m_cover->setChecked(true);
    connect(m_cover, &QCheckBox::toggled, m_board, &BoardGLView::setShowCover);
    vf->addRow(m_cover);

    auto *views = new QWidget;
    auto *vh = new QHBoxLayout(views);
    vh->setContentsMargins(0, 0, 0, 0);
    auto *front = new QPushButton(tr("Front"));
    auto *angled = new QPushButton(tr("Angled"));
    connect(front, &QPushButton::clicked, m_board, &BoardGLView::setFrontView);
    connect(angled, &QPushButton::clicked, m_board, &BoardGLView::setAngledView);
    vh->addWidget(front);
    vh->addWidget(angled);
    vf->addRow(views);

    m_theory = new QCheckBox(tr("Overlay Maxwell-Boltzmann"));
    m_theory->setChecked(true);
    connect(m_theory, &QCheckBox::toggled, this, [this](bool on) {
        m_board->setShowTheory(on);
        m_dist->setShowTheory(on);
    });
    vf->addRow(m_theory);
    v->addWidget(viewBox);

    v->addStretch(1);
    return panel;
}

QWidget *MainWindow::buildFacts()
{
    const Features &f = m_geo.f;
    auto *box = new QGroupBox(tr("Readout"));
    auto *v = new QVBoxLayout(box);

    auto *geom = new QLabel(
        tr("<b>Extracted from %1 with Blender %2</b><br>"
           "chamber %3 mm, %4 balls &nbsp;|&nbsp; ball &oslash; %5 mm<br>"
           "effusion gap %6 mm (%7 ball &oslash;) at y = %8<br>"
           "%9 bins, pitch %10 mm &nbsp;|&nbsp; drop to fin tips %11 mm<br>"
           "fall time %12 ms - a bin at &Delta;x mm reads v = &Delta;x / t")
            .arg(m_geo.source, m_geo.blenderVersion)
            .arg(QStringLiteral("%1 x %2 x %3")
                     .arg(f.rightWallX - f.leftWallX, 0, 'f', 1)
                     .arg(f.ceilingY - f.floorY, 0, 'f', 1)
                     .arg(m_geo.interiorDepth(), 0, 'f', 1))
            .arg(QStringLiteral("%1 deep").arg(m_sim.depthLayers()))
            .arg(m_geo.ballDiameter, 0, 'f', 1)
            .arg(f.effusionGap, 0, 'f', 2)
            .arg(f.effusionGap / m_geo.ballDiameter, 0, 'f', 2)
            .arg(f.dividerTopY, 0, 'f', 2)
            .arg(f.bins.size())
            .arg(f.finPitch, 0, 'f', 1)
            .arg(f.dropHeight, 0, 'f', 1)
            .arg(m_sim.fallTime() * 1000.0, 0, 'f', 1));
    geom->setWordWrap(true);
    v->addWidget(geom);

    m_readout = new QLabel;
    m_readout->setTextFormat(Qt::RichText);
    v->addWidget(m_readout);
    return box;
}

void MainWindow::tick()
{
    const double elapsed = m_clock.restart() / 1000.0;
    if (m_running)
        m_sim.advance(std::min(0.05, elapsed) * m_timeScale);
    m_board->update();
    m_dist->update();
    refreshReadout();
}

void MainWindow::refreshReadout()
{
    const SimStats &s = m_sim.stats();
    int binned = 0;
    for (int c : m_sim.binCounts())
        binned += c;

    double sigma = 0.0;
    double vRms = 0.0;
    const bool fitted = m_sim.fitDistribution(&sigma, &vRms);

    m_readout->setText(
        tr("in chamber <b>%1</b> &nbsp; effused <b>%2</b> &nbsp; binned <b>%3</b><br>"
           "chamber gas v_rms <b>%4 m/s</b> &nbsp; mean <b>%5 m/s</b><br>"
           "binned distribution %6<br>"
           "simulated time <b>%7 s</b>")
            .arg(s.inChamber)
            .arg(s.escaped)
            .arg(binned)
            .arg(s.rmsSpeed / 1000.0, 0, 'f', 2)
            .arg(s.meanSpeed / 1000.0, 0, 'f', 2)
            .arg(fitted ? tr("v_rms <b>%1 m/s</b>, most probable <b>%2 m/s</b>")
                              .arg(vRms / 1000.0, 0, 'f', 2)
                              .arg(sigma / 1000.0, 0, 'f', 2)
                        : tr("&mdash;"))
            .arg(s.simTime, 0, 'f', 1));
}

void MainWindow::resetRun()
{
    m_sim.params().sliderIn = true;
    m_sim.reset();
    if (m_slider)
        m_slider->setText(tr("Drop the balls (pull slider)"));
    m_clock.restart();
}

void MainWindow::togglePause()
{
    m_running = !m_running;
    m_pause->setText(m_running ? tr("Pause") : tr("Resume"));
    m_clock.restart();
}
