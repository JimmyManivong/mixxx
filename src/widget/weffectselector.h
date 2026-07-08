#pragma once

#include <QComboBox>

#include "effects/defs.h"
#include "widget/wbasewidget.h"

class EffectsManager;
class QDomNode;
class SkinContext;

class WEffectSelector : public QComboBox, public WBaseWidget {
    Q_OBJECT
  public:
    WEffectSelector(QWidget* pParent, EffectsManager* pEffectsManager);

    void setup(const QDomNode& node, const SkinContext& context);

    // CUSTOM: pins a normal-sized per-item font (Qt::FontRole) onto every
    // list entry right before opening, when the combo's stylesheet font is
    // unusually large (this fork's 20px CDJ effect-name box). FontRole is
    // the one font channel respected by every popup render path, including
    // macOS's menu-style popup delegate which ignores the view's font
    // entirely. This can't happen in populate(): that runs during skin
    // setup, before the stylesheet font is polished onto the widget, so a
    // size check there still sees the small default font.
    void showPopup() override;

  private slots:
    void slotEffectUpdated();
    void slotEffectSelected(int newIndex);
    void populate();
    bool event(QEvent* pEvent) override;

  private:
    EffectsManager* m_pEffectsManager;
    VisibleEffectsListPointer m_pVisibleEffectsList;
    EffectSlotPointer m_pEffectSlot;
};
