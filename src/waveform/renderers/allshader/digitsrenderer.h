#pragma once

#include <QOpenGLFunctions>

#include "shaders/textureshader.h"
#include "util/opengltexture2d.h"

namespace allshader {
class DigitsRenderer;
}

class allshader::DigitsRenderer : public QOpenGLFunctions {
  public:
    DigitsRenderer() = default;
    ~DigitsRenderer();

    void init();
    void updateTexture(float fontPointSize, float maxHeight, float devicePixelRatio);
    float draw(const QMatrix4x4& matrix,
            float x,
            float y,
            const QString& s);
    float height() const;

  private:
    mixxx::TextureShader m_shader;
    OpenGLTexture2D m_texture;
    int m_penWidth;
    float m_offset[18]; // CUSTOM: Increased for "Bars" text (17 chars + 1)
    float m_width[17];  // CUSTOM: Increased for "Bars" text
    float m_fontPointSize{};
    float m_height{};
    float m_maxHeight{};
    float m_adjustedFontPointSize{};
    DISALLOW_COPY_AND_ASSIGN(DigitsRenderer);
};
