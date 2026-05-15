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
 *        draggable crossover, threshold, and expander handles.
 *
 * Plots three colour-tinted bands on a log-frequency axis.  The two vertical
 * lines are the Low / High crossover frequencies.  Per band: two horizontal
 * lines for the above/below thresholds plus an optional expander threshold
 * below those.  Scroll wheel on the above-threshold zone adjusts ratioAbove;
 * scroll on the below-threshold zone adjusts ratioBelow.
 */
class CompiledMultibandCurveView final : public juce::Component,
                                         public CompiledDevicePanel,
                                         private juce::Timer {
  public:
    explicit CompiledMultibandCurveView(juce::String pluginId);

    int getPreferredHeight() const {
        return 140;
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
    void setOnLayoutChanged(std::function<void()> cb) override {
        onLayoutChanged_ = std::move(cb);
    }
    bool wantsFullBody() const override;
    int preferredHeight() const override {
        return getPreferredHeight();
    }

    std::function<void(int slotIndex, float displayValue)> onParameterChanged;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

  private:
    enum class Handle {
        None,
        LowXo,
        HighXo,
        LowThreshAbove,
        LowThreshBelow,
        LowThreshExpandBelow,
        LowThreshExpandAbove,
        LowLimit,
        MidThreshAbove,
        MidThreshBelow,
        MidThreshExpandBelow,
        MidThreshExpandAbove,
        MidLimit,
        HighThreshAbove,
        HighThreshBelow,
        HighThreshExpandBelow,
        HighThreshExpandAbove,
        HighLimit,
    };

    void timerCallback() override;
    void resampleFromPlugin();

    float xToFreq(float x) const;
    float freqToX(float hz) const;
    float dbToY(float db) const;
    float yToDb(float y) const;

    static bool isThresholdHandle(Handle h);
    static bool isExpandHandle(Handle h);
    static int thresholdBandIndex(Handle h);
    static bool isAboveThresholdHandle(Handle h);
    static int thresholdSlotForHandle(Handle h);

    Handle pickHandle(float x, float y) const;

    int bandAtX(float x) const;
    // Returns the ratio slot for a band (above=true → ratioAbove, above=false → ratioBelow).
    int ratioSlotForBand(int band, bool above) const;

    magda::daw::audio::compiled::MagdaMultibandCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;

    float lowXoHz_ = 120.0f;
    float highXoHz_ = 2500.0f;
    std::array<float, 3> threshAboveDb_{{-24.0f, -24.0f, -24.0f}};
    std::array<float, 3> threshBelowDb_{{-48.0f, -48.0f, -48.0f}};
    std::array<float, 3> threshExpandBelowDb_{{-72.0f, -72.0f, -72.0f}};
    std::array<float, 3> threshExpandAboveDb_{{0.0f, 0.0f, 0.0f}};
    std::array<float, 3> ratiosAbove_{{8.0f, 8.0f, 8.0f}};
    std::array<float, 3> ratiosBelow_{{8.0f, 8.0f, 8.0f}};
    std::array<float, 3> expandRatiosBelow_{{1.0f, 1.0f, 1.0f}};
    std::array<float, 3> expandRatiosAbove_{{1.0f, 1.0f, 1.0f}};
    std::array<float, 3> limitDb_{{0.0f, 0.0f, 0.0f}};
    Handle hoveredHandle_ = Handle::None;
    Handle draggedHandle_ = Handle::None;
    juce::Rectangle<float> plotArea_;

    juce::Rectangle<float> collapseButtonArea_;
    bool collapseButtonHovered_ = false;
    // Which band is receiving a ratio scroll.  -1 = no active scroll.
    // ratioScrollZone_: 0 = ratioAbove, 1 = ratioBelow, 2 = expandRatioBelow, 3 = expandRatioAbove.
    int ratioScrollBand_ = -1;
    int ratioScrollZone_ = 0;

    std::function<void()> onLayoutChanged_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledMultibandCurveView)
};

}  // namespace magda::daw::ui
