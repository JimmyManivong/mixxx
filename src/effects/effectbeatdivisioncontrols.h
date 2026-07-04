#pragma once

#include <QObject>
#include <QString>
#include <array>
#include <memory>

#include "control/controlobject.h"
#include "control/controlpushbutton.h"
#include "util/class.h"

/// Exposes a fixed set of "beat division" quick-select controls for a single
/// effect parameter slot (e.g. [EffectRack1_EffectUnit1_Effect1],parameter1):
/// one push-button per musical note division (1/16 through 32 beats) that
/// jumps the parameter straight to that exact value, plus a read-only
/// "active" indicator per division so a skin can light up whichever one is
/// currently selected - mirroring the XDJ-style hardware Beat FX division
/// row. Modeled on BeatLoopingControl's beatloop_%1_activate/_enabled
/// pattern (engine/controls/loopingcontrol.cpp), generalized to any knob
/// parameter slot rather than one instance per loop size.
///
/// This is deliberately generic: it doesn't know or care which effect is
/// loaded. Any effect whose first (or any) knob parameter declares
/// UnitsHint::Beats gets this for free via EffectKnobParameterSlot - see
/// effectknobparameterslot.cpp.
class EffectBeatDivisionControls : public QObject {
    Q_OBJECT
  public:
    // Beat divisions in ascending order, paired with the label used in each
    // control's ConfigKey item name (e.g. "parameter1_beat_division_1_4_set").
    // Slashes aren't legal in ConfigKey item names, hence "1_4" not "1/4".
    static constexpr std::array<double, 11> kDivisions = {
            1.0 / 16.0,
            1.0 / 8.0,
            1.0 / 4.0,
            1.0 / 2.0,
            3.0 / 4.0,
            1.0,
            2.0,
            4.0,
            8.0,
            16.0,
            32.0,
    };
    static constexpr std::array<const char*, 11> kDivisionLabels = {
            "1_16",
            "1_8",
            "1_4",
            "1_2",
            "3_4",
            "1",
            "2",
            "4",
            "8",
            "16",
            "32",
    };

    // group: the effect slot's group, e.g. "[EffectRack1_EffectUnit1_Effect1]"
    // itemPrefix: the parameter's item prefix, e.g. "parameter1"
    EffectBeatDivisionControls(const QString& group, const QString& itemPrefix);
    ~EffectBeatDivisionControls() override;

    // Re-lights whichever division's "active" indicator matches
    // currentRawValue (within a small epsilon). Call whenever the parameter's
    // raw value changes for any reason (button press, manual knob turn,
    // meta-knob movement, loading/clearing an effect). If unitsAreBeats is
    // false, every indicator is forced off - a division readout only makes
    // sense for a Beats-unit parameter.
    void updateActiveIndicators(double currentRawValue, bool unitsAreBeats);

  signals:
    // Emitted with the exact raw beat value to jump the parameter to, only
    // when one of the division buttons is pressed.
    void divisionActivated(double rawBeatsValue);

  private:
    std::array<std::unique_ptr<ControlPushButton>, 11> m_pControlSet;
    std::array<std::unique_ptr<ControlObject>, 11> m_pControlActive;

    DISALLOW_COPY_AND_ASSIGN(EffectBeatDivisionControls);
};
