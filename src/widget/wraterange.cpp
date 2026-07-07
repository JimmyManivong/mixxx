#include "widget/wraterange.h"

#include "control/controlproxy.h"
#include "moc_wraterange.cpp"
#include "skin/legacy/skincontext.h"

WRateRange::WRateRange(const QString& group, QWidget* parent)
        : WNumber(parent),
          m_nodePosition(VerticalPosition::Top),
          m_nodeDisplay(DisplayType::Default) {
    m_pRateRangeControl = new ControlProxy(
            group, "rateRange", this, ControlFlag::NoAssertIfMissing);
    m_pRateRangeControl->connectValueChanged(this, &WRateRange::setValue);
    m_pRateDirControl = new ControlProxy(
            group, "rate_dir", this, ControlFlag::NoAssertIfMissing);
    m_pRateDirControl->connectValueChanged(this, &WRateRange::slotRateDirChanged);
}

void WRateRange::setup(const QDomNode& node, const SkinContext& context) {
    WNumber::setup(node, context);

    QDomElement RateRangePosition = context.selectElement(node, "Position");
    QDomElement RateRangeType = context.selectElement(node, "Display");
    m_nodePosition = RateRangePosition.text() == "Top"
            ? VerticalPosition::Top
            : VerticalPosition::Bottom;
    if (RateRangeType.text() == "prefix") {
        m_nodeDisplay = DisplayType::Prefix;
    } else if (RateRangeType.text() == "range") {
        m_nodeDisplay = DisplayType::Range;
    } else if (RateRangeType.text() == "cdj") {
        m_nodeDisplay = DisplayType::Cdj;
    } else {
        m_nodeDisplay = DisplayType::Default;
    }
    if (m_nodeDisplay != DisplayType::Cdj) {
        // Legacy behaviour. The Cdj display keeps the skin's <Alignment>
        // instead, so it can be right-aligned under the BPM box.
        setAlignment(Qt::AlignCenter);
    }

    // Initialize the widget (overrides the base class initial value).
    const double range = m_pRateRangeControl->get();
    setValue(range);
}

void WRateRange::slotRateDirChanged(double dir) {
    Q_UNUSED(dir);

    const double range = m_pRateRangeControl->get();
    setValue(range);
}

void WRateRange::setValue(double range) {
    const double direction = m_pRateDirControl->get();

    QString prefix('-');
    if (m_nodePosition == VerticalPosition::Top && direction > 0) {
        prefix = '+';
    }

    if (m_nodePosition == VerticalPosition::Bottom && direction < 0) {
        prefix = '+';
    }

    if (m_nodeDisplay == DisplayType::Prefix) {
        m_nodeText = prefix;
    } else if (m_nodeDisplay == DisplayType::Range) {
        m_nodeText = QString::number(range * 100);
    } else if (m_nodeDisplay == DisplayType::Cdj) {
        // Pioneer TEMPO RANGE readout. 0.89 rather than exactly 1.0 so a
        // range tweaked from the preferences (e.g. 90%) still reads WIDE.
        if (range >= 0.89) {
            m_nodeText = QStringLiteral("WIDE");
        } else {
            m_nodeText = QChar(0x00B1) + QString::number(qRound(range * 100));
        }
    } else {
        m_nodeText = prefix.append(QString::number(range * 100));
    }

    setText(m_nodeText);
}
