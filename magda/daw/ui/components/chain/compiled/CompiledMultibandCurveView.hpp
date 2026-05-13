#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::audio::compiled {
class MagdaMultibandCompiledPlugin;
}

namespace magda::daw::ui {

/**
 * @brief Spectrum band visual for the compiled multiband compressor with
 *        draggable crossover and threshold handles.
 *
 * Plots three colour-tinted bands on a log-frequency axis. The two
 * vertical lines between them are the Low / High crossover frequencies;
 * the horizontal lines inside each band are the above/below thresholds.
 * Drags fire `onParameterChanged(slotIndex, displayValue)` so
 * DeviceSlotComponent can route the new value through TrackManager
 * (which keeps automation, undo, and the cached DeviceInfo all in sync).
 *
 * Polls host params at ~30 Hz and only repaints when one moves
 * materially (or when a drag is in progress).
 */
class CompiledMultibandCurveView final : public juce::Component,
                                         public CompiledDevicePanel,
                                         private juce::Timer {
  public:
    explicit CompiledMultibandCurveView(juce::String pluginId);

    int getPreferredHeight() const {
        return 130;
    }

    void setCompiledPlugin(magda::daw::audio::compiled::MagdaMultibandCompiledPlugin* plugin);
    void updateFromDevice(const magda::DeviceInfo& device) override;

    juce::Component& component() override {
        return *this;
    }
    void bindPlugin(te::Plugin* plugin) override;
    void setOnParameterChanged(std::function<void(int, float)> cb) override {
        onParameterChanged = std::move(cb);
    }
    int preferredHeight() const override {
        return getPreferredHeight();
    }

    /// Fires while the user is dragging a handle. `displayValue` is in
    /// the slot's display unit, e.g. Hz for crossovers and dB for thresholds.
    std::function<void(int slotIndex, float displayValue)> onParameterChanged;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

  private:
    enum class Handle {
        None,
        LowXo,
        HighXo,
        LowThreshAbove,
        LowThreshBelow,
        MidThreshAbove,
        MidThreshBelow,
        HighThreshAbove,
        HighThreshBelow
    };

    void timerCallback() override;
    void resampleFromPlugin();

    /// Map a pixel x within the plot to a frequency in Hz on the log axis.
    float xToFreq(float x) const;
    /// Inverse — frequency to plot pixel x.
    float freqToX(float hz) const;
    /// Map a threshold dB value to plot pixel y.
    float dbToY(float db) const;
    /// Inverse - plot pixel y to threshold dB.
    float yToDb(float y) const;

    static bool isThresholdHandle(Handle handle);
    static int thresholdBandIndex(Handle handle);
    static bool isAboveThresholdHandle(Handle handle);
    static int thresholdSlotForHandle(Handle handle);

    /// Pick the nearest editable handle at a mouse position; None if
    /// the cursor isn't close enough to any editable line.
    Handle pickHandle(float x, float y) const;

    magda::daw::audio::compiled::MagdaMultibandCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;

    float lowXoHz_ = 120.0f;
    float highXoHz_ = 2500.0f;
    std::array<float, 3> threshAboveDb_{{-24.0f, -24.0f, -24.0f}};
    std::array<float, 3> threshBelowDb_{{-48.0f, -48.0f, -48.0f}};
    std::array<float, 3> ratios_{{4.0f, 4.0f, 4.0f}};
    Handle hoveredHandle_ = Handle::None;
    Handle draggedHandle_ = Handle::None;
    juce::Rectangle<float> plotArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledMultibandCurveView)
};

}  // namespace magda::daw::ui
