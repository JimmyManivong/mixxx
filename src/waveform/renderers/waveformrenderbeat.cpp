#include "waveform/renderers/waveformrenderbeat.h"

#include <QPainter>

#include "track/track.h"
#include "util/painterscope.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "widget/wskincolor.h"

class QPaintEvent;

WaveformRenderBeat::WaveformRenderBeat(WaveformWidgetRenderer* waveformWidgetRenderer)
        : WaveformRendererAbstract(waveformWidgetRenderer) {
    m_beats.resize(128);
    m_downbeats.resize(32);  // Fewer downbeats than total beats
}

WaveformRenderBeat::~WaveformRenderBeat() {
}

void WaveformRenderBeat::setup(const QDomNode& node, const SkinContext& context) {
    m_beatColor = QColor(context.selectString(node, "BeatColor"));
    m_beatColor = WSkinColor::getCorrectColor(m_beatColor).toRgb();

    // CUSTOM: Read BeatHighlightColor for downbeats (Rekordbox style)
    QString highlightColorStr = context.selectString(node, "BeatHighlightColor");
    if (!highlightColorStr.isEmpty()) {
        m_beatHighlightColor = QColor(highlightColorStr);
        m_beatHighlightColor = WSkinColor::getCorrectColor(m_beatHighlightColor).toRgb();
    } else {
        // Default to red if not specified
        m_beatHighlightColor = QColor("#FF0000");
    }
}

void WaveformRenderBeat::draw(QPainter* painter, QPaintEvent* /*event*/) {
    TrackPointer pTrackInfo = m_waveformRenderer->getTrackInfo();

    if (!pTrackInfo) {
        return;
    }

    mixxx::BeatsPointer trackBeats = pTrackInfo->getBeats();
    if (!trackBeats) {
        return;
    }

    int alpha = m_waveformRenderer->getBeatGridAlpha();
    if (alpha == 0) {
        return;
    }
#ifdef MIXXX_USE_QOPENGL
    // Using alpha transparency with drawLines causes a graphical issue when
    // drawing with QPainter on the QOpenGLWindow: instead of individual lines
    // a large rectangle encompassing all beatlines is drawn.
    m_beatColor.setAlphaF(1.f);
#else
    m_beatColor.setAlphaF(alpha/100.0);
#endif

    const double trackSamples = m_waveformRenderer->getTrackSamples();
    if (trackSamples <= 0) {
        return;
    }

    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();

    const double firstDisplayedPosition =
            m_waveformRenderer->getFirstDisplayedPosition();
    const double lastDisplayedPosition =
            m_waveformRenderer->getLastDisplayedPosition();

    // qDebug() << "trackSamples" << trackSamples
    //          << "firstDisplayedPosition" << firstDisplayedPosition
    //          << "lastDisplayedPosition" << lastDisplayedPosition;

    const auto startPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            firstDisplayedPosition * trackSamples);
    const auto endPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            lastDisplayedPosition * trackSamples);
    auto it = trackBeats->iteratorFrom(startPosition);

    // if no beat do not waste time saving/restoring painter
    if (it == trackBeats->cend() || *it > endPosition) {
        return;
    }

    PainterScope PainterScope(painter);

    painter->setRenderHint(QPainter::Antialiasing);

    const Qt::Orientation orientation = m_waveformRenderer->getOrientation();
    const float rendererWidth = m_waveformRenderer->getWidth();
    const float rendererHeight = m_waveformRenderer->getHeight();

    int beatCount = 0;
    int downbeatCount = 0;
    int beatIndexInBar = 0;  // Track position in 4/4 measure

    // CUSTOM: Separate beats and downbeats for different rendering (Rekordbox style)
    for (; it != trackBeats->cend() && *it <= endPosition; ++it) {
        double beatPosition = it->toEngineSamplePos();
        double xBeatPoint =
                m_waveformRenderer->transformSamplePositionInRendererWorld(beatPosition);

        xBeatPoint = qRound(xBeatPoint * devicePixelRatio) / devicePixelRatio;

        // Determine if this is a downbeat (1st beat of 4/4 measure)
        bool isDownbeat = (beatIndexInBar % 4 == 0);

        if (isDownbeat) {
            // Store in downbeats vector for thick red rendering
            if (downbeatCount >= m_downbeats.size()) {
                m_downbeats.resize(m_downbeats.size() * 2);
            }

            if (orientation == Qt::Horizontal) {
                m_downbeats[downbeatCount++].setLine(xBeatPoint, 0.0f, xBeatPoint, rendererHeight);
            } else {
                m_downbeats[downbeatCount++].setLine(0.0f, xBeatPoint, rendererWidth, xBeatPoint);
            }
        } else {
            // Store in regular beats vector for thin white rendering
            if (beatCount >= m_beats.size()) {
                m_beats.resize(m_beats.size() * 2);
            }

            if (orientation == Qt::Horizontal) {
                m_beats[beatCount++].setLine(xBeatPoint, 0.0f, xBeatPoint, rendererHeight);
            } else {
                m_beats[beatCount++].setLine(0.0f, xBeatPoint, rendererWidth, xBeatPoint);
            }
        }

        beatIndexInBar++;
    }

    // Draw regular beats (thin, white)
    if (beatCount > 0) {
        QPen beatPen(m_beatColor);
        beatPen.setWidthF(std::max(1.0, scaleFactor()));
        painter->setPen(beatPen);
        painter->drawLines(m_beats.constData(), beatCount);
    }

    // Draw downbeats (thick, red) - Rekordbox style
    if (downbeatCount > 0) {
        QPen downbeatPen(m_beatHighlightColor);
        downbeatPen.setWidthF(std::max(3.0, scaleFactor() * 3.0));  // 3x thicker
        painter->setPen(downbeatPen);
        painter->drawLines(m_downbeats.constData(), downbeatCount);
    }
}
