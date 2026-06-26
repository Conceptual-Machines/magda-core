#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "core/ParameterInfo.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Shared single-row faceplate for the compiled-Faust drum-machine voices
 *        (Kick / Snare / Clap / Hat / Tom).
 *
 * Each voice is a thin compiled instrument with a handful of host-slot knobs, so
 * one generic faceplate serves them all: it builds a labelled value box per host
 * slot from the device's ParameterInfo list (voice macros + the trailing Gain),
 * titled with the voice name. Every box is a LinkableTextSlider carrying its host
 * slot index via setParamIndex(), so mod / macro / automation / MIDI-Learn drag
 * linking is wired by the standard DeviceSlotComponent::setupCustomUILinking()
 * path, exactly like PolySynthUI. The manager pushes live values in via
 * updateFromParameters().
 */
class DrumVoiceUI : public juce::Component {
  public:
    explicit DrumVoiceUI(juce::String title);
    ~DrumVoiceUI() override;

    /// True if `pluginId` is one of the drum-machine voice devices.
    static bool handles(const juce::String& pluginId);
    /// Faceplate title for a drum-voice `pluginId` (empty if not a drum voice).
    static juce::String titleFor(const juce::String& pluginId);

    /// Push current parameter values (and ranges) into the matching boxes,
    /// building the boxes on first call once the slot count is known.
    void updateFromParameters(const std::vector<magda::ParameterInfo>& params);

    /// Flat list of every box (host slot index carried via setParamIndex).
    /// Consumed by DeviceSlotComponent::setupCustomUILinking().
    std::vector<LinkableTextSlider*> getLinkableSliders();

    /// Width the slot wants, derived from the built box count.
    int preferredContentWidth() const;

    std::function<void(int paramIndex, float value)> onParameterChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    struct Control {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<LinkableTextSlider> slider;
    };

    // Build one labelled box per host slot (idempotent: only grows to `count`).
    void ensureControls(int count);

    juce::String title_;
    std::vector<Control> controls_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumVoiceUI)
};

}  // namespace magda::daw::ui
