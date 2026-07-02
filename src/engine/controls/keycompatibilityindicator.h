#pragma once

#include <QObject>
#include <memory>
#include <vector>

class ControlObject;
class ControlProxy;

/// CUSTOM (Rekordbox-style harmonic mixing aid): publishes a per-deck control
/// `[ChannelN],key_compatible` that is 1.0 when that deck's musical key is
/// harmonically compatible (Circle of Fifths) with at least one OTHER loaded
/// deck, and 0.0 otherwise. Skins bind it to a key widget's highlight so the key
/// lights up only when the decks would mix in key.
class KeyCompatibilityIndicator : public QObject {
    Q_OBJECT
  public:
    explicit KeyCompatibilityIndicator(int numDecks, QObject* parent = nullptr);
    ~KeyCompatibilityIndicator() override;

  private slots:
    void slotKeyChanged(double value);

  private:
    void recompute();

    struct Deck {
        std::unique_ptr<ControlProxy> pKey;            // reads [ChannelN],key
        std::unique_ptr<ControlObject> pKeyCompatible; // writes [ChannelN],key_compatible
    };
    std::vector<Deck> m_decks;
};
