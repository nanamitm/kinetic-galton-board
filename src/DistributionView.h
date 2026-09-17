#pragma once

#include <QWidget>

class Simulation;

// Bin occupancy plotted against the speed each bin corresponds to, with the
// Maxwell-Boltzmann curve the machine is supposed to trace laid over it.
class DistributionView : public QWidget
{
    Q_OBJECT
public:
    explicit DistributionView(QWidget *parent = nullptr);

    void setSimulation(const Simulation *sim) { m_sim = sim; update(); }
    void setShowTheory(bool on) { m_showTheory = on; update(); }

protected:
    void paintEvent(QPaintEvent *) override;
    QSize sizeHint() const override { return {420, 260}; }

private:
    const Simulation *m_sim = nullptr;
    bool m_showTheory = true;
};
