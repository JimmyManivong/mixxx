#include "widget/wbarspinner.h"

#include <QDomNode>
#include <QPainter>
#include <QRandomGenerator>
#include <cmath>

#include "skin/legacy/skincontext.h"
#include "moc_wbarspinner.cpp"

namespace {
constexpr int kBeatsPerBar = 4;
} // namespace

WBarSpinner::WBarSpinner(QWidget* pParent, const QString& group)
        : WWidget(pParent),
          m_group(group),
          m_beatDistance(group, QStringLiteral("beat_distance"), this),
          m_beatNumber(group, QStringLiteral("beat_number"), this),
          m_markerColor(QColor(0xff, 0x2d, 0x2d)),
          m_notchColor(QColor(0xff, 0xff, 0xff)),
          // 1 o'clock (30 deg) .. 5 o'clock (150 deg), picked once at random.
          m_markerAngleDeg(30.0 + QRandomGenerator::global()->bounded(120.0)) {
    // The overlay only paints the marker/notch; clicks go through to the badge.
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);

    // Repaint whenever the playback phase advances. beat_distance changes every
    // engine buffer while playing, giving a smooth rotation.
    m_beatDistance.connectValueChanged(this, &WBarSpinner::slotValueChanged);
    m_beatNumber.connectValueChanged(this, &WBarSpinner::slotValueChanged);
}

void WBarSpinner::setup(const QDomNode& node, const SkinContext& context) {
    QString color;
    if (context.hasNodeSelectString(node, "MarkerColor", &color)) {
        m_markerColor = QColor(color);
    }
    if (context.hasNodeSelectString(node, "NotchColor", &color)) {
        m_notchColor = QColor(color);
    }
}

void WBarSpinner::slotValueChanged(double v) {
    Q_UNUSED(v);
    update();
}

void WBarSpinner::paintEvent(QPaintEvent* /*e*/) {
    // beat_distance is the engine's normal convention here (0.0 = on beat,
    // 1.0 = almost at the next beat), i.e. already the forward progress
    // within a beat - no inversion needed. (It used to be inverted upstream
    // in BpmControl for this widget's benefit, but that corrupted the value
    // the sync engine relies on; see the CUSTOM comment in
    // BpmControl::updateBeatDistance.)
    const double fraction = m_beatDistance.get();
    const int beatNumber = static_cast<int>(m_beatNumber.get());
    const int beatInBar = beatNumber > 0 ? (beatNumber - 1) % kBeatsPerBar : 0;

    double phase = (beatInBar + fraction) / static_cast<double>(kBeatsPerBar);
    if (phase < 0.0) {
        phase = 0.0;
    } else if (phase >= 1.0) {
        phase -= std::floor(phase);
    }
    // Bar phase as a clockwise angle from 12 o'clock (degrees).
    const double angleDeg = phase * 360.0;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const double cx = width() / 2.0;
    const double cy = height() / 2.0;
    const double radius = qMin(width(), height()) / 2.0 - 4.0;
    const QRectF box(cx - radius, cy - radius, 2.0 * radius, 2.0 * radius);

    // The red marker is the bar origin: at the start of a bar (phase 0) the
    // notch sits around it. The notch is a bit wider than the marker so the red
    // keeps a small margin inside the gap.
    // Notch a touch wider than the marker so that at the origin (phase 0) the
    // red nearly fills the gap, leaving a tiny sliver each side: the ring looks
    // almost closed but not quite.
    constexpr double kMarkerDeg = 14.0; // red tab width
    constexpr double kGapDeg = 18.0;    // notch width (> marker -> small margin)

    // Rotating white ring with a notch (gap). The gap is what visually turns:
    // it starts around the red marker and, when it comes back to it, one bar has
    // elapsed. Qt arc angles: 0 deg at 3 o'clock, CCW positive, 1/16 deg units.
    // Clockwise-from-top angle a -> Qt angle (90 - a).
    // Origin = the red marker: at phase 0 the notch is centered on (facing) the
    // red. It then rotates clockwise and comes back to it one bar later.
    const double gapCenterDeg = m_markerAngleDeg + angleDeg;
    const double gapCenterQt = 90.0 - gapCenterDeg;
    const double startQt = gapCenterQt + kGapDeg / 2.0;
    const double spanQt = -(360.0 - kGapDeg); // remainder of the ring, clockwise
    QPen ringPen(m_notchColor, 2.0, Qt::SolidLine, Qt::FlatCap);
    painter.setPen(ringPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(box,
            static_cast<int>(std::lround(startQt * 16.0)),
            static_cast<int>(std::lround(spanQt * 16.0)));

    // Fixed red reference marker at its random clock position (1h..5h).
    QPen markerPen(m_markerColor, 3.0, Qt::SolidLine, Qt::FlatCap);
    painter.setPen(markerPen);
    painter.setBrush(Qt::NoBrush);
    const double markerCenterQt = 90.0 - m_markerAngleDeg; // clockwise from top
    const double markerStartQt = markerCenterQt - kMarkerDeg / 2.0;
    painter.drawArc(box,
            static_cast<int>(std::lround(markerStartQt * 16.0)),
            static_cast<int>(std::lround(kMarkerDeg * 16.0)));
}
