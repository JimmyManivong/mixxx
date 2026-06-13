#pragma once

#include <QColor>

#include "skin/legacy/skincontext.h"
#include "util/class.h"
#include "waveform/renderers/waveformrendererabstract.h"

class WaveformRenderBeat : public WaveformRendererAbstract {
  public:
    explicit WaveformRenderBeat(WaveformWidgetRenderer* waveformWidgetRenderer);
    virtual ~WaveformRenderBeat();

    virtual void setup(const QDomNode& node, const SkinContext& context);
    virtual void draw(QPainter* painter, QPaintEvent* event);

  private:
    QColor m_beatColor;
    QColor m_beatHighlightColor;  // Color for downbeats (1st beat of measure)
    QVector<QLineF> m_beats;
    QVector<QLineF> m_downbeats;  // Separate vector for downbeats

    DISALLOW_COPY_AND_ASSIGN(WaveformRenderBeat);
};
