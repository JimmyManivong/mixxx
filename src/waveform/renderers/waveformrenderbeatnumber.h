#pragma once

#include <QFont>
#include <QPair>
#include <QPointF>
#include <QVector>

#include "waveformrenderersignalbase.h"

class WaveformRenderBeatNumber : public WaveformRendererSignalBase {
  public:
    explicit WaveformRenderBeatNumber(WaveformWidgetRenderer* waveformWidget);
    ~WaveformRenderBeatNumber() override = default;

    void setup(const QDomNode& node, const SkinContext& context) override;
    void draw(QPainter* painter, QPaintEvent* event) override;

  private:
    QFont m_font;
    QColor m_textColor;

    DISALLOW_COPY_AND_ASSIGN(WaveformRenderBeatNumber);
};
