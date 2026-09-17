#pragma once

#include <QPointF>
#include <QString>
#include <QVector>

// Everything the simulator knows about the machine's shape, as produced by
// tools/blender_extract.py from MaxwellBoltzmann775.3mf.  Millimetres, +Y up,
// origin at the centre of the body.

struct Fin {
    double x0 = 0.0;
    double x1 = 0.0;
    double topY = 0.0;
};

struct Bin {
    double x0 = 0.0;
    double x1 = 0.0;
    double centre() const { return 0.5 * (x0 + x1); }
    double width() const { return x1 - x0; }
};

struct Features {
    double floorY = 0.0;
    double ceilingY = 0.0;
    double leftWallX = 0.0;
    double rightWallX = 0.0;

    double dividerX0 = 0.0;      // wall between the gas chamber and the bins
    double dividerX1 = 0.0;
    double dividerTopY = 0.0;
    double effusionGap = 0.0;    // clear height of the escape hole

    double finTopY = 0.0;        // top of the bin dividers
    double finPitch = 0.0;
    double dropHeight = 0.0;     // dividerTopY - finTopY

    double sliderSlotY0 = 0.0;
    double sliderSlotY1 = 0.0;
    bool hasSlider = false;

    QVector<Fin> fins;
    QVector<Bin> bins;

    double sliderY() const { return 0.5 * (sliderSlotY0 + sliderSlotY1); }
    double sliderHalfThickness() const { return 0.5 * (sliderSlotY1 - sliderSlotY0); }
};

struct Geometry {
    QString source;
    QString blenderVersion;
    double ballDiameter = 4.0;
    QPointF bboxMin, bboxMax;
    double depthMin = -12.0, depthMax = 12.0;   // outer extent along the print axis
    double interiorZ0 = -5.0, interiorZ1 = 12.0; // where a ball may actually be

    QVector<QVector<QPointF>> rings;    // closed outlines of the body cross-section
    QVector<QPointF> agitatorProfile;   // rotor outline, centred on its own axis
    QPointF agitatorCentre;
    double agitatorRadius = 0.0;
    double agitatorHubRadius = 0.0;
    double agitatorZ0 = -5.0, agitatorZ1 = 3.0;  // rotor extent in body coords

    double interiorDepth() const { return interiorZ1 - interiorZ0; }

    Features f;

    bool load(const QString &path, QString *error = nullptr);
};
