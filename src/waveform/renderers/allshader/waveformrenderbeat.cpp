#include "waveform/renderers/allshader/waveformrenderbeat.h"

#include <QDomNode>

#include "control/controlobject.h"
#include "preferences/configobject.h"
#include "skin/legacy/skincontext.h"
#include "track/track.h"
#include "waveform/renderers/allshader/matrixforwidgetgeometry.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "waveform/waveform.h"
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

    // CUSTOM: Read BeatHighlightColor for downbeat markers (Rekordbox style).
    // Falls back to white when the skin leaves it empty.
    QString highlightColorStr = context.selectString(node, "BeatHighlightColor");
    if (!highlightColorStr.isEmpty()) {
        m_highlightColor = QColor(highlightColorStr);
        m_highlightColor = WSkinColor::getCorrectColor(m_highlightColor).toRgb();
    } else {
        m_highlightColor = QColor("#FFFFFF");
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

    // CUSTOM: hide the beat grid over the silence before the sound starts and
    // after it ends. We scan the waveform once (cached) for the first and last
    // sample that carries real signal, and skip beats outside that range.
    mixxx::audio::FramePos musicStartPos = mixxx::audio::kStartFramePos;
    mixxx::audio::FramePos musicEndPos; // invalid -> no end clipping
    mixxx::audio::FramePos downbeatRefPos = mixxx::audio::kStartFramePos;
    {
        ConstWaveformPointer waveform = trackInfo->getWaveform();
        if (waveform && !waveform.isNull()) {
            const int dataSize = waveform->getDataSize();
            const WaveformData* data = waveform->data();
            const double audioVisualRatio = waveform->getAudioVisualRatio();
            if (data != nullptr && dataSize > 1 && audioVisualRatio > 0.0) {
                // Re-scan when the buffer is swapped OR while it is still filling
                // (completion grows during analysis); otherwise a scan done on a
                // half-analysed track would clip the grid partway through.
                const int completion = waveform->getCompletion();
                if (data != m_cachedSignalData || completion != m_cachedCompletion) {
                    m_cachedCompletion = completion;
                    // Silence reads ~0; treat anything above this (out of 255)
                    // as real signal. Index is interleaved L/R (2 per frame).
                    constexpr uchar kSignalThreshold = 6;
                    int firstIdx = -1;
                    int lastIdx = -1;
                    for (int i = 0; i < dataSize; ++i) {
                        if (data[i].filtered.all > kSignalThreshold) {
                            if (firstIdx < 0) {
                                firstIdx = i;
                            }
                            lastIdx = i;
                        }
                    }
                    if (firstIdx >= 0) {
                        m_signalStartFrame =
                                static_cast<double>(firstIdx / 2) * audioVisualRatio;
                        m_signalEndFrame =
                                static_cast<double>(lastIdx / 2) * audioVisualRatio;
                    } else {
                        m_signalStartFrame = 0.0;
                        m_signalEndFrame = 0.0;
                    }
                    m_cachedSignalData = data;
                }
                if (m_signalEndFrame > m_signalStartFrame) {
                    // Grid clipping is anchored on where the SOUND starts (audio
                    // onset), snapped to the nearest beat, so the grid is hidden
                    // only over the silence — independent of the downbeat.
                    const mixxx::audio::FramePos onsetPos =
                            mixxx::audio::FramePos(m_signalStartFrame);
                    const mixxx::audio::FramePos clipBeat =
                            trackBeats->findClosestBeat(onsetPos);
                    musicStartPos = clipBeat.isValid() ? clipBeat : onsetPos;
                    musicEndPos = mixxx::audio::FramePos(m_signalEndFrame);
                    // The downbeat (bar 1 = white line + red triangle) defaults to
                    // the very beginning of the sound, but the GRID EDIT "SET"
                    // button stores the exact playhead position in grid_downbeat_pos
                    // — when set (>=0), the downbeat lands there.
                    downbeatRefPos = onsetPos;
                    const double downbeatFrame = ControlObject::get(
                            ConfigKey(m_waveformRenderer->getGroup(),
                                    QStringLiteral("grid_downbeat_pos")));
                    if (downbeatFrame >= 0.0) {
                        downbeatRefPos = mixxx::audio::FramePos(downbeatFrame);
                    }
                }
            }
        }
    }
    const auto outsideMusic = [&](mixxx::audio::FramePos pos) {
        return pos < musicStartPos || (musicEndPos.isValid() && pos > musicEndPos);
    };

    const float rendererBreadth = m_waveformRenderer->getBreadth();
    const float breadth = m_isSlipRenderer ? rendererBreadth / 2.f : rendererBreadth;

    // CUSTOM (Rekordbox style): each beat = a thin full-height vertical line
    // (needed for beat-matching) topped and tailed by a horizontal cap so the
    // marker reads as a "T" (⊤ at the top edge, ⊥ at the bottom). Normal beats
    // grey and quite transparent so the waveform stays readable; downbeats (bar
    // starts) white and opaque so they pop. Lines kept thin for a clean look.
    constexpr float kBeatAlphaScale = 0.35f; // grey lines subtle
    constexpr float kLineHalfWidth = 0.5f;   // vertical line half-width (px)
    constexpr float kCapWidth = 7.f;         // horizontal T cap width (px)
    constexpr float kCapThickness = 2.5f;    // T cap thickness (px)
    // Downbeats: white vertical line + small red triangles at top/bottom.
    constexpr float kTriHalfWidth = 3.5f;    // red triangle half-base (px)
    constexpr float kTriHeight = 5.f;        // red triangle height (px)

    // Absolute beat index, anchored on the last marker which sits on the
    // track's first downbeat (see Beats docs). This keeps the downbeats aligned
    // to the actual bar starts and stable while the waveform scrolls, instead of
    // counting from whichever beat happens to be visible first.
    const auto downbeatAnchor = trackBeats->clastmarker();
    // CUSTOM: align the bar phase so the FIRST beat of the sound is a downbeat
    // (white line + red triangle), then every 4 beats after. musicStartPos is the
    // audio onset; the first beat at/after it is the reference.
    int firstSoundBeatIndex = 0;
    {
        const auto downbeatBeat = trackBeats->findClosestBeat(downbeatRefPos);
        if (downbeatBeat.isValid()) {
            firstSoundBeatIndex = static_cast<int>(
                    trackBeats->iteratorFrom(downbeatBeat) - downbeatAnchor);
        }
    }
    const auto isDownbeat = [firstSoundBeatIndex](int index) {
        return ((((index - firstSoundBeatIndex) % 4) + 4) % 4) == 0;
    };

    // Count the number of beats (and downbeats) in the range to reserve space.
    // Note that we could also use
    //   int numBeatsInRange = trackBeats->numBeatsInRange(startPosition, endPosition);
    // for this, but there have been reports of that method failing with a DEBUG_ASSERT.
    int numBeatsInRange = 0;
    int numDownbeatsInRange = 0;
    {
        auto it = trackBeats->iteratorFrom(startPosition);
        int beatIndex = static_cast<int>(it - downbeatAnchor);
        for (; it != trackBeats->cend() && *it <= endPosition; ++it, ++beatIndex) {
            if (outsideMusic(*it)) {
                continue;
            }
            if (isDownbeat(beatIndex)) {
                numDownbeatsInRange++;
            }
            numBeatsInRange++;
        }
    }

    // Normal beat = grey "T": vertical line + top cap + bottom cap = 3 rects = 18 verts.
    // Downbeat = white vertical line (6 verts) + 2 red triangles (6 verts).
    const int numGreyBeats = numBeatsInRange - numDownbeatsInRange;
    m_vertices.clear();
    m_vertices.reserve(numGreyBeats * 3 * 6);
    m_dotVertices.clear();
    m_dotVertices.reserve(numDownbeatsInRange * 6);
    m_triangleVertices.clear();
    m_triangleVertices.reserve(numDownbeatsInRange * 2 * 3);

    {
        auto it = trackBeats->iteratorFrom(startPosition);
        int beatIndex = static_cast<int>(it - downbeatAnchor);
        for (; it != trackBeats->cend() && *it <= endPosition; ++it, ++beatIndex) {
            if (outsideMusic(*it)) {
                continue;
            }
            double beatPosition = it->toEngineSamplePos();
            double xBeatPoint =
                    m_waveformRenderer->transformSamplePositionInRendererWorld(
                            beatPosition, positionType);

            xBeatPoint = qRound(xBeatPoint * devicePixelRatio) / devicePixelRatio;

            const float cx = static_cast<float>(xBeatPoint);

            if (isDownbeat(beatIndex)) {
                // White vertical line + small red triangles at top and bottom.
                m_dotVertices.addRectangle(
                        cx - kLineHalfWidth, 0.f, cx + kLineHalfWidth, breadth);
                // Top triangle pointing down (▼).
                m_triangleVertices.addTriangle({cx - kTriHalfWidth, 0.f},
                        {cx + kTriHalfWidth, 0.f},
                        {cx, kTriHeight});
                // Bottom triangle pointing up (▲).
                m_triangleVertices.addTriangle({cx - kTriHalfWidth, breadth},
                        {cx + kTriHalfWidth, breadth},
                        {cx, breadth - kTriHeight});
            } else {
                // Grey "T": vertical line + top/bottom horizontal caps.
                const float capX1 = cx - kCapWidth / 2.f;
                const float capX2 = cx + kCapWidth / 2.f;
                m_vertices.addRectangle(
                        cx - kLineHalfWidth, 0.f, cx + kLineHalfWidth, breadth);
                m_vertices.addRectangle(capX1, 0.f, capX2, kCapThickness);
                m_vertices.addRectangle(capX1, breadth - kCapThickness, capX2, breadth);
            }
        }
    }

    DEBUG_ASSERT(numGreyBeats * 3 * 6 == m_vertices.size());
    DEBUG_ASSERT(numDownbeatsInRange * 6 == m_dotVertices.size());
    DEBUG_ASSERT(numDownbeatsInRange * 2 * 3 == m_triangleVertices.size());

    const int positionLocation = m_shader.positionLocation();
    const int matrixLocation = m_shader.matrixLocation();
    const int colorLocation = m_shader.colorLocation();

    m_shader.bind();
    m_shader.enableAttributeArray(positionLocation);

    const QMatrix4x4 matrix = matrixForWidgetGeometry(m_waveformRenderer, false);
    m_shader.setUniformValue(matrixLocation, matrix);

    // Grey "T" markers for normal beats (semi-transparent).
    if (m_vertices.size() > 0) {
        QColor beatColor = m_color;
        beatColor.setAlphaF((alpha / 100.0f) * kBeatAlphaScale);
        m_shader.setAttributeArray(
                positionLocation, GL_FLOAT, m_vertices.constData(), 2);
        m_shader.setUniformValue(colorLocation, beatColor);
        glDrawArrays(GL_TRIANGLES, 0, m_vertices.size());
    }

    // CUSTOM: white downbeat lines (the "bar counter") drawn opaque so they pop.
    if (m_dotVertices.size() > 0) {
        QColor lineColor = m_highlightColor;
        lineColor.setAlphaF(1.0f);
        m_shader.setAttributeArray(
                positionLocation, GL_FLOAT, m_dotVertices.constData(), 2);
        m_shader.setUniformValue(colorLocation, lineColor);
        glDrawArrays(GL_TRIANGLES, 0, m_dotVertices.size());
    }

    // CUSTOM: small red triangles at the top/bottom of each downbeat line.
    if (m_triangleVertices.size() > 0) {
        QColor triColor("#FF0000");
        triColor.setAlphaF(1.0f);
        m_shader.setAttributeArray(
                positionLocation, GL_FLOAT, m_triangleVertices.constData(), 2);
        m_shader.setUniformValue(colorLocation, triColor);
        glDrawArrays(GL_TRIANGLES, 0, m_triangleVertices.size());
    }

    m_shader.disableAttributeArray(positionLocation);
    m_shader.release();
}

} // namespace allshader
