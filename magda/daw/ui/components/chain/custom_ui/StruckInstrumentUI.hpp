#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "core/ParameterInfo.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"

namespace magda::daw::audio::compiled {
class MagdaCompiledPolyInstrument;
}

namespace magda::daw::ui {

/**
 * @brief Shared faceplate for the struck-modal instruments (Marimba / Djembe /
 *        Bell), all of which are exciter -> modal-resonator devices.
 *
 * Layout mirrors the Materia (Elements) faceplate: an instrument-specific body
 * graphic on the left (a tone bar, a membrane, or a bell cross-section) whose
 * strike point you drag to set Strike Position, and EXCITER | RESONATOR knob
 * columns on the right. Every knob is a LinkableTextSlider carrying its host slot
 * via setParamIndex(), so mod / macro / automation / MIDI-Learn linking is wired
 * by the standard DeviceSlotComponent::setupCustomUILinking() path. The Position
 * knob and the body share the same slot, so dragging either keeps both in sync.
 *
 * Binding the live plugin (setLivePlugin) lets the body flash on each note-on,
 * polled from the plugin's strikePulse() counter on a timer.
 */
class StruckInstrumentUI : public juce::Component, private juce::Timer {
  public:
    explicit StruckInstrumentUI(const juce::String& pluginId);
    ~StruckInstrumentUI() override;

    /// True if `pluginId` is one of the struck-modal instrument devices.
    static bool handles(const juce::String& pluginId);

    void updateFromParameters(const std::vector<magda::ParameterInfo>& params);
    std::vector<LinkableTextSlider*> getLinkableSliders();
    int preferredContentWidth() const;

    /// Bind the live plugin so the body can flash on note-on (nullptr unbinds).
    void setLivePlugin(magda::daw::audio::compiled::MagdaCompiledPolyInstrument* plugin);

    std::function<void(int paramIndex, float value)> onParameterChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

  private:
    enum class Kind { Marimba, Djembe, Bell };

    struct Control {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<LinkableTextSlider> slider;
    };

    void timerCallback() override;
    void ensureControls(int count);
    // Lay a vertical stack of [label + slider] rows into `area`, top-aligned.
    void layoutColumn(juce::Rectangle<int> area, const std::vector<int>& slots, int rowH, int gap);
    // Draw the instrument body + strike dot into bodyArea_.
    void paintBody(juce::Graphics& g);
    // Strike-point pixel position for the current Position value (and inverse).
    juce::Point<float> strikePoint() const;
    void setPositionFromPoint(juce::Point<int> p);

    float positionValue() const;  // Strike Position slot, clamped 0..1
    float decayNorm() const;      // Decay slot normalised 0..1 (for the body glow)

    const Kind kind_;
    juce::String title_;
    std::vector<int> exciterSlots_, resonatorSlots_;
    int gainSlot_ = -1;
    static constexpr int kPositionSlot = 0;

    std::vector<Control> controls_;
    std::vector<float> slotMin_, slotMax_;

    juce::Rectangle<int> bodyArea_, exciterArea_, resonatorArea_;

    magda::daw::audio::compiled::MagdaCompiledPolyInstrument* plugin_ = nullptr;
    std::uint32_t lastStrikePulse_ = 0;
    float flash_ = 0.0f;  // strike-flash level, decays each timer tick
    // Djembe only: the angle of the strike dot on the membrane. Position is the
    // radius (center->edge); the angle is UI state so the dot tracks the mouse
    // instead of snapping to a fixed axis (default straight up).
    float djembeAngle_ = -1.5707963f;  // -pi/2

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StruckInstrumentUI)
};

}  // namespace magda::daw::ui
