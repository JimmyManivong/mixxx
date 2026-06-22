#include "waveform/renderers/allshader/waveformrendererfiltered.h"

#include <vector>

#include "track/track.h"
#include "util/math.h"
#include "waveform/renderers/allshader/matrixforwidgetgeometry.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "waveform/waveform.h"

namespace allshader {

WaveformRendererFiltered::WaveformRendererFiltered(
        WaveformWidgetRenderer* waveformWidget, bool bRgbStacked)
        : WaveformRendererSignalBase(waveformWidget),
          m_bRgbStacked(bRgbStacked) {
}

void WaveformRendererFiltered::onSetup(const QDomNode& node) {
    Q_UNUSED(node);
}

void WaveformRendererFiltered::initializeGL() {
    WaveformRendererSignalBase::initializeGL();
    m_shader.init();
}

void WaveformRendererFiltered::paintGL() {
    TrackPointer pTrack = m_waveformRenderer->getTrackInfo();
    if (!pTrack) {
        return;
    }

    ConstWaveformPointer waveform = pTrack->getWaveform();
    if (waveform.isNull()) {
        return;
    }

    const int dataSize = waveform->getDataSize();
    if (dataSize <= 1) {
        return;
    }

    const WaveformData* data = waveform->data();
    if (data == nullptr) {
        return;
    }

    // CUSTOM (Rekordbox-style dynamics): cache the track's loudest point so each
    // band can be normalized to it and gamma-curved -> organic, spiky waveform
    // (kicks punch, breaks dip) instead of a saturated block.
    if (data != m_cachedPeakData || m_globalPeak <= 0.f) {
        uchar peak = 1;
        for (int i = 0; i < dataSize; ++i) {
            peak = math_max(peak, data[i].filtered.all);
        }
        m_globalPeak = static_cast<float>(peak);
        m_cachedPeakData = data;
    }

    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();
    const int length = static_cast<int>(m_waveformRenderer->getLength() * devicePixelRatio);

    // See waveformrenderersimple.cpp for a detailed explanation of the frame and index calculation
    const int visualFramesSize = dataSize / 2;
    const double firstVisualFrame =
            m_waveformRenderer->getFirstDisplayedPosition() * visualFramesSize;
    const double lastVisualFrame =
            m_waveformRenderer->getLastDisplayedPosition() * visualFramesSize;

    // Represents the # of visual frames per horizontal pixel.
    const double visualIncrementPerPixel =
            (lastVisualFrame - firstVisualFrame) / static_cast<double>(length);

    // Per-band gain from the EQ knobs.
    float allGain{1.0};
    float bandGain[3] = {1.0, 1.0, 1.0};
    getGains(&allGain, true, &bandGain[0], &bandGain[1], &bandGain[2]);

    // CUSTOM: keep the waveform fixed - the EQ knobs must not change it.
    bandGain[0] = 1.0f;
    bandGain[1] = 1.0f;
    bandGain[2] = 1.0f;

    const float breadth = static_cast<float>(m_waveformRenderer->getBreadth()) * devicePixelRatio;
    const float halfBreadth = breadth / 2.0f;

    // CUSTOM: amplitude is normalized to m_globalPeak (0..1) and gamma-curved,
    // so heightFactor just maps that to pixels.
    constexpr float kHeightScale = 0.85f;
    const float heightFactor = kHeightScale * allGain * halfBreadth;

    // Index 0=low(blue), 1=mid(orange), 2=high(white).
    constexpr float kBandHeightBoost[3] = {1.0f, 1.5f, 1.2f};
    // Per-band gamma. Steep on white = sparse tips. Gentle on orange = full body.
    constexpr float kBandGamma[3] = {1.1f, 1.5f, 3.0f};

    // Effective visual frame for x
    double xVisualFrame = qRound(firstVisualFrame / visualIncrementPerPixel) *
            visualIncrementPerPixel;

    const int numVerticesPerLine = 6; // 2 triangles

    // CUSTOM (Rekordbox-style smooth envelope): instead of one independent
    // rectangle per pixel column (which looks like a spiky comb), each band is
    // drawn as a continuous filled ribbon whose top/bottom edges connect the
    // peak height of consecutive columns. That turns the blocky bars into the
    // smooth triangular "sail" shapes Rekordbox shows. We therefore need to
    // collect the per-column top/bottom y first, then assemble the ribbon: one
    // quad (2 triangles) per adjacent column pair → 6 vertices * (length - 1).
    const int numRibbonColumns = std::max(0, length - 1);

    int reserved[4];
    // low, mid, high
    for (int bandIndex = 0; bandIndex < 3; bandIndex++) {
        m_vertices[bandIndex].clear();
        reserved[bandIndex] = numVerticesPerLine * numRibbonColumns;
        m_vertices[bandIndex].reserve(reserved[bandIndex]);
    }

    // Per-band, per-column envelope (top = channel 0 / up, bottom = channel 1 / down).
    std::vector<float> topY[3];
    std::vector<float> botY[3];
    for (int bandIndex = 0; bandIndex < 3; bandIndex++) {
        topY[bandIndex].resize(length, halfBreadth);
        botY[bandIndex].resize(length, halfBreadth);
    }

    // the horizontal line
    reserved[3] = numVerticesPerLine;
    m_vertices[3].clear();
    m_vertices[3].reserve(reserved[3]);

    m_vertices[3].addRectangle(
            0.f,
            halfBreadth - 0.5f * devicePixelRatio,
            static_cast<float>(length),
            halfBreadth + 0.5f * devicePixelRatio);

    const double maxSamplingRange = visualIncrementPerPixel / 2.0;

    for (int pos = 0; pos < length; ++pos) {
        const int visualFrameStart = std::lround(xVisualFrame - maxSamplingRange);
        const int visualFrameStop = std::lround(xVisualFrame + maxSamplingRange);

        const int visualIndexStart = std::max(visualFrameStart * 2, 0);
        const int visualIndexStop =
                std::min(std::max(visualFrameStop, visualFrameStart + 1) * 2, dataSize - 1);

        const float fpos = static_cast<float>(pos);

        // 3 bands, 2 channels
        float max[3][2]{};

        for (int i = visualIndexStart; i < visualIndexStop; i += 2) {
            for (int chn = 0; chn < 2; chn++) {
                const WaveformData& waveformData = data[i + chn];
                const float filteredLow = static_cast<float>(waveformData.filtered.low);
                const float filteredMid = static_cast<float>(waveformData.filtered.mid);
                const float filteredHigh = static_cast<float>(waveformData.filtered.high);

                max[0][chn] = math_max(max[0][chn], filteredLow);
                max[1][chn] = math_max(max[1][chn], filteredMid);
                max[2][chn] = math_max(max[2][chn], filteredHigh);
            }
        }

        // Below this fraction of the global peak nothing is drawn → black gaps
        // between transients like Rekordbox.
        constexpr float kNoiseFloor = 0.10f;

        for (int bandIndex = 0; bandIndex < 3; bandIndex++) {
            max[bandIndex][0] *= bandGain[bandIndex];
            max[bandIndex][1] *= bandGain[bandIndex];

            const float boost = kBandHeightBoost[bandIndex];
            const float gamma = kBandGamma[bandIndex];

            float raw0 = std::min(1.f, max[bandIndex][0] * boost / m_globalPeak);
            float raw1 = std::min(1.f, max[bandIndex][1] * boost / m_globalPeak);

            const float carved0 = (raw0 > kNoiseFloor) ? std::pow(raw0, gamma) : 0.f;
            const float carved1 = (raw1 > kNoiseFloor) ? std::pow(raw1, gamma) : 0.f;

            // Store the envelope; geometry is assembled as ribbons below.
            topY[bandIndex][pos] = halfBreadth - heightFactor * carved0;
            botY[bandIndex][pos] = halfBreadth + heightFactor * carved1;
        }

        Q_UNUSED(fpos);
        xVisualFrame += visualIncrementPerPixel;
    }

    // CUSTOM (Rekordbox-style flow): a gentle 3-tap smoothing of the envelope so
    // the sails flow into each other instead of jittering column-to-column. Kept
    // mild (centre weight 0.7) to preserve the punch of kick transients. Applied
    // on a copy so each tap reads the original (un-smoothed) neighbours.
    constexpr float kEdgeWeight = 0.15f;
    constexpr float kCenterWeight = 1.f - 2.f * kEdgeWeight;
    for (int bandIndex = 0; bandIndex < 3; bandIndex++) {
        const std::vector<float> srcTop = topY[bandIndex];
        const std::vector<float> srcBot = botY[bandIndex];
        for (int pos = 1; pos + 1 < length; ++pos) {
            topY[bandIndex][pos] = kEdgeWeight * srcTop[pos - 1] +
                    kCenterWeight * srcTop[pos] + kEdgeWeight * srcTop[pos + 1];
            botY[bandIndex][pos] = kEdgeWeight * srcBot[pos - 1] +
                    kCenterWeight * srcBot[pos] + kEdgeWeight * srcBot[pos + 1];
        }
    }

    // Assemble each band as a smooth filled ribbon: connect column pos to pos+1
    // with a quad spanning from the top envelope down to the bottom envelope.
    // Drawn low(blue)→mid(orange)→high(white) so white sits on top at centre and
    // the taller blue caps stick out, same concentric ordering as before.
    for (int bandIndex = 0; bandIndex < 3; bandIndex++) {
        for (int pos = 0; pos + 1 < length; ++pos) {
            const float xa = static_cast<float>(pos);
            const float xb = static_cast<float>(pos + 1);
            const float ta = topY[bandIndex][pos];
            const float tb = topY[bandIndex][pos + 1];
            const float ba = botY[bandIndex][pos];
            const float bb = botY[bandIndex][pos + 1];
            m_vertices[bandIndex].addTriangle({xa, ta}, {xb, tb}, {xb, bb});
            m_vertices[bandIndex].addTriangle({xa, ta}, {xb, bb}, {xa, ba});
        }
    }

    const QMatrix4x4 matrix = matrixForWidgetGeometry(m_waveformRenderer, true);

    const int matrixLocation = m_shader.matrixLocation();
    const int colorLocation = m_shader.colorLocation();
    const int positionLocation = m_shader.positionLocation();

    m_shader.bind();
    m_shader.enableAttributeArray(positionLocation);

    m_shader.setUniformValue(matrixLocation, matrix);

    QColor colors[4];
    if (m_bRgbStacked) {
        colors[0].setRgbF(static_cast<float>(m_rgbLowColor_r),
                static_cast<float>(m_rgbLowColor_g),
                static_cast<float>(m_rgbLowColor_b));
        colors[1].setRgbF(static_cast<float>(m_rgbMidColor_r),
                static_cast<float>(m_rgbMidColor_g),
                static_cast<float>(m_rgbMidColor_b));
        colors[2].setRgbF(static_cast<float>(m_rgbHighColor_r),
                static_cast<float>(m_rgbHighColor_g),
                static_cast<float>(m_rgbHighColor_b));
    } else {
        colors[0].setRgbF(static_cast<float>(m_lowColor_r),
                static_cast<float>(m_lowColor_g),
                static_cast<float>(m_lowColor_b));
        colors[1].setRgbF(static_cast<float>(m_midColor_r),
                static_cast<float>(m_midColor_g),
                static_cast<float>(m_midColor_b));
        colors[2].setRgbF(static_cast<float>(m_highColor_r),
                static_cast<float>(m_highColor_g),
                static_cast<float>(m_highColor_b));
    }
    colors[3].setRgbF(static_cast<float>(m_axesColor_r),
            static_cast<float>(m_axesColor_g),
            static_cast<float>(m_axesColor_b),
            static_cast<float>(m_axesColor_a));

    // 3 bands + 1 extra for the horizontal line

    for (int i = 0; i < 4; i++) {
        DEBUG_ASSERT(reserved[i] == m_vertices[i].size());
        m_shader.setUniformValue(colorLocation, colors[i]);
        m_shader.setAttributeArray(
                positionLocation, GL_FLOAT, m_vertices[i].constData(), 2);

        glDrawArrays(GL_TRIANGLES, 0, m_vertices[i].size());
    }

    m_shader.disableAttributeArray(positionLocation);
    m_shader.release();
}

} // namespace allshader
