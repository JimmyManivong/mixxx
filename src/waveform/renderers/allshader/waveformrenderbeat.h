#pragma once

#include <QColor>

#include "shaders/unicolorshader.h"
#include "util/class.h"
#include "waveform/renderers/allshader/vertexdata.h"
#include "waveform/renderers/allshader/waveformrenderer.h"

class QDomNode;
class SkinContext;

namespace allshader {
class WaveformRenderBeat;
}

class allshader::WaveformRenderBeat final : public allshader::WaveformRenderer {
  public:
    explicit WaveformRenderBeat(WaveformWidgetRenderer* waveformWidget,
            ::WaveformRendererAbstract::PositionSource type =
                    ::WaveformRendererAbstract::Play);

    void setup(const QDomNode& node, const SkinContext& context) override;
    void paintGL() override;
    void initializeGL() override;

  private:
    mixxx::UnicolorShader m_shader;
    QColor m_color;
    QColor m_highlightColor;  // Color for the white downbeat lines (Rekordbox style)
    VertexData m_vertices;       // Grey "T" markers for normal beats
    VertexData m_dotVertices;    // White vertical lines for downbeats
    VertexData m_triangleVertices;  // Red triangles top/bottom on downbeats

    bool m_isSlipRenderer;

    // CUSTOM: cached audio onset/offset (audio-frame positions of the first and
    // last waveform sample carrying real signal) so the beat grid can be hidden
    // over the silence before and after the sound. Recomputed when the waveform
    // data pointer changes.
    const void* m_cachedSignalData{nullptr};
    double m_signalStartFrame{0.0};
    double m_signalEndFrame{0.0};

    DISALLOW_COPY_AND_ASSIGN(WaveformRenderBeat);
};
