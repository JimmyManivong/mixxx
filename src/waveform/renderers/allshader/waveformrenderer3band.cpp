#include "waveform/renderers/allshader/waveformrenderer3band.h"

#include "track/track.h"
#include "util/colorcomponents.h"
#include "waveform/renderers/allshader/matrixforwidgetgeometry.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "waveform/waveform.h"

namespace allshader {

namespace {
constexpr int kLowIdx = 0;
constexpr int kMidIdx = 1;
constexpr int kHighIdx = 2;
// Gains balance the 3 overlapping bands (MAX data). At each pixel the band with
// the largest gained height is the visible outer colour. Low/Mid kept near parity
// so the body reads blue on bass and orange on mid-heavy spots (like Rekordbox),
// and kHighGain is a touch above stock (0.4) for slightly brighter white tips.
constexpr float kLowGain = 1.0f;
constexpr float kMidGain = 1.0f;
constexpr float kHighGain = 0.6f;
// Alpha of the amber mid band: <1 makes it blend with the blue low underneath
// to a brown intermediate tone. Lower = more blue/brown, higher = purer amber.
constexpr float kMidBlendAlpha = 0.78f;
// Body = blend of the per-pixel window AVERAGE and its MAX peak, for low/mid.
// At close zoom a pixel covers ~1 frame so avg == max (no effect, punch kept).
// Zoomed out a pixel covers many frames; pure MAX would fill every pixel to a
// solid block, so leaning on the average restores a readable energy envelope
// (loud=tall, quiet=short) like Rekordbox. 1.0 = old pure-peak look, 0.0 = pure
// average. High band stays pure MAX so transient ticks keep their crispness.
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

    // applyCompensation=false: skip the historical x2 gain. We now read "pregain"
    // (default 1.0) instead of "total_gain" (which was typically ~0.5 after
    // ReplayGain), so the x2 would double the scale and clip the body. Without it
    // the effective scale stays ~1.0 and the bands no longer overflow the height.
    // CUSTOM (Rekordbox-style FROZEN waveform): pass nullptr for the per-band
    // gains so the EQ knobs (low/mid/high kill) do NOT shrink the coloured bands.
    // The waveform always shows the track's real content regardless of EQ, like
    // Rekordbox — only the static band gains below apply.
    float allGain(1.0);
    getGains(&allGain, false, nullptr, nullptr, nullptr);

    float gains[3];
    gains[kLowIdx] = kLowGain;
    gains[kMidIdx] = kMidGain;
    gains[kHighIdx] = kHighGain;

    const float breadth = static_cast<float>(m_waveformRenderer->getBreadth()) * devicePixelRatio;
    const float halfBreadth = breadth / 2.0f;
    const float heightFactor = allGain * halfBreadth / m_maxValue;

    double xVisualFrame = qRound(firstVisualFrame / visualIncrementPerPixel) *
            visualIncrementPerPixel;

    const int numVerticesPerLine = 6;
    const int reserved = numVerticesPerLine * (3 * length + 1);

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

    // Pass 1: gather the gained per-pixel height of each band.
    m_bandHeight[kLowIdx].assign(length, 0.f);
    m_bandHeight[kMidIdx].assign(length, 0.f);
    m_bandHeight[kHighIdx].assign(length, 0.f);
    double xvf = xVisualFrame;
    for (int pos = 0; pos < length; ++pos) {
        const int visualFrameStart = std::lround(xvf - maxSamplingRange);
        const int visualFrameStop = std::lround(xvf + maxSamplingRange);
        const int visualIndexStart = std::max(visualFrameStart * 2, 0);
        const int visualIndexStop =
                std::min(std::max(visualFrameStop, visualFrameStart + 1) * 2, dataSize - 1);

        uchar u8low{}, u8mid{}, u8high{};
        int sumLow = 0, sumMid = 0, cnt = 0;
        for (int chn = 0; chn < 2; chn++) {
            for (int i = visualIndexStart + chn; i < visualIndexStop + chn; i += 2) {
                const WaveformData& wd = data[i];
                u8low = math_max(u8low, wd.filtered.low);
                u8mid = math_max(u8mid, wd.filtered.mid);
                u8high = math_max(u8high, wd.filtered.high);
                sumLow += wd.filtered.low;
                sumMid += wd.filtered.mid;
                ++cnt;
            }
        }
        // Blend window average with the peak so zoomed-out waveforms read as an
        // energy envelope instead of a solid max block (see kBodyPeakMix above).
        const float avgLow = cnt ? static_cast<float>(sumLow) / cnt : 0.f;
        const float avgMid = cnt ? static_cast<float>(sumMid) / cnt : 0.f;
        const float bodyLow = avgLow + (static_cast<float>(u8low) - avgLow) * kBodyPeakMix;
        const float bodyMid = avgMid + (static_cast<float>(u8mid) - avgMid) * kBodyPeakMix;
        m_bandHeight[kLowIdx][pos] = bodyLow * gains[kLowIdx];
        m_bandHeight[kMidIdx][pos] = bodyMid * gains[kMidIdx];
        m_bandHeight[kHighIdx][pos] = static_cast<float>(u8high) * gains[kHighIdx];
        xvf += visualIncrementPerPixel;
    }

    // Spatial (fixed, NOT temporal) moving average -> rounded envelope like
    // Rekordbox. Applied to low/mid only; high (white tips) stays sharp so
    // transients keep their crispness. Radius 0 disables the rounding.
    constexpr int kSmoothRadius = 2;
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

    // Pass 2: build the rectangles from the (smoothed) heights.
    for (int pos = 0; pos < length; ++pos) {
        const float fpos = static_cast<float>(pos);
        for (int eq = kLowIdx; eq < kHighIdx + 1; eq++) {
            const float h = (eq == kHighIdx)
                    ? m_bandHeight[eq][pos]
                    : smoothAt(m_bandHeight[eq], pos);
            m_vertices.addRectangle(fpos - 0.5f,
                    halfBreadth - heightFactor * h,
                    fpos + 0.5f,
                    halfBreadth + heightFactor * h);
            switch (eq) {
            default:
                DEBUG_ASSERT(!"Invalid EQ index");
            case kLowIdx:
                m_colors.addForRectangle(m_lowColor_r, m_lowColor_g, m_lowColor_b, m_lowColor_a);
                break;
            case kMidIdx:
                // Semi-transparent so the amber mid blends with the blue low it
                // overlaps -> a brown intermediate tone (Rekordbox-like), while
                // pure mid stays amber. Needs GL_BLEND (enabled before draw).
                m_colors.addForRectangle(
                        m_midColor_r, m_midColor_g, m_midColor_b, kMidBlendAlpha);
                break;
            case kHighIdx:
                m_colors.addForRectangle(
                        m_highColor_r, m_highColor_g, m_highColor_b, m_highColor_a);
                break;
            }
        }
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

    // Standard alpha blending so the semi-transparent mid band mixes with the
    // blue low beneath it (amber over blue -> brown), giving Rekordbox-like tonal
    // gradation instead of 3 flat opaque colours.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glDrawArrays(GL_TRIANGLES, 0, m_vertices.size());

    m_shader.disableAttributeArray(positionLocation);
    m_shader.disableAttributeArray(colorLocation);
    m_shader.release();
}

} // namespace allshader
