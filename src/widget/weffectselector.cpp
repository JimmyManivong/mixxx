#include "widget/weffectselector.h"

#include <QAbstractItemView>
#include <QFontInfo>
#include <QtDebug>

#include "effects/effectsmanager.h"
#include "effects/visibleeffectslist.h"
#include "moc_weffectselector.cpp"
#include "widget/effectwidgetutils.h"

WEffectSelector::WEffectSelector(QWidget* pParent, EffectsManager* pEffectsManager)
        : QComboBox(pParent),
          WBaseWidget(this),
          m_pEffectsManager(pEffectsManager),
          m_pVisibleEffectsList(pEffectsManager->getVisibleEffectsList()) {
    // Prevent this widget from getting focused by Tab/Shift+Tab
    // to avoid interfering with using the library via keyboard.
    // Allow click focus though so the list can always be opened by mouse,
    // see https://github.com/mixxxdj/mixxx/issues/10184
    setFocusPolicy(Qt::ClickFocus);
}

void WEffectSelector::setup(const QDomNode& node, const SkinContext& context) {
    // EffectWidgetUtils propagates NULLs so this is all safe.
    EffectChainPointer pChainSlot = EffectWidgetUtils::getEffectChainFromNode(
            node, context, m_pEffectsManager);
    m_pEffectSlot = EffectWidgetUtils::getEffectSlotFromNode(
            node, context, pChainSlot);

    if (m_pEffectSlot != nullptr) {
        connect(m_pVisibleEffectsList.data(),
                &VisibleEffectsList::visibleEffectsListChanged,
                this,
                &WEffectSelector::populate);
        connect(m_pEffectSlot.data(),
                &EffectSlot::effectChanged,
                this,
                &WEffectSelector::slotEffectUpdated);
        connect(this,
                QOverload<int>::of(&QComboBox::activated),
                this,
                &WEffectSelector::slotEffectSelected);
    } else {
        SKIN_WARNING(node,
                context,
                QStringLiteral("EffectSelector node could not attach to effect "
                               "slot."));
    }

    populate();
}

void WEffectSelector::populate() {
    blockSignals(true);
    clear();

    const QList<EffectManifestPointer> visibleEffectManifests = m_pVisibleEffectsList->getList();

    // CUSTOM: this fork's CDJ-style effect-name display (#XdjFxName) uses a
    // huge 20px closed-box font, and the dropdown list inherits it. Neither
    // a QSS rule on the popup view nor view()->setFont() can fix that on
    // macOS: there the popup is rendered menu-style (SH_ComboBox_Popup) by
    // QComboMenuDelegate, which takes its font from the combo box itself
    // and ignores the view's font entirely. The one channel every render
    // path respects is the model's per-item Qt::FontRole, so when the
    // combo's own font is unusually large, pin each item's font to a
    // normal list size instead.
    QFont itemFont = font();
    constexpr int kMaxReasonableItemPixelSize = 16;
    const bool useSmallItemFont =
            QFontInfo(itemFont).pixelSize() > kMaxReasonableItemPixelSize;
    if (useSmallItemFont) {
        itemFont.setPixelSize(11);
    }
    // Elide with the same font the items are actually drawn in, otherwise
    // the big closed-box font over-shortens every name.
    QFontMetrics metrics(itemFont);

    // Add empty item: no effect
    addItem(kNoEffectString);
    setItemData(0, QVariant(tr("No effect loaded.")), Qt::ToolTipRole);
    if (useSmallItemFont) {
        setItemData(0, QVariant::fromValue(itemFont), Qt::FontRole);
    }

    for (int i = 0; i < visibleEffectManifests.size(); ++i) {
        const EffectManifestPointer pManifest = visibleEffectManifests.at(i);
        QString elidedDisplayName = metrics.elidedText(pManifest->displayName(),
                Qt::ElideMiddle,
                view()->width() - 2);
        addItem(elidedDisplayName, QVariant(pManifest->uniqueId()));
        if (useSmallItemFont) {
            setItemData(i + 1, QVariant::fromValue(itemFont), Qt::FontRole);
        }

        QString name = pManifest->name();
        QString description = pManifest->description();
        // <b> makes the effect name bold. Also, like <span> it serves as hack
        // to get Qt to treat the string as rich text so it automatically wraps long lines.
        setItemData(i + 1,
                QVariant(QStringLiteral("<b>") + name +
                        QStringLiteral("</b><br/>") + description),
                Qt::ToolTipRole);
    }

    slotEffectUpdated();
    blockSignals(false);
}

void WEffectSelector::slotEffectSelected(int newIndex) {
    const EffectManifestPointer pManifest =
            m_pEffectsManager->getBackendManager()->getManifestFromUniqueId(
                    itemData(newIndex).toString());

    m_pEffectSlot->loadEffectWithDefaults(pManifest);

    setBaseTooltip(itemData(newIndex, Qt::ToolTipRole).toString());
    // Clicking an effect item moves keyboard focus to the list view.
    // Move focus back to the previously focused library widget.
    ControlObject::set(ConfigKey("[Library]", "refocus_prev_widget"), 1);
}

void WEffectSelector::slotEffectUpdated() {
    int newIndex;

    if (m_pEffectSlot != nullptr) {
        if (m_pEffectSlot->getManifest() != nullptr) {
            EffectManifestPointer pManifest = m_pEffectSlot->getManifest();
            newIndex = findData(QVariant(pManifest->uniqueId()));
        } else {
            newIndex = findData(QVariant());
        }
    } else {
        newIndex = findData(QVariant());
    }

    if (kEffectDebugOutput) {
        qDebug() << "WEffectSelector::slotEffectUpdated"
                 << "old" << itemData(currentIndex())
                 << "new" << itemData(newIndex);
    }

    if (newIndex != -1 && newIndex != currentIndex()) {
        setCurrentIndex(newIndex);
        setBaseTooltip(itemData(newIndex, Qt::ToolTipRole).toString());
    }
}

void WEffectSelector::showPopup() {
    // CUSTOM: see the comment on the declaration in the header. Applied
    // before the base call so the popup's size calculations already use
    // the per-item font.
    constexpr int kMaxReasonableItemPixelSize = 16;
    if (QFontInfo(font()).pixelSize() > kMaxReasonableItemPixelSize) {
        QFont itemFont = font();
        itemFont.setPixelSize(11);
        const QVariant fontVariant = QVariant::fromValue(itemFont);
        for (int i = 0; i < count(); ++i) {
            setItemData(i, fontVariant, Qt::FontRole);
        }
    }
    QComboBox::showPopup();
}

bool WEffectSelector::event(QEvent* pEvent) {
    if (pEvent->type() == QEvent::ToolTip) {
        updateTooltip();
    } else if (pEvent->type() == QEvent::Wheel && !hasFocus()) {
        // don't change effect by scrolling hovered effect selector
        return true;
    }

    return QComboBox::event(pEvent);
}
