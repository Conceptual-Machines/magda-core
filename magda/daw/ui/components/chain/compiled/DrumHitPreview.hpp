// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace magda::daw::audio::compiled {
class MagdaCompiledPolyInstrument;
}

namespace magda::daw::ui {

/**
 * @brief Renders one hit of a drum voice on a worker thread, for its faceplate to draw.
 *
 * Only the latest request counts: one queued behind a render replaces any older one, so a knob
 * dragged across its range costs one render per frame the worker can keep up with, not one per
 * value.
 */
class DrumHitPreview final : private juce::Thread, private juce::AsyncUpdater {
  public:
    using Instrument = magda::daw::audio::compiled::MagdaCompiledPolyInstrument;

    struct Hit {
        std::vector<float> samples;
        float lengthMs = 0.0f;
    };

    DrumHitPreview();
    ~DrumHitPreview() override;

    /// Render @p lengthMs of a hit at @p slotValues (real units, by slot) on @p device.
    void request(std::shared_ptr<Instrument> device, std::vector<float> slotValues, float lengthMs);

    /// Called on the message thread with each finished hit.
    std::function<void(const Hit&)> onRendered;

  private:
    struct Job {
        std::shared_ptr<Instrument> device;
        std::vector<float> slotValues;
        float lengthMs = 0.0f;
    };

    void run() override;
    void handleAsyncUpdate() override;

    std::mutex mutex_;
    std::optional<Job> pending_;
    std::optional<Hit> finished_;
};

}  // namespace magda::daw::ui
