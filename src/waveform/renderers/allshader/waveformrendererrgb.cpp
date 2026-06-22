#include "waveform/renderers/allshader/waveformrendererrgb.h"

#include "track/track.h"
#include "util/math.h"
#include "waveform/renderers/allshader/matrixforwidgetgeometry.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "waveform/waveform.h"

namespace allshader {

namespace {
inline float math_pow2(float x) {
    return x * x;
}
} // namespace

WaveformRendererRGB::WaveformRendererRGB(WaveformWidgetRenderer* waveformWidget,
        ::WaveformRendererAbstract::PositionSource type)
        : WaveformRendererSignalBase(waveformWidget),
          m_isSlipRenderer(type == ::WaveformRendererAbstract::Slip) {
}

void WaveformRendererRGB::onSetup(const QDomNode& node) {
    Q_UNUSED(node);
}

void WaveformRendererRGB::initializeGL() {
    WaveformRendererSignalBase::initializeGL();
    m_shader.init();
}

void WaveformRendererRGB::paintGL() {
    TrackPointer pTrack = m_waveformRenderer->getTrackInfo();
    if (!pTrack || (m_isSlipRenderer && !m_waveformRenderer->isSlipActive())) {
        return;
    }

    auto positionType = m_isSlipRenderer ? ::WaveformRendererAbstract::Slip
                                         : ::WaveformRendererAbstract::Play;

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

    // CUSTOM (Rekordbox-style dynamics): find the loudest point of the whole
    // track once and cache it. The per-column height is later normalized to
    // this peak and gamma-curved so the waveform "breathes" (kicks stand out,
    // breaks stay low) instead of saturating into a flat block.
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
            m_waveformRenderer->getFirstDisplayedPosition(positionType) * visualFramesSize;
    const double lastVisualFrame =
            m_waveformRenderer->getLastDisplayedPosition(positionType) * visualFramesSize;

    // Represents the # of visual frames per horizontal pixel.
    const double visualIncrementPerPixel =
            (lastVisualFrame - firstVisualFrame) / static_cast<double>(length);

    // Per-band gain from the EQ knobs.
    float allGain(1.0), lowGain(1.0), midGain(1.0), highGain(1.0);
    // applyCompensation = false, as we scale to match filtered.all
    getGains(&allGain, false, &lowGain, &midGain, &highGain);

    // CUSTOM: keep the waveform fixed - don't let the EQ knobs change it. We
    // still honour allGain (the overall waveform zoom/gain), just not the
    // per-band EQ gains.
    lowGain = 1.0f;
    midGain = 1.0f;
    highGain = 1.0f;

    const float breadth = static_cast<float>(m_waveformRenderer->getBreadth()) * devicePixelRatio;
    const float halfBreadth = breadth / 2.0f;

    // CUSTOM height controls:
    //  - kHeightScale: overall height of the waveform (1.0 = stock; the RMS
    //    analyzer gives low values so a bit more reads better).
    //  - kBassHeightBoost: extra height for bass-dominated columns only, so the
    //    kicks punch taller while mids/highs keep their normal height. The
    //    per-column height becomes max(all, low * kBassHeightBoost).
    constexpr float kHeightScale = 0.95f;  // 1.0 = the track's loudest point fills the half-height
    constexpr float kBassHeightBoost = 1.6f;
    constexpr float kGamma = 1.6f;         // >1 carves the dynamics (organic Rekordbox look)
    // heightFactor maps a normalized 0..1 amplitude to pixels (no /255 here -
    // amplitude is normalized to the track's global peak below).
    const float heightFactor = kHeightScale * allGain * halfBreadth;

    // Standard RGB band colors (from the skin / defaults: low=red, mid=green,
    // high=blue).
    const float low_r = static_cast<float>(m_rgbLowColor_r);
    const float mid_r = static_cast<float>(m_rgbMidColor_r);
    const float high_r = static_cast<float>(m_rgbHighColor_r);
    const float low_g = static_cast<float>(m_rgbLowColor_g);
    const float mid_g = static_cast<float>(m_rgbMidColor_g);
    const float high_g = static_cast<float>(m_rgbHighColor_g);
    const float low_b = static_cast<float>(m_rgbLowColor_b);
    const float mid_b = static_cast<float>(m_rgbMidColor_b);
    const float high_b = static_cast<float>(m_rgbHighColor_b);

    // Effective visual frame for x
    double xVisualFrame = qRound(firstVisualFrame / visualIncrementPerPixel) *
            visualIncrementPerPixel;

    const int numVerticesPerLine = 6; // 2 triangles

    const int reserved = numVerticesPerLine * (length + 1);

    m_vertices.clear();
    m_vertices.reserve(reserved);
    m_colors.clear();
    m_colors.reserve(reserved);

    m_vertices.addRectangle(0.f,
            halfBreadth - 0.5f * devicePixelRatio,
            static_cast<float>(length),
            m_isSlipRenderer ? halfBreadth : halfBreadth + 0.5f * devicePixelRatio);
    m_colors.addForRectangle(
            static_cast<float>(m_axesColor_r),
            static_cast<float>(m_axesColor_g),
            static_cast<float>(m_axesColor_b));

    const double maxSamplingRange = visualIncrementPerPixel / 2.0;

    for (int pos = 0; pos < length; ++pos) {
        const int visualFrameStart = std::lround(xVisualFrame - maxSamplingRange);
        const int visualFrameStop = std::lround(xVisualFrame + maxSamplingRange);

        const int visualIndexStart = std::max(visualFrameStart * 2, 0);
        const int visualIndexStop =
                std::min(std::max(visualFrameStop, visualFrameStart + 1) * 2, dataSize - 1);

        const float fpos = static_cast<float>(pos);

        // Find the max values for low, mid, high and all in the waveform data.
        // - Max of left and right
        uchar u8maxLow{};
        uchar u8maxMid{};
        uchar u8maxHigh{};
        // - Per channel
        uchar u8maxAllChn[2]{};
        for (int chn = 0; chn < 2; chn++) {
            // data is interleaved left / right
            for (int i = visualIndexStart + chn; i < visualIndexStop + chn; i += 2) {
                const WaveformData& waveformData = data[i];

                u8maxLow = math_max(u8maxLow, waveformData.filtered.low);
                u8maxMid = math_max(u8maxMid, waveformData.filtered.mid);
                u8maxHigh = math_max(u8maxHigh, waveformData.filtered.high);
                u8maxAllChn[chn] = math_max(u8maxAllChn[chn], waveformData.filtered.all);
            }
        }

        // Cast to float
        float maxLow = static_cast<float>(u8maxLow);
        float maxMid = static_cast<float>(u8maxMid);
        float maxHigh = static_cast<float>(u8maxHigh);
        float maxAllChn[2]{static_cast<float>(u8maxAllChn[0]), static_cast<float>(u8maxAllChn[1])};
        // Uncomment to undo scaling with pow(value, 2.0f * 0.316f) done in analyzerwaveform.h
        // float maxAllChn[2]{unscale(u8maxAllChn[0]), unscale(u8maxAllChn[1])};

        // Calculate the squared magnitude of the maxLow, maxMid and maxHigh values.
        // We take the square root to get the magnitude below.
        const float sum = math_pow2(maxLow) + math_pow2(maxMid) + math_pow2(maxHigh);

        // Apply the gains
        maxLow *= lowGain;
        maxMid *= midGain;
        maxHigh *= highGain;

        // Calculate the squared magnitude of the gained maxLow, maxMid and maxHigh values
        // We take the square root to get the magnitude below.
        const float sumGained = math_pow2(maxLow) + math_pow2(maxMid) + math_pow2(maxHigh);

        // The maxAll values will be used to draw the amplitude. We scale them according to
        // magnitude of the gained maxLow, maxMid and maxHigh values
        if (sum != 0.f) {
            // magnitude = sqrt(sum) and magnitudeGained = sqrt(sumGained), and
            // factor = magnitudeGained / magnitude, but we can do with a single sqrt:
            const float factor = std::sqrt(sumGained / sum);
            maxAllChn[0] *= factor;
            maxAllChn[1] *= factor;
        }

        // CUSTOM colour model: a plain additive low+mid+high mix turns muddy
        // (blue bass + orange mid -> pale pink, white highs wash everything).
        // Instead let the DOMINANT band win: weight each band, square the
        // weights to sharpen toward the strongest, and take a weighted average.
        // Result = clean blue (kicks) / orange (mids) / white (highs), not a
        // washed pastel. Boosts let mids/highs win in their own moments.
        constexpr float kMidColorBoost = 1.5f;
        constexpr float kHighColorBoost = 1.4f;
        const float eLow = maxLow;
        const float eMid = maxMid * kMidColorBoost;
        const float eHigh = maxHigh * kHighColorBoost;
        const float wLow = eLow * eLow;
        const float wMid = eMid * eMid;
        const float wHigh = eHigh * eHigh;
        const float wTotal = wLow + wMid + wHigh;

        float red = 0.f;
        float green = 0.f;
        float blue = 0.f;
        if (wTotal > 0.f) {
            const float inv = 1.f / wTotal;
            red = (wLow * low_r + wMid * mid_r + wHigh * high_r) * inv;
            green = (wLow * low_g + wMid * mid_g + wHigh * high_g) * inv;
            blue = (wLow * low_b + wMid * mid_b + wHigh * high_b) * inv;
            // Re-saturate so the winning colour stays vivid.
            const float maxComponent = math_max3(red, green, blue);
            if (maxComponent > 0.f) {
                const float normFactor = 1.f / maxComponent;
                red *= normFactor;
                green *= normFactor;
                blue *= normFactor;
            }
        }

        // CUSTOM (Rekordbox-style dynamics): the bass can push a column taller
        // (kicks punch), then normalize to the track's global peak and apply a
        // gamma curve (kGamma>1) so loud sections stand out and quiet sections
        // dip - the waveform "breathes" instead of saturating into a block.
        const float boostedLow = maxLow * kBassHeightBoost;
        const float nAll0 = std::min(1.f, std::max(maxAllChn[0], boostedLow) / m_globalPeak);
        const float nAll1 = std::min(1.f, std::max(maxAllChn[1], boostedLow) / m_globalPeak);
        const float carved0 = std::pow(nAll0, kGamma);
        const float carved1 = std::pow(nAll1, kGamma);

        // Lines are thin rectangles
        m_vertices.addRectangle(fpos - 0.5f,
                halfBreadth - heightFactor * carved0,
                fpos + 0.5f,
                m_isSlipRenderer ? halfBreadth : halfBreadth + heightFactor * carved1);
        m_colors.addForRectangle(red, green, blue);

        xVisualFrame += visualIncrementPerPixel;
    }

    DEBUG_ASSERT(reserved == m_vertices.size());
    DEBUG_ASSERT(reserved == m_colors.size());

    const QMatrix4x4 matrix = matrixForWidgetGeometry(m_waveformRenderer, true);

    const int matrixLocation = m_shader.matrixLocation();
    const int positionLocation = m_shader.positionLocation();
    const int colorLocation = m_shader.colorLocation();

    m_shader.bind();
    m_shader.enableAttributeArray(positionLocation);
    m_shader.enableAttributeArray(colorLocation);

    m_shader.setUniformValue(matrixLocation, matrix);

    m_shader.setAttributeArray(
            positionLocation, GL_FLOAT, m_vertices.constData(), 2);
    m_shader.setAttributeArray(
            colorLocation, GL_FLOAT, m_colors.constData(), 3);

    glDrawArrays(GL_TRIANGLES, 0, m_vertices.size());

    m_shader.disableAttributeArray(positionLocation);
    m_shader.disableAttributeArray(colorLocation);
    m_shader.release();
}

} // namespace allshader
