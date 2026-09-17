#include "MainWindow.h"
#include "Simulation.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QPalette>
#include <QSurfaceFormat>
#include <QTextStream>
#include <cmath>

namespace {

void applyDarkPalette(QApplication &app)
{
    app.setStyle(QStringLiteral("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window, QColor(0x1c, 0x1f, 0x26));
    p.setColor(QPalette::WindowText, QColor(0xdd, 0xe2, 0xea));
    p.setColor(QPalette::Base, QColor(0x14, 0x16, 0x1b));
    p.setColor(QPalette::AlternateBase, QColor(0x22, 0x26, 0x2f));
    p.setColor(QPalette::Text, QColor(0xdd, 0xe2, 0xea));
    p.setColor(QPalette::Button, QColor(0x2a, 0x2f, 0x39));
    p.setColor(QPalette::ButtonText, QColor(0xdd, 0xe2, 0xea));
    p.setColor(QPalette::Highlight, QColor(0x4f, 0xc3, 0xf7));
    p.setColor(QPalette::HighlightedText, QColor(0x10, 0x13, 0x18));
    p.setColor(QPalette::ToolTipBase, QColor(0x2a, 0x2f, 0x39));
    p.setColor(QPalette::ToolTipText, QColor(0xdd, 0xe2, 0xea));
    app.setPalette(p);
}

// Prefer a geometry.json sitting next to the binary or in the source tree so a
// re-run of the Blender pipeline is picked up without rebuilding; fall back to
// the copy baked into the resources.
QString findGeometry(const QString &override_)
{
    if (!override_.isEmpty())
        return override_;
    const QDir exeDir(QCoreApplication::applicationDirPath());
    const QStringList candidates {
        exeDir.filePath(QStringLiteral("assets/geometry.json")),
        exeDir.filePath(QStringLiteral("../assets/geometry.json")),
        exeDir.filePath(QStringLiteral("../../assets/geometry.json")),
    };
    for (const QString &c : candidates) {
        if (QFileInfo::exists(c))
            return c;
    }
    return QStringLiteral(":/assets/geometry.json");
}

// Run the model with no window at all and print what the bins hold.  Used to
// check the physics without driving the UI.
int runHeadless(const QString &geometryPath, double seconds, const SimParams &prm)
{
    QTextStream out(stdout);
    Geometry geo;
    QString err;
    if (!geo.load(geometryPath, &err)) {
        out << "geometry: " << err << Qt::endl;
        return 2;
    }
    if (qEnvironmentVariableIsSet("KGB_ROTOR_FULL_DEPTH")) {
        // Control experiment: pretend the rotor spans the whole chamber depth.
        geo.agitatorZ0 = geo.interiorZ0;
        geo.agitatorZ1 = geo.interiorZ1;
        out << "NOTE       rotor depth span forced to the full interior"
            << Qt::endl;
    }
    Simulation sim;
    sim.params() = prm;
    sim.setGeometry(&geo);
    sim.params().sliderIn = false;          // balls go straight onto the rotor

    const int steps = int(seconds / Simulation::kFixedDt);
    for (int i = 0; i < steps; ++i)
        sim.step(Simulation::kFixedDt);
    sim.advance(0.0);

    const SimStats &st = sim.stats();
    out << "geometry   " << geo.source << "  bins=" << geo.f.bins.size()
        << "  gap=" << geo.f.effusionGap << " mm  drop=" << geo.f.dropHeight
        << " mm  t_fall=" << sim.fallTime() * 1000.0 << " ms" << Qt::endl;
    out << "interior   " << geo.interiorDepth() << " mm deep = "
        << sim.depthLayers() << " ball layers" << Qt::endl;
    out << "run        " << seconds << " s at " << prm.rpm << " rpm, "
        << sim.balls().size() << " spheres" << Qt::endl;
    out << "state      chamber=" << st.inChamber << "  effused=" << st.escaped
        << "  v_rms=" << st.rmsSpeed / 1000.0 << " m/s" << Qt::endl;

    int total = 0;
    int peak = 1;
    for (int c : sim.binCounts()) {
        total += c;
        peak = std::max(peak, c);
    }
    {   // Are any spheres actually inside a wall?  A resting one should sit
        // at exactly +radius from the nearest surface.
        double worst = 1e18;
        int overlapping = 0;
        for (const Ball &b : sim.balls()) {
            const double clearance = sim.signedWallDistance(b.p) - sim.ballRadius();
            worst = std::min(worst, clearance);
            if (clearance < -0.10)
                ++overlapping;
        }
        out << "walls      worst clearance " << worst
            << " mm, spheres inside a wall by >0.1 mm: " << overlapping << Qt::endl;
        if (qEnvironmentVariableIsSet("KGB_DUMP_STUCK")) {
            int shown = 0;
            for (const Ball &b : sim.balls()) {
                const double clearance = sim.signedWallDistance(b.p) - sim.ballRadius();
                if (clearance >= -0.10 || shown++ >= 25)
                    continue;
                out << QStringLiteral("   stuck (%1, %2, %3) clr %4 v %5 %6")
                           .arg(b.p.x, 8, 'f', 2).arg(b.p.y, 8, 'f', 2)
                           .arg(b.p.z, 7, 'f', 2).arg(clearance, 7, 'f', 2)
                           .arg(b.v.length(), 8, 'f', 1)
                           .arg(b.escaped ? "escaped" : "chamber")
                    << Qt::endl;
            }
        }

        // Of the ones sitting still, how many are simply out of the rotor's
        // reach?  It sweeps a 15 mm disc and only part of the depth.
        int rest = 0, restOutXY = 0, restOutZ = 0;
        for (const Ball &b : sim.balls()) {
            if (b.escaped || b.v.length() >= 50.0)
                continue;
            ++rest;
            const bool inZ = b.p.z >= geo.agitatorZ0 - sim.ballRadius()
                          && b.p.z <= geo.agitatorZ1 + sim.ballRadius();
            if (!inZ)
                ++restOutZ;
            if (!sim.withinRotorReach(b.p) && inZ)
                ++restOutXY;
        }
        out << "at rest    " << rest << " spheres; beyond the rotor's disc "
            << restOutXY << ", beyond its depth span " << restOutZ << Qt::endl;
        for (const Ball &b : sim.balls()) {
            if (b.escaped || b.v.length() >= 50.0)
                continue;
            const double dx = b.p.x - geo.agitatorCentre.x();
            const double dy = b.p.y - geo.agitatorCentre.y();
            const double rr = std::hypot(dx, dy);
            out << QStringLiteral("   (%1,%2,%3)  r_axis %4 (reach %5)  z span [%6,%7]  %8")
                       .arg(b.p.x, 8, 'f', 2).arg(b.p.y, 7, 'f', 2).arg(b.p.z, 6, 'f', 2)
                       .arg(rr, 6, 'f', 2)
                       .arg(geo.agitatorRadius + sim.ballRadius(), 5, 'f', 1)
                       .arg(geo.agitatorZ0, 5, 'f', 1).arg(geo.agitatorZ1, 5, 'f', 1)
                       .arg(sim.withinRotorReach(b.p) ? "REACHABLE" : "out of reach")
                << Qt::endl;
        }
    }

    {   // where did the spheres that never effused end up?
        double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9, z0 = 1e9, z1 = -1e9;
        int slow = 0;
        for (const Ball &b : sim.balls()) {
            if (b.escaped)
                continue;
            x0 = std::min(x0, b.p.x); x1 = std::max(x1, b.p.x);
            y0 = std::min(y0, b.p.y); y1 = std::max(y1, b.p.y);
            z0 = std::min(z0, b.p.z); z1 = std::max(z1, b.p.z);
            if (b.v.length() < 50.0)
                ++slow;
        }
        out << "chamber    x[" << x0 << "," << x1 << "] y[" << y0 << "," << y1
            << "] z[" << z0 << "," << z1 << "]  at rest=" << slow << Qt::endl;
    }
    // The speed each sphere actually effused with, histogrammed on the same
    // edges as the bins.  If this is smooth while the landings are not, the
    // spheres are being redirected after they leave the hole.
    QVector<int> want(geo.f.bins.size(), 0);
    for (double v : sim.escapeSpeeds()) {
        const double x = geo.f.dividerX1 + v * sim.fallTime();
        for (int i = 0; i < geo.f.bins.size(); ++i) {
            if (x >= geo.f.bins[i].x0 && x < geo.f.bins[i].x1) {
                ++want[i];
                break;
            }
        }
    }
    int wantPeak = 1;
    for (int c : want)
        wantPeak = std::max(wantPeak, c);

    out << "bins       " << total << " spheres binned "
        << "(left column = where the escape speed says it should land)" << Qt::endl;
    for (int i = 0; i < sim.binCounts().size(); ++i) {
        const double v = (geo.f.bins[i].centre() - geo.f.dividerX1) / sim.fallTime();
        const int n = sim.binCounts()[i];
        out << QStringLiteral("  %1  %2 m/s  predicted %3 %4 | landed %5 %6")
                   .arg(i + 1, 2)
                   .arg(v / 1000.0, 5, 'f', 2)
                   .arg(want[i], 4)
                   .arg(QString(QChar(0x2588)).repeated(want[i] * 22 / wantPeak), -22)
                   .arg(n, 4)
                   .arg(QString(QChar(0x2588)).repeated(n * 22 / peak))
            << Qt::endl;
    }
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("KineticGaltonBoard"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));
    app.setWindowIcon(QIcon(QStringLiteral(":/assets/app.ico")));
    applyDarkPalette(app);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Simulator for the Maxwell-Boltzmann kinetic Galton board, "
                       "built on the outline extracted from the printable 3MF."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("geometry"),
                                 QStringLiteral("geometry.json to load (optional)"));
    QCommandLineOption headless(QStringLiteral("headless"),
                                QStringLiteral("Run <seconds> of physics with no window "
                                               "and print the bin histogram."),
                                QStringLiteral("seconds"));
    QCommandLineOption rpmOpt(QStringLiteral("rpm"),
                              QStringLiteral("Agitator speed for --headless."),
                              QStringLiteral("rpm"), QStringLiteral("2600"));
    QCommandLineOption ballsOpt(QStringLiteral("balls"),
                                QStringLiteral("Sphere count for --headless."),
                                QStringLiteral("n"), QStringLiteral("400"));
    QCommandLineOption ewallOpt(QStringLiteral("ewall"),
                                QStringLiteral("Wall restitution for --headless."),
                                QStringLiteral("e"), QStringLiteral("0.55"));
    QCommandLineOption eballOpt(QStringLiteral("eball"),
                                QStringLiteral("Sphere restitution for --headless."),
                                QStringLiteral("e"), QStringLiteral("0.80"));
    parser.addOption(ewallOpt);
    parser.addOption(eballOpt);
    parser.addOption(headless);
    parser.addOption(rpmOpt);
    parser.addOption(ballsOpt);
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (parser.isSet(headless)) {
        SimParams prm;
        prm.rpm = parser.value(rpmOpt).toDouble();
        prm.ballCount = parser.value(ballsOpt).toInt();
        prm.wallRestitution = parser.value(ewallOpt).toDouble();
        prm.ballRestitution = parser.value(eballOpt).toDouble();
        return runHeadless(findGeometry(args.value(0)),
                           parser.value(headless).toDouble(), prm);
    }

    MainWindow w(findGeometry(args.value(0)));
    w.show();
    return app.exec();
}
