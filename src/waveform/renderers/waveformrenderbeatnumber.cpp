#include "waveform/renderers/waveformrenderbeatnumber.h"

#include <QPainter>
#include <QPen>

#include "track/track.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "widget/wskincolor.h"

WaveformRenderBeatNumber::WaveformRenderBeatNumber(WaveformWidgetRenderer* waveformWidget)
        : WaveformRendererSignalBase(waveformWidget) {
}

void WaveformRenderBeatNumber::setup(const QDomNode& node, const SkinContext& context) {
    Q_UNUSED(node);
    Q_UNUSED(context);

    m_font.setPixelSize(10);
    m_font.setBold(true);
    m_textColor = QColor(255, 255, 255, 220); // White with transparency
}

void WaveformRenderBeatNumber::draw(QPainter* painter, QPaintEvent* event) {
    Q_UNUSED(event);

    TrackPointer pTrackInfo = m_waveformRenderer->getTrackInfo();
    if (!pTrackInfo) {
        return;
    }

    mixxx::BeatsPointer trackBeats = pTrackInfo->getBeats();
    if (!trackBeats) {
        return;
    }

    const double trackSamples = m_waveformRenderer->getTrackSamples();
    if (trackSamples <= 0) {
        return;
    }

    const double firstDisplayedPosition =
            m_waveformRenderer->getFirstDisplayedPosition();
    const double lastDisplayedPosition =
            m_waveformRenderer->getLastDisplayedPosition();

    const auto startPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            firstDisplayedPosition * trackSamples);
    const auto endPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            lastDisplayedPosition * trackSamples);

    auto it = trackBeats->iteratorFrom(startPosition);
    if (it == trackBeats->cend() || *it > endPosition) {
        return;
    }

    // Get first beat for calculating beat numbers
    auto firstBeatPos = trackBeats->firstBeat();
    auto firstBeatIterator = trackBeats->iteratorFrom(firstBeatPos);

    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();
    const Qt::Orientation orientation = m_waveformRenderer->getOrientation();

    PainterScope painterScope(painter);
    painter->setFont(m_font);
    painter->setPen(m_textColor);

    // Draw beat numbers
    for (; it != trackBeats->cend() && *it <= endPosition; ++it) {
        double beatPosition = it->toEngineSamplePos();
        double xBeatPoint =
                m_waveformRenderer->transformSamplePositionInRendererWorld(beatPosition);

        xBeatPoint = qRound(xBeatPoint * devicePixelRatio) / devicePixelRatio;

        // CUSTOM: Calculate total beat number from start (Rekordbox style)
        // Instead of cycling 1-4, show total beat count: 1, 2, 3... 780, 781...
        int beatIndexFromFirst = it - firstBeatIterator;
        int beatNumber = beatIndexFromFirst + 1; // Start counting from 1

        QString beatText = QString::number(beatNumber);

        if (orientation == Qt::Horizontal) {
            // Draw number at top of waveform, centered on beat line
            QRectF textRect(xBeatPoint - 10, 2, 20, 12);
            painter->drawText(textRect, Qt::AlignCenter, beatText);
        } else {
            // Vertical orientation
            QRectF textRect(2, xBeatPoint - 6, 12, 12);
            painter->drawText(textRect, Qt::AlignCenter, beatText);
        }
    }
}
