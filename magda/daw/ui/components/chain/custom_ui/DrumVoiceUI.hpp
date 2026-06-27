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
    explicit DrumVoiceUI(const juce::String& pluginId);
    ~DrumVoiceUI() override;

    /// A named group of host slots, laid out as one titled column with a small
    /// envelope graph drawn from its attack/decay slots.
    struct Section {
        juce::String title;
        std::vector<int> slots;
        int attackSlot = -1;  // host slot of this layer's Attack (-1 = instant)
        int decaySlot = -1;   // host slot of this layer's primary Decay (-1 = no graph)
        int curveSlot = -1;   // host slot of this layer's decay Curve (-1 = linear)
        int cols = 2;         // value boxes are laid out in this many columns
    };

    /// True if `pluginId` is one of the drum-machine voice devices.
    static bool handles(const juce::String& pluginId);
    /// Faceplate title for a drum-voice `pluginId` (empty if not a drum voice).
    static juce::String titleFor(const juce::String& pluginId);
    /// Section grouping for a voice; empty = single flat row of all slots.
    static std::vector<Section> sectionsFor(const juce::String& pluginId);

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
    // Envelope graphs are interactive: drag the peak/end dots to set
    // attack/decay, scroll over a graph to set its Curve.
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

  private:
    struct Control {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<LinkableTextSlider> slider;
    };
    enum class Drag { None, Attack, Decay };

    // Build one labelled box per host slot (idempotent: only grows to `count`).
    void ensureControls(int count);
    // Lay the given slots out as a label-on-top grid of `cols` columns, each row
    // `rowH` tall, filling `area`.
    void layoutGrid(juce::Rectangle<int> area, const std::vector<int>& slots, int cols, int rowH);

    // Draw a layer's attack->decay envelope (linear segments, matching the dsp)
    // into `area`, with time scaled by `axisMaxMs` so layer lengths compare.
    void drawEnvelope(juce::Graphics& g, juce::Rectangle<int> area, const Section& s,
                      float axisMaxMs);
    // The env's per-section time axis (ms): attack max + decay max from the slots.
    float sectionAxisMaxMs(const Section& s) const;
    // Peak (attack) and end (decay) handle points of section `i`'s graph.
    bool envHandles(int i, juce::Point<float>& peak, juce::Point<float>& end) const;
    // Push `value` (real units) into a slot's box + the host, then repaint.
    void setSlotValue(int slot, float value);

    juce::String title_;
    std::vector<Section> sections_;
    std::vector<Control> controls_;
    std::vector<float> slotMin_;  // real-unit min per slot (drag clamps)
    std::vector<float> slotMax_;  // real-unit max per slot (per-section env axis + clamps)
    // Title + envelope strips per section, cached in resized() for paint().
    std::vector<juce::Rectangle<int>> sectionTitleAreas_;
    std::vector<juce::Rectangle<int>> sectionEnvAreas_;
    int dragSection_ = -1;
    Drag dragKind_ = Drag::None;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumVoiceUI)
};

}  // namespace magda::daw::ui
