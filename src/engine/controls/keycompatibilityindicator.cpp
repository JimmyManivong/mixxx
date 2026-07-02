#include "engine/controls/keycompatibilityindicator.h"

#include "control/controlobject.h"
#include "control/controlproxy.h"
#include "mixer/playermanager.h"
#include "moc_keycompatibilityindicator.cpp"
#include "track/keyutils.h"

namespace {
mixxx::track::io::key::ChromaticKey keyFromControl(double value) {
    const int i = static_cast<int>(value);
    if (i <= mixxx::track::io::key::INVALID ||
            i > mixxx::track::io::key::B_MINOR) {
        return mixxx::track::io::key::INVALID;
    }
    return static_cast<mixxx::track::io::key::ChromaticKey>(i);
}
} // namespace

KeyCompatibilityIndicator::KeyCompatibilityIndicator(int numDecks, QObject* parent)
        : QObject(parent) {
    m_decks.reserve(numDecks);
    for (int i = 0; i < numDecks; ++i) {
        const QString group = PlayerManager::groupForDeck(i);
        Deck deck;
        deck.pKey = std::make_unique<ControlProxy>(group, QStringLiteral("key"), this);
        deck.pKey->connectValueChanged(this, &KeyCompatibilityIndicator::slotKeyChanged);
        deck.pKeyCompatible = std::make_unique<ControlObject>(
                ConfigKey(group, QStringLiteral("key_compatible")));
        m_decks.push_back(std::move(deck));
    }
    recompute();
}

KeyCompatibilityIndicator::~KeyCompatibilityIndicator() = default;

void KeyCompatibilityIndicator::slotKeyChanged(double value) {
    Q_UNUSED(value);
    recompute();
}

void KeyCompatibilityIndicator::recompute() {
    for (size_t i = 0; i < m_decks.size(); ++i) {
        const auto keyI = keyFromControl(m_decks[i].pKey->get());
        bool compatible = false;
        if (keyI != mixxx::track::io::key::INVALID) {
            const QList<mixxx::track::io::key::ChromaticKey> compatibleKeys =
                    KeyUtils::getCompatibleKeys(keyI);
            for (size_t j = 0; j < m_decks.size(); ++j) {
                if (j == i) {
                    continue;
                }
                const auto keyJ = keyFromControl(m_decks[j].pKey->get());
                if (keyJ != mixxx::track::io::key::INVALID &&
                        compatibleKeys.contains(keyJ)) {
                    compatible = true;
                    break;
                }
            }
        }
        m_decks[i].pKeyCompatible->set(compatible ? 1.0 : 0.0);
    }
}
