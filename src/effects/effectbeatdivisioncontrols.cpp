#include "effects/effectbeatdivisioncontrols.h"

#include <cmath>

#include "moc_effectbeatdivisioncontrols.cpp"

namespace {
constexpr double kActiveEpsilon = 1e-6;
} // namespace

EffectBeatDivisionControls::EffectBeatDivisionControls(
        const QString& group, const QString& itemPrefix) {
    for (size_t i = 0; i < kDivisions.size(); ++i) {
        const QString label = QString::fromLatin1(kDivisionLabels[i]);
        const double division = kDivisions[i];

        m_pControlSet[i] = std::make_unique<ControlPushButton>(
                ConfigKey(group, itemPrefix + QStringLiteral("_beat_division_") + label + QStringLiteral("_set")));
        connect(m_pControlSet[i].get(),
                &ControlObject::valueChanged,
                this,
                [this, division](double value) {
                    if (value == 0) {
                        return;
                    }
                    emit divisionActivated(division);
                },
                Qt::DirectConnection);

        m_pControlActive[i] = std::make_unique<ControlObject>(
                ConfigKey(group, itemPrefix + QStringLiteral("_beat_division_") + label + QStringLiteral("_active")));
        m_pControlActive[i]->setReadOnly();
    }
}

EffectBeatDivisionControls::~EffectBeatDivisionControls() = default;

void EffectBeatDivisionControls::updateActiveIndicators(double currentRawValue, bool unitsAreBeats) {
    for (size_t i = 0; i < kDivisions.size(); ++i) {
        bool isActive = unitsAreBeats &&
                std::abs(currentRawValue - kDivisions[i]) < kActiveEpsilon;
        m_pControlActive[i]->forceSet(isActive ? 1.0 : 0.0);
    }
}
