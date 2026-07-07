#include "widget/wnumber.h"

#include "moc_wnumber.cpp"
#include "skin/legacy/skincontext.h"

WNumber::WNumber(QWidget* pParent)
        : WLabel(pParent),
          m_iNoDigits(2),
          m_iSmallDecimalsPx(0) {
}

void WNumber::setup(const QDomNode& node, const SkinContext& context) {
    WLabel::setup(node, context);

    // Number of digits after the decimal.
    context.hasNodeSelectInt(node, "NumberOfDigits", &m_iNoDigits);

    // CUSTOM (RekordboxPi): optional CDJ-style smaller decimals (see .h).
    context.hasNodeSelectInt(node, "SmallDecimalsPx", &m_iSmallDecimalsPx);

    setValue(0.);
}

void WNumber::onConnectedControlChanged(double dParameter, double dValue) {
    Q_UNUSED(dParameter);
    // We show the actual control value instead of its parameter.
    setValue(dValue);
}

void WNumber::setValue(double dValue) {
    QString number = QString::number(dValue, 'f', m_iNoDigits);
    if (m_iSmallDecimalsPx > 0) {
        const int dot = number.lastIndexOf(QLatin1Char('.'));
        if (dot >= 0) {
            // Rich text (QLabel auto-detects the tag) so the fractional part
            // renders smaller than the integer part, like a CDJ BPM readout.
            number = number.left(dot) +
                    QStringLiteral("<span style=\"font-size:%1px;\">%2</span>")
                            .arg(m_iSmallDecimalsPx)
                            .arg(number.mid(dot));
        }
    }
    if (m_skinText.contains("%1")) {
        setText(m_skinText.arg(number));
    } else {
        setText(m_skinText + number);
    }
}
