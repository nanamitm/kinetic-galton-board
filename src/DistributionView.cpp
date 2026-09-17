#include "DistributionView.h"
#include "Simulation.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <cmath>

DistributionView::DistributionView(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(180);
}

void DistributionView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(0x14, 0x16, 0x1b));
    if (!m_sim || !m_sim->geometry())
        return;

    const Features &f = m_sim->geometry()->f;
    const QVector<int> &counts = m_sim->binCounts();
    if (counts.isEmpty())
        return;

    const QRectF plot = QRectF(rect()).adjusted(44, 14, -12, -30);
    if (plot.width() < 40 || plot.height() < 40)
        return;

    int maxCount = 1;
    for (int c : counts)
        maxCount = std::max(maxCount, c);
    const int yTop = std::max(4, int(std::ceil(maxCount * 1.25)));

    const double x0 = f.dividerX1;
    const double tFall = std::max(1e-9, m_sim->fallTime());
    const double vMin = (f.bins.first().x0 - x0) / tFall;
    const double vMax = (f.bins.last().x1 - x0) / tFall;

    auto sx = [&](double v) {
        return plot.left() + plot.width() * (v - vMin) / std::max(1e-9, vMax - vMin);
    };
    auto sy = [&](double n) {
        return plot.bottom() - plot.height() * n / yTop;
    };

    // ---- axes and gridlines
    p.setPen(QPen(QColor(0x2c, 0x32, 0x3d), 1));
    const int steps = 4;
    QFont fnt = font();
    fnt.setPointSizeF(8.0);
    p.setFont(fnt);
    for (int i = 0; i <= steps; ++i) {
        const double n = yTop * double(i) / steps;
        const double y = sy(n);
        p.setPen(QPen(QColor(0x25, 0x2a, 0x34), 1));
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(QColor(0x7e, 0x8a, 0x9c));
        p.drawText(QRectF(2, y - 8, 38, 16), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(int(std::round(n))));
    }

    // ---- measured bins
    for (int i = 0; i < counts.size(); ++i) {
        const double a = sx((f.bins[i].x0 - x0) / tFall);
        const double b = sx((f.bins[i].x1 - x0) / tFall);
        const QRectF bar(a + 1.0, sy(counts[i]), std::max(1.0, b - a - 2.0),
                         plot.bottom() - sy(counts[i]));
        p.fillRect(bar, QColor(0x4f, 0xc3, 0xf7, 190));
    }

    // ---- fitted Maxwell-Boltzmann effusion curve
    double sigma = 0.0;
    double vRms = 0.0;
    if (m_showTheory && m_sim->fitDistribution(&sigma, &vRms)) {
        // scale the density so its integral matches the discs actually binned
        int binned = 0;
        for (int c : counts)
            binned += c;
        const double binWidth = (f.bins.first().x1 - f.bins.first().x0) / tFall;
        const double norm = binned * binWidth / (sigma * sigma);

        QPainterPath curve;
        const int N = 160;
        for (int i = 0; i <= N; ++i) {
            const double v = vMin + (vMax - vMin) * i / N;
            const double y = norm * v * std::exp(-0.5 * v * v / (sigma * sigma));
            const QPointF pt(sx(v), sy(y));
            i == 0 ? curve.moveTo(pt) : curve.lineTo(pt);
        }
        p.setPen(QPen(QColor(0xff, 0xd5, 0x4f), 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawPath(curve);

        p.setPen(QColor(0xff, 0xd5, 0x4f));
        p.drawText(plot.adjusted(0, 0, -4, 0), Qt::AlignRight | Qt::AlignTop,
                   QStringLiteral("ideal effusion  v e^(-v%1/2s%1)   "
                                  "v_rms = %2 m/s   v_peak = %3 m/s")
                       .arg(QChar(0x00B2))
                       .arg(vRms / 1000.0, 0, 'f', 2)
                       .arg(sigma / 1000.0, 0, 'f', 2));
    }

    // ---- frame and x axis
    p.setPen(QPen(QColor(0x3a, 0x42, 0x50), 1));
    p.drawLine(plot.bottomLeft(), plot.bottomRight());
    p.drawLine(plot.topLeft(), plot.bottomLeft());
    p.setPen(QColor(0x7e, 0x8a, 0x9c));
    for (int i = 0; i <= 5; ++i) {
        const double v = vMin + (vMax - vMin) * i / 5.0;
        p.drawText(QRectF(sx(v) - 24, plot.bottom() + 3, 48, 14),
                   Qt::AlignHCenter | Qt::AlignTop,
                   QString::number(v / 1000.0, 'f', 1));
    }
    p.drawText(QRectF(plot.left(), plot.bottom() + 15, plot.width(), 14),
               Qt::AlignHCenter | Qt::AlignTop,
               QStringLiteral("escape speed  [m/s]   (bin position / fall time)"));
}
