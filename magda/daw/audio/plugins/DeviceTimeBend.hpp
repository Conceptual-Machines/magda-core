#pragma once

#include <magda/sdk/device/ParameterDescriptor.hpp>
#include <magda/sdk/sequencer/PublishedPattern.hpp>
#include <magda/sdk/sequencer/TimeBend.hpp>
#include <magda/sdk/state/StateNode.hpp>

#include "plugins/DeviceJuceInterop.hpp"

/**
 * @file DeviceTimeBend.hpp
 * @brief The time bend a MIDI device plays its steps through (#3044).
 *
 * The curve, on and span persist as one text setting; AMOUNT and CYCLES are parameters, so they
 * take macros and mods.
 */

namespace magda::daw::audio {

/** @brief A device's time bend: restored from its setting, played from a published copy. */
class DeviceTimeBend {
  public:
    /// The setting's property id, the same on every device that has a bend.
    static inline const juce::Identifier settingId{"timeBend"};

    /** @brief Reads the setting from @p state; a missing or unreadable one is the default. */
    void restore(const sdk::StateNode& state) {
        sdk::sequencer::TimeBend bend;
        if (const auto* value = state.find(stateKey(settingId)))
            if (auto read = sdk::sequencer::timeBendFromText(value->toString()))
                bend = *read;
        published_.publish(bend);
    }

    /** @brief The bend last restored. Message thread. */
    sdk::sequencer::TimeBend current() const {
        return published_.current();
    }

    /** @brief The bend for one block, with the parameters' @p amount and @p cycles. */
    sdk::sequencer::TimeBend forBlock(float amount, int cycles) {
        const sdk::sequencer::PublishedPattern<sdk::sequencer::TimeBend>::Hold hold{published_};
        sdk::sequencer::TimeBend bend =
            hold.isValid() ? hold.pattern() : sdk::sequencer::TimeBend{};
        bend.amount = amount;
        bend.cycles = cycles;
        return bend;
    }

    /** @brief The AMOUNT slot's metadata at @p index. */
    static sdk::ParameterDescriptor amountSlot(int index) {
        sdk::ParameterDescriptor info;
        info.index = index;
        info.stableId = "bendAmount";
        info.name = "Bend Amount";
        info.minValue = 0.0f;
        info.maxValue = 1.0f;
        info.defaultValue = 1.0f;
        info.displayFormat = sdk::DisplayFormat::Percent;
        return info;
    }

    /** @brief The CYCLES slot's metadata at @p index. */
    static sdk::ParameterDescriptor cyclesSlot(int index) {
        sdk::ParameterDescriptor info;
        info.index = index;
        info.stableId = "bendCycles";
        info.name = "Bend Cycles";
        info.minValue = 1.0f;
        info.maxValue = static_cast<float>(sdk::sequencer::kMaxTimeBendCycles);
        info.defaultValue = 1.0f;
        return info;
    }

  private:
    sdk::sequencer::PublishedPattern<sdk::sequencer::TimeBend> published_;
};

}  // namespace magda::daw::audio
