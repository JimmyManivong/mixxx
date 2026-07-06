#pragma once

#include <vector>

#include "shaders/rgbashader.h"
#include "util/class.h"
#include "waveform/renderers/allshader/rgbadata.h"
#include "waveform/renderers/allshader/vertexdata.h"
#include "waveform/renderers/allshader/waveformrenderersignalbase.h"

namespace allshader {
class WaveformRendererThreeBand;
}

class allshader::WaveformRendererThreeBand final : public allshader::WaveformRendererSignalBase {
  public:
    explicit WaveformRendererThreeBand(WaveformWidgetRenderer* waveformWidget);

    void onSetup(const QDomNode& node) override;
    void initializeGL() override;
    void paintGL() override;

  private:
    mixxx::RGBAShader m_shader;
    VertexData m_vertices;
    RGBAData m_colors;
    // Per-pixel envelope height (reused each frame), for spatial smoothing.
    std::vector<float> m_envelopeHeight;
    // Per-pixel mixed RGB color, flattened as [r0,g0,b0, r1,g1,b1, ...].
    std::vector<float> m_mixColor;

    DISALLOW_COPY_AND_ASSIGN(WaveformRendererThreeBand);
};
