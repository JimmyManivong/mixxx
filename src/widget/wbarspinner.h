#pragma once

#include <QColor>
#include <QString>

#include "control/controlproxy.h"
#include "widget/wwidget.h"

class QDomNode;
class SkinContext;

/// CUSTOM (Rekordbox-style jog marker): a transparent overlay that draws a red
/// marker rotating around the BPM badge. One full revolution corresponds to one
/// bar (4 beats). A fixed notch at the top (12 o'clock) marks the start of the
/// bar: each time the marker passes it, one bar has elapsed.
///
/// The rotation is driven by the engine controls:
///   [ChannelN],beat_distance  fraction within the current beat
///   [ChannelN],beat_number    1-based beat index from the first beat
/// so the marker is locked to the beatgrid, not to vinyl RPM.
class WBarSpinner : public WWidget {
    Q_OBJECT
  public:
    WBarSpinner(QWidget* pParent, const QString& group);

    void setup(const QDomNode& node, const SkinContext& context);

  protected:
    void paintEvent(QPaintEvent* e) override;

  private slots:
    void slotValueChanged(double v);

  private:
    QString m_group;
    ControlProxy m_beatDistance;
    ControlProxy m_beatNumber;

    QColor m_markerColor;
    QColor m_notchColor;

    // Fixed clock angle of the red marker, in degrees clockwise from 12 o'clock.
    // Randomised once per deck within the 1 o'clock..5 o'clock range (30..150),
    // like Rekordbox where it sits at an arbitrary spot below-right.
    double m_markerAngleDeg;
};
