#include "waveform/renderers/allshader/waveformrenderbeat.h"

#include <QDomNode>

#include "skin/legacy/skincontext.h"
#include "track/track.h"
#include "waveform/renderers/allshader/matrixforwidgetgeometry.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "widget/wskincolor.h"

namespace allshader {

WaveformRenderBeat::WaveformRenderBeat(WaveformWidgetRenderer* waveformWidget,
        ::WaveformRendererAbstract::PositionSource type)
        : WaveformRenderer(waveformWidget),
          m_isSlipRenderer(type == ::WaveformRendererAbstract::Slip) {
}

void WaveformRenderBeat::initializeGL() {
    WaveformRenderer::initializeGL();
    m_shader.init();
}

void WaveformRenderBeat::setup(const QDomNode& node, const SkinContext& context) {
    m_color = QColor(context.selectString(node, "BeatColor"));
    m_color = WSkinColor::getCorrectColor(m_color).toRgb();

    // CUSTOM: Read BeatHighlightColor for downbeats (Rekordbox style)
    QString highlightColorStr = context.selectString(node, "BeatHighlightColor");
    if (!highlightColorStr.isEmpty()) {
        m_highlightColor = QColor(highlightColorStr);
        m_highlightColor = WSkinColor::getCorrectColor(m_highlightColor).toRgb();
    } else {
        // Default to red if not specified
        m_highlightColor = QColor("#FF0000");
    }
}

void WaveformRenderBeat::paintGL() {
    TrackPointer trackInfo = m_waveformRenderer->getTrackInfo();

    if (!trackInfo || (m_isSlipRenderer && !m_waveformRenderer->isSlipActive())) {
        return;
    }

    auto positionType = m_isSlipRenderer ? ::WaveformRendererAbstract::Slip
                                         : ::WaveformRendererAbstract::Play;

    mixxx::BeatsPointer trackBeats = trackInfo->getBeats();
    if (!trackBeats) {
        return;
    }

    int alpha = m_waveformRenderer->getBeatGridAlpha();
    if (alpha == 0) {
        return;
    }

    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_color.setAlphaF(alpha / 100.0f);

    const double trackSamples = m_waveformRenderer->getTrackSamples();
    if (trackSamples <= 0) {
        return;
    }

    const double firstDisplayedPosition =
            m_waveformRenderer->getFirstDisplayedPosition(positionType);
    const double lastDisplayedPosition =
            m_waveformRenderer->getLastDisplayedPosition(positionType);

    const auto startPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            firstDisplayedPosition * trackSamples);
    const auto endPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            lastDisplayedPosition * trackSamples);

    if (!startPosition.isValid() || !endPosition.isValid()) {
        return;
    }

    const float rendererBreadth = m_waveformRenderer->getBreadth();

    const int numVerticesPerLine = 6; // 2 triangles

    // CUSTOM: Count beats and downbeats separately (Rekordbox style)
    int numBeatsInRange = 0;
    int numDownbeatsInRange = 0;
    int beatIndexInBar = 0;

    for (auto it = trackBeats->iteratorFrom(startPosition);
            it != trackBeats->cend() && *it <= endPosition;
            ++it) {
        if (beatIndexInBar % 4 == 0) {
            numDownbeatsInRange++;
        } else {
            numBeatsInRange++;
        }
        beatIndexInBar++;
    }

    const int reservedBeats = numBeatsInRange * numVerticesPerLine;
    const int reservedDownbeats = numDownbeatsInRange * numVerticesPerLine;
    m_vertices.clear();
    m_vertices.reserve(reservedBeats);
    m_downbeatVertices.clear();
    m_downbeatVertices.reserve(reservedDownbeats);

    beatIndexInBar = 0;
    for (auto it = trackBeats->iteratorFrom(startPosition);
            it != trackBeats->cend() && *it <= endPosition;
            ++it) {
        double beatPosition = it->toEngineSamplePos();
        double xBeatPoint =
                m_waveformRenderer->transformSamplePositionInRendererWorld(
                        beatPosition, positionType);

        xBeatPoint = qRound(xBeatPoint * devicePixelRatio) / devicePixelRatio;

        bool isDownbeat = (beatIndexInBar % 4 == 0);

        if (isDownbeat) {
            // Downbeats are thicker (3px instead of 1px)
            const float x1 = static_cast<float>(xBeatPoint) - 1.5f;
            const float x2 = x1 + 3.f;

            m_downbeatVertices.addRectangle(x1,
                    0.f,
                    x2,
                    m_isSlipRenderer ? rendererBreadth / 2 : rendererBreadth);
        } else {
            // Regular beats are 1px
            const float x1 = static_cast<float>(xBeatPoint);
            const float x2 = x1 + 1.f;

            m_vertices.addRectangle(x1,
                    0.f,
                    x2,
                    m_isSlipRenderer ? rendererBreadth / 2 : rendererBreadth);
        }

        beatIndexInBar++;
    }

    DEBUG_ASSERT(reservedBeats == m_vertices.size());
    DEBUG_ASSERT(reservedDownbeats == m_downbeatVertices.size());

    const int positionLocation = m_shader.positionLocation();
    const int matrixLocation = m_shader.matrixLocation();
    const int colorLocation = m_shader.colorLocation();

    m_shader.bind();
    m_shader.enableAttributeArray(positionLocation);

    const QMatrix4x4 matrix = matrixForWidgetGeometry(m_waveformRenderer, false);
    m_shader.setUniformValue(matrixLocation, matrix);

    // Draw regular beats (thin, white/default color)
    if (m_vertices.size() > 0) {
        m_shader.setAttributeArray(
                positionLocation, GL_FLOAT, m_vertices.constData(), 2);
        m_shader.setUniformValue(colorLocation, m_color);
        glDrawArrays(GL_TRIANGLES, 0, m_vertices.size());
    }

    // CUSTOM: Draw downbeats (thick, red) - Rekordbox style
    if (m_downbeatVertices.size() > 0) {
        QColor downbeatColor = m_highlightColor;
        downbeatColor.setAlphaF(alpha / 100.0f);

        m_shader.setAttributeArray(
                positionLocation, GL_FLOAT, m_downbeatVertices.constData(), 2);
        m_shader.setUniformValue(colorLocation, downbeatColor);
        glDrawArrays(GL_TRIANGLES, 0, m_downbeatVertices.size());
    }

    m_shader.disableAttributeArray(positionLocation);
    m_shader.release();
}

} // namespace allshader
