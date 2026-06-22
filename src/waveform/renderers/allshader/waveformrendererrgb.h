#pragma once

#include "shaders/rgbshader.h"
#include "util/class.h"
#include "waveform/renderers/allshader/rgbdata.h"
#include "waveform/renderers/allshader/vertexdata.h"
#include "waveform/renderers/allshader/waveformrenderersignalbase.h"

namespace allshader {
class WaveformRendererRGB;
}

class allshader::WaveformRendererRGB final : public allshader::WaveformRendererSignalBase {
  public:
    explicit WaveformRendererRGB(WaveformWidgetRenderer* waveformWidget,
            ::WaveformRendererAbstract::PositionSource type =
                    ::WaveformRendererAbstract::Play);

    // override ::WaveformRendererSignalBase
    void onSetup(const QDomNode& node) override;

    void initializeGL() override;
    void paintGL() override;

  private:
    mixxx::RGBShader m_shader;
    VertexData m_vertices;
    RGBData m_colors;

    bool m_isSlipRenderer;

    // Cached global peak of filtered.all over the whole track, used to
    // normalize the waveform height (Rekordbox-style dynamics). Recomputed
    // when the underlying waveform data pointer changes.
    float m_globalPeak{0.f};
    const void* m_cachedPeakData{nullptr};

    DISALLOW_COPY_AND_ASSIGN(WaveformRendererRGB);
};
