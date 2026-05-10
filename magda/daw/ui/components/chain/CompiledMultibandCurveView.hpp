#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/DeviceInfo.hpp"

namespace magda::daw::audio::compiled {
class MagdaMultibandCompiledPlugin;
}

namespace magda::daw::ui {

/**
 * @brief Spectrum band visual for the compiled multiband compressor with
 *        draggable crossover handles.
 *
 * Plots three colour-tinted bands on a log-frequency axis. The two
 * vertical lines between them are the Low / High crossover frequencies;
 * the user can mouse-down on either line and drag horizontally to set
 * the crossover. Drags fire `onParameterChanged(slotIndex, displayValue)`
 * so DeviceSlotComponent can route the new value through TrackManager
 * (which keeps automation, undo, and the cached DeviceInfo all in sync).
 *
 * Polls host params at ~30 Hz and only repaints when one moves
 * materially (or when a drag is in progress).
 */
class CompiledMultibandCurveView final : public juce::Component, private juce::Timer {
  public:
    explicit CompiledMultibandCurveView(juce::String pluginId);

    int getPreferredHeight() const {
        // Multiband only needs to show three coloured band regions and two
        // crossover handles — a shorter strip reads cleaner and leaves
        // more room in the chain panel for the dense 9-cell knob row.
        return 90;
    }

    void setCompiledPlugin(magda::daw::audio::compiled::MagdaMultibandCompiledPlugin* plugin);
    void updateFromDevice(const magda::DeviceInfo& device);

    /// Fires while the user is dragging a crossover handle. `slotIndex`
    /// is one of MagdaMultibandCompiledPlugin::kLowXoSlot / kHighXoSlot;
    /// `displayValue` is in Hz.
    std::function<void(int slotIndex, float displayValue)> onParameterChanged;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

  private:
    enum class Handle { None, LowXo, HighXo };

    void timerCallback() override;
    void resampleFromPlugin();

    /// Map a pixel x within the plot to a frequency in Hz on the log axis.
    float xToFreq(float x) const;
    /// Inverse — frequency to plot pixel x.
    float freqToX(float hz) const;
    /// Pick the crossover handle nearest to a mouse x position; None if
    /// the cursor isn't close enough to either line.
    Handle pickHandle(float x) const;

    magda::daw::audio::compiled::MagdaMultibandCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;

    float lowXoHz_ = 120.0f;
    float highXoHz_ = 2500.0f;
    Handle hoveredHandle_ = Handle::None;
    Handle draggedHandle_ = Handle::None;
    juce::Rectangle<float> plotArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledMultibandCurveView)
};

}  // namespace magda::daw::ui
