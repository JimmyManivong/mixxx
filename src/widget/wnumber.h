#pragma once

#include "widget/wlabel.h"

class WNumber : public WLabel  {
    Q_OBJECT
  public:
    explicit WNumber(QWidget* pParent = nullptr);

    void setup(const QDomNode& node, const SkinContext& context) override;

    void onConnectedControlChanged(double dParameter, double dValue) override;

  public slots:
    virtual void setValue(double dValue);

  protected:
    // Number of digits to round to.
    int m_iNoDigits;
    // CUSTOM (RekordboxPi): when > 0, the decimal part (dot included) is
    // rendered at this pixel size via rich text, smaller than the integer
    // part - the CDJ/XDJ BPM readout look ("150" big, ".0" small).
    int m_iSmallDecimalsPx;
};
