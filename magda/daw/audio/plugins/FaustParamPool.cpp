#include "FaustParamPool.hpp"

#include <algorithm>

namespace magda::daw::audio {

namespace {

void resetSlot(FaustParamSlot& slot, int index) {
    slot.index = index;
    slot.active = false;
    slot.zone = nullptr;
    // Leave label / unit / kind / range / choices alone so historical
    // state is recoverable for diagnostics (and so a slot moving
    // active→inactive→active across a DSP swap restores cleanly when
    // the same control reappears at the same idx).
}

void fillSlot(FaustParamSlot& slot, int index, const HarvestedControl& h) {
    slot.index = index;
    slot.active = true;
    slot.label = h.label;
    slot.unit = h.metadata.unit;
    slot.group = h.group;
    // Style menu/radio overrides the kind even if the harvester
    // reported Continuous — Faust users do `hslider("Mode
    // [style:menu{…}]", …)` and expect a dropdown.
    slot.kind = h.metadata.isMenuStyle ? FaustParamSlot::Kind::Discrete : h.kind;
    slot.minValue = h.minValue;
    slot.maxValue = h.maxValue;
    slot.stepValue = h.stepValue;
    slot.defaultValue = h.defaultValue;
    slot.logScale = h.metadata.logScale;
    slot.choices = h.metadata.menuChoices;
    slot.zone = h.zone;
    slot.role = h.metadata.role;
    slot.hidden = h.metadata.hidden;
    slot.gateSlotIndex = h.metadata.gateSlotIndex;
    slot.gateNegated = h.metadata.gateNegated;
    slot.scaleAnchor = h.metadata.scaleAnchor;
}

FaustParamPool::ActiveBindingDescriptor descriptorFor(const FaustParamSlot& slot) {
    FaustParamPool::ActiveBindingDescriptor d;
    d.slotIndex = slot.index;
    d.zone = slot.zone;
    d.kind = slot.kind;
    d.minValue = slot.minValue;
    d.maxValue = slot.maxValue;
    d.stepValue = slot.stepValue;
    d.logScale = slot.logScale;
    d.scaleAnchor = slot.scaleAnchor;
    d.role = slot.role;
    d.gateSlotIndex = slot.gateSlotIndex;
    d.gateNegated = slot.gateNegated;
    if (slot.kind == FaustParamSlot::Kind::Discrete) {
        // Sort by underlying value so the dropdown index → real-value
        // lookup matches the order users see (and matches what
        // paramInfoFromSlot produces for ParameterInfo::choices).
        auto sorted = slot.choices;
        std::sort(sorted.begin(), sorted.end(),
                  [](const std::pair<float, juce::String>& a,
                     const std::pair<float, juce::String>& b) { return a.first < b.first; });
        d.discreteValues.reserve(sorted.size());
        for (const auto& c : sorted)
            d.discreteValues.push_back(c.first);
    }
    return d;
}

}  // namespace

FaustParamPool::FaustParamPool() {
    for (int i = 0; i < kSize; ++i)
        resetSlot(slots_[static_cast<size_t>(i)], i);
}

void FaustParamPool::clearActive() {
    for (auto& slot : slots_) {
        slot.active = false;
        slot.zone = nullptr;
    }
}

int FaustParamPool::activeCount() const {
    int n = 0;
    for (const auto& s : slots_)
        if (s.active)
            ++n;
    return n;
}

FAUSTFLOAT* FaustParamPool::getProjectTempoZone() const {
    for (const auto& s : slots_) {
        if (s.active && s.role == FaustControlRole::ProjectTempo)
            return s.zone;
    }
    return nullptr;
}

FaustParamPool::RebindReport FaustParamPool::rebindFromHarvest(
    const std::vector<HarvestedControl>& harvested) {
    RebindReport report;

    // Start every slot inactive; specific slots get re-flagged active
    // below as they're claimed.
    for (auto& slot : slots_) {
        slot.active = false;
        slot.zone = nullptr;
    }

    std::array<bool, kSize> claimed{};
    claimed.fill(false);
    std::vector<size_t> deferred;  // controls without a usable [idx:N]
    deferred.reserve(harvested.size());

    // ── Pass 1: idx-tagged controls ──────────────────────────────────
    for (size_t i = 0; i < harvested.size(); ++i) {
        const auto& h = harvested[i];
        const int requested = h.metadata.slotIndex;
        if (requested < 0 || requested >= kSize) {
            if (requested != -1) {
                report.diagnostics.push_back(juce::String("[idx:") + juce::String(requested) +
                                             "] out of range; using encounter order for \"" +
                                             h.label + "\"");
            }
            deferred.push_back(i);
            continue;
        }
        if (claimed[static_cast<size_t>(requested)]) {
            report.diagnostics.push_back(juce::String("duplicate [idx:") + juce::String(requested) +
                                         "] on \"" + h.label + "\"; using encounter order");
            deferred.push_back(i);
            continue;
        }
        claimed[static_cast<size_t>(requested)] = true;
        fillSlot(slots_[static_cast<size_t>(requested)], requested, h);
        report.activeBindings.push_back(descriptorFor(slots_[static_cast<size_t>(requested)]));
    }

    // ── Pass 2: encounter order into the next free slot ──────────────
    int dropped = 0;
    int cursor = 0;
    for (auto i : deferred) {
        while (cursor < kSize && claimed[static_cast<size_t>(cursor)])
            ++cursor;
        if (cursor >= kSize) {
            ++dropped;
            continue;
        }
        claimed[static_cast<size_t>(cursor)] = true;
        const auto& h = harvested[i];
        fillSlot(slots_[static_cast<size_t>(cursor)], cursor, h);
        report.activeBindings.push_back(descriptorFor(slots_[static_cast<size_t>(cursor)]));
        ++cursor;
    }

    if (dropped > 0) {
        report.diagnostics.push_back(juce::String(dropped) +
                                     " control(s) dropped: pool overflow (max " +
                                     juce::String(kSize) + ")");
    }

    return report;
}

}  // namespace magda::daw::audio
