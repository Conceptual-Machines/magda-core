#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <functional>

#include "../core/TempoMap.hpp"

namespace magda {

/**
 * @brief TempoMap implementation backed by te::Edit::tempoSequence.
 *
 * Delegates every conversion to the engine's tempo sequence, which is the
 * single source of truth for tempo. Holds no scalar tempo copy.
 *
 * The current Edit is resolved through an editProvider callback rather than a
 * stored reference, because the Edit can be recreated (project load, reset).
 * When no Edit exists yet, conversions fall back to a constant DEFAULT_BPM so
 * early UI queries don't crash or divide by zero.
 */
class TracktionTempoMap final : public TempoMap {
  public:
    explicit TracktionTempoMap(std::function<tracktion::Edit*()> editProvider)
        : editProvider_(std::move(editProvider)) {}

    double beatToTime(double beat) const override {
        if (auto* edit = currentEdit())
            return edit->tempoSequence.toTime(tracktion::BeatPosition::fromBeats(beat)).inSeconds();
        return beat * 60.0 / DEFAULT_BPM;
    }

    double timeToBeat(double seconds) const override {
        if (auto* edit = currentEdit())
            return edit->tempoSequence.toBeats(tracktion::TimePosition::fromSeconds(seconds))
                .inBeats();
        return seconds * DEFAULT_BPM / 60.0;
    }

    double bpmAt(double beat) const override {
        if (auto* edit = currentEdit())
            return edit->tempoSequence.getBpmAtBeat(tracktion::BeatPosition::fromBeats(beat));
        return DEFAULT_BPM;
    }

  private:
    static constexpr double DEFAULT_BPM = 120.0;

    tracktion::Edit* currentEdit() const {
        return editProvider_ ? editProvider_() : nullptr;
    }

    std::function<tracktion::Edit*()> editProvider_;
};

}  // namespace magda
