#include "waveform/renderers/allshader/waveformrenderer3band.h"

#include "track/track.h"
#include "util/colorcomponents.h"
#include "util/math.h"
#include "waveform/renderers/allshader/matrixforwidgetgeometry.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "waveform/waveform.h"

namespace allshader {

namespace {
// CUSTOM (2026-07-07): this renderer used to draw low/mid/high as 3
// independently-scaled, overlapping colored bars (tallest band visible,
// alpha-blended where they overlap). Comparing pixel-for-pixel against
// real Rekordbox captures of the same track showed that isn't how a
// Rekordbox/Serato-style waveform actually works: there is exactly ONE
// envelope shape (height = overall/broadband amplitude, the "all" analysis
// channel - see WaveformData in waveform.h), and its FILL COLOR is a
// per-pixel weighted mix of the low/mid/high reference colors, weighted by
// each band's relative energy at that point - not 3 separate stacked
// shapes. Mixxx already ships exactly this algorithm as the stock
// "AllShaderRGBWaveform" renderer (src/waveform/renderers/allshader/
// waveformrendererrgb.cpp, not compiled into this fork - see
// preferences/upgrade.cpp's comment on why only ThreeBand is built). This
// is that algorithm, ported in as this renderer's actual body, keeping our
// customizations: skin-driven colors, the "frozen" ignore-EQ/trim
// behavior, and the extra spatial smoothing for the rounded per-beat
// "blob" look Rekordbox shows instead of a jagged peak-by-peak outline.
constexpr int kSmoothRadius = 10;
// Body = blend of the per-pixel window AVERAGE and its MAX peak. At close
// zoom a pixel covers ~1 frame so avg == max (no effect, punch kept).
// Zoomed out a pixel covers many frames; pure MAX would fill every pixel
// to a solid block, so leaning on the average restores a readable energy
// envelope (loud=tall, quiet=short) like Rekordbox. 1.0 = pure peak, 0.0 =
// pure average.
constexpr float kBodyPeakMix = 0.35f;
} // namespace

WaveformRendererThreeBand::WaveformRendererThreeBand(
        WaveformWidgetRenderer* waveformWidget)
        : WaveformRendererSignalBase(waveformWidget) {
}

void WaveformRendererThreeBand::onSetup(const QDomNode& /*node*/) {
}

void WaveformRendererThreeBand::initializeGL() {
    WaveformRendererSignalBase::initializeGL();
    m_shader.init();
}

void WaveformRendererThreeBand::paintGL() {
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

    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();
    const int length = static_cast<int>(m_waveformRenderer->getLength() * devicePixelRatio);

    const int visualFramesSize = dataSize / 2;
    const double firstVisualFrame =
            m_waveformRenderer->getFirstDisplayedPosition() * visualFramesSize;
    const double lastVisualFrame =
            m_waveformRenderer->getLastDisplayedPosition() * visualFramesSize;

    const double visualIncrementPerPixel =
            (lastVisualFrame - firstVisualFrame) / static_cast<double>(length);

    // CUSTOM (Rekordbox-style FROZEN waveform): don't call getGains() - it
    // pulls in the trim/pregain and EQ knobs (see waveformwidgetrenderer.cpp),
    // which would make turning those knobs shrink/grow or recolor the
    // waveform. The waveform always shows the track's real content
    // regardless of any live mixer control, so all gains are fixed at 1.0.
    constexpr float allGain = 1.0f;
    // CUSTOM: the color-mix math above is exactly the stock RGB algorithm,
    // but applied to it un-touched (1.0/1.0/1.0) the mix reads almost
    // solid blue with no orange/white ever showing - Mixxx's own analysis
    // filters (analyzerwaveform.h) give bass far more relative energy than
    // Rekordbox's own filters apparently do for the same audio, so without
    // compensation low wins the normalization almost everywhere. These
    // gains only rebalance the COLOR MIX weighting, not the envelope
    // height (which stays driven by the "all" channel, untouched).
    constexpr float lowGain = 0.1f;
    constexpr float midGain = 4.0f;
    // CUSTOM: the reference (near-white) high color contributes strongly to
    // all 3 RGB channels at once, so a high highGain washes every pixel
    // toward pale/desaturated regardless of low/mid balance. Reduced so
    // blue/orange read as bolder, more saturated hues like the reference,
    // with white only breaking through at genuinely dominant transients.
    constexpr float highGain = 0.5f;

    const float breadth = static_cast<float>(m_waveformRenderer->getBreadth()) * devicePixelRatio;
    const float halfBreadth = breadth / 2.0f;
    const float heightFactor = allGain * halfBreadth / m_maxValue;

    const float low_r = m_rgbLowColor_r;
    const float mid_r = m_rgbMidColor_r;
    const float high_r = m_rgbHighColor_r;
    const float low_g = m_rgbLowColor_g;
    const float mid_g = m_rgbMidColor_g;
    const float high_g = m_rgbHighColor_g;
    const float low_b = m_rgbLowColor_b;
    const float mid_b = m_rgbMidColor_b;
    const float high_b = m_rgbHighColor_b;

    double xVisualFrame = qRound(firstVisualFrame / visualIncrementPerPixel) *
            visualIncrementPerPixel;

    const int numVerticesPerLine = 6;
    const int reserved = numVerticesPerLine * (length + 1);

    m_vertices.clear();
    m_vertices.reserve(reserved);
    m_colors.clear();
    m_colors.reserve(reserved);

    // Center axis line
    m_vertices.addRectangle(0.f,
            halfBreadth - 0.5f * devicePixelRatio,
            static_cast<float>(length),
            halfBreadth + 0.5f * devicePixelRatio);
    m_colors.addForRectangle(0.f, 0.f, 0.f, 0.f);

    const double maxSamplingRange = visualIncrementPerPixel / 2.0;

    // Pass 1: gather each pixel's envelope height (from the "all" channel,
    // peak/average blended) and its mix color (from low/mid/high peaks).
    m_envelopeHeight.assign(length, 0.f);
    m_mixColor.assign(static_cast<size_t>(length) * 3, 0.f);
    double xvf = xVisualFrame;
    for (int pos = 0; pos < length; ++pos) {
        const int visualFrameStart = std::lround(xvf - maxSamplingRange);
        const int visualFrameStop = std::lround(xvf + maxSamplingRange);
        const int visualIndexStart = std::max(visualFrameStart * 2, 0);
        const int visualIndexStop =
                std::min(std::max(visualFrameStop, visualFrameStart + 1) * 2, dataSize - 1);

        uchar u8low{}, u8mid{}, u8high{};
        int sumAll = 0, cnt = 0;
        uchar u8maxAllChn[2]{};
        for (int chn = 0; chn < 2; chn++) {
            for (int i = visualIndexStart + chn; i < visualIndexStop + chn; i += 2) {
                const WaveformData& wd = data[i];
                u8low = math_max(u8low, wd.filtered.low);
                u8mid = math_max(u8mid, wd.filtered.mid);
                u8high = math_max(u8high, wd.filtered.high);
                u8maxAllChn[chn] = math_max(u8maxAllChn[chn], wd.filtered.all);
                sumAll += wd.filtered.all;
                ++cnt;
            }
        }

        float maxLow = static_cast<float>(u8low) * lowGain;
        float maxMid = static_cast<float>(u8mid) * midGain;
        float maxHigh = static_cast<float>(u8high) * highGain;

        // CUSTOM (2026-07-07, iteration 4): a continuous weighted blend of
        // the 3 reference colors (the stock RGB renderer's approach)
        // produced a washed-out pale gradient - every pixel is some blend
        // of all 3 colors, never a bold pure hue. The reference capture
        // shows sharp-edged solid-colored regions instead (a clearly blue
        // patch, then a clearly tan patch, not a smooth gradient between
        // them), suggesting a hard "loudest band wins its pure color"
        // choice per pixel rather than a continuous mix.
        float red, green, blue;
        if (maxLow >= maxMid && maxLow >= maxHigh) {
            red = low_r;
            green = low_g;
            blue = low_b;
        } else if (maxMid >= maxHigh) {
            red = mid_r;
            green = mid_g;
            blue = mid_b;
        } else {
            red = high_r;
            green = high_g;
            blue = high_b;
        }
        m_mixColor[static_cast<size_t>(pos) * 3 + 0] = red;
        m_mixColor[static_cast<size_t>(pos) * 3 + 1] = green;
        m_mixColor[static_cast<size_t>(pos) * 3 + 2] = blue;

        // Envelope height: blend the window's average "all" value with its
        // peak per-channel max, so a zoomed-out view reads as an energy
        // envelope instead of a solid block (see kBodyPeakMix above).
        const float avgAll = cnt ? static_cast<float>(sumAll) / cnt : 0.f;
        const float maxAll = static_cast<float>(math_max(u8maxAllChn[0], u8maxAllChn[1]));
        m_envelopeHeight[pos] = avgAll + (maxAll - avgAll) * kBodyPeakMix;

        xvf += visualIncrementPerPixel;
    }

    // Spatial (fixed, NOT temporal) moving average on the envelope height
    // -> rounded per-beat "blob" contour like Rekordbox, instead of a
    // jagged peak-by-peak outline. Radius 0 disables the rounding.
    auto smoothAt = [length](const std::vector<float>& src, int pos) -> float {
        float sum = 0.f;
        int n = 0;
        for (int k = -kSmoothRadius; k <= kSmoothRadius; ++k) {
            const int p = pos + k;
            if (p < 0 || p >= length) {
                continue;
            }
            sum += src[p];
            ++n;
        }
        return n ? sum / static_cast<float>(n) : src[pos];
    };

    // Pass 2: build one rectangle per pixel from the (smoothed) envelope
    // height, filled with its mixed color.
    for (int pos = 0; pos < length; ++pos) {
        const float fpos = static_cast<float>(pos);
        const float h = smoothAt(m_envelopeHeight, pos);
        m_vertices.addRectangle(fpos - 0.5f,
                halfBreadth - heightFactor * h,
                fpos + 0.5f,
                halfBreadth + heightFactor * h);
        m_colors.addForRectangle(m_mixColor[static_cast<size_t>(pos) * 3 + 0],
                m_mixColor[static_cast<size_t>(pos) * 3 + 1],
                m_mixColor[static_cast<size_t>(pos) * 3 + 2],
                1.0f);
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
            colorLocation, GL_FLOAT, m_colors.constData(), 4);

    glDrawArrays(GL_TRIANGLES, 0, m_vertices.size());

    m_shader.disableAttributeArray(positionLocation);
    m_shader.disableAttributeArray(colorLocation);
    m_shader.release();
}

} // namespace allshader
