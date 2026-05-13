#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::audio::compiled {
class MagdaPhaserCompiledPlugin;
}

namespace magda::daw::ui {

/**
 * @brief Animated notch/sweep visual for the compiled phaser.
 *
 * Shows the current LFO sweep window on a log-frequency axis. The two
 * vertical edges are draggable and write Min Hz / Max Hz through the normal
 * device parameter path.
 */
class CompiledPhaserCurveView final : public juce::Component,
                                      public CompiledDevicePanel,
                                      private juce::Timer {
  public:
    explicit CompiledPhaserCurveView(juce::String pluginId);

    int getPreferredHeight() const {
        return 130;
    }

    void setCompiledPlugin(magda::daw::audio::compiled::MagdaPhaserCompiledPlugin* plugin);
    void updateFromDevice(const magda::DeviceInfo& device) override;

    // CompiledDevicePanel
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

    std::function<void(int slotIndex, float displayValue)> onParameterChanged;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

  private:
    enum class Handle { None, MinHz, MaxHz };

    void timerCallback() override;
    void resampleFromPlugin();
    void normalizeSweepRange();

    float xToFreq(float x) const;
    float freqToX(float hz) const;
    Handle pickHandle(float x) const;

    magda::daw::audio::compiled::MagdaPhaserCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;

    float rateHz_ = 0.5f;
    float depth_ = 1.0f;
    float feedback_ = 0.3f;
    int stagesIndex_ = 1;
    float minHz_ = 100.0f;
    float maxHz_ = 2000.0f;
    float mix_ = 0.6f;
    double lfoPhase_ = 0.0;
    juce::uint32 lastTickMs_ = 0;

    Handle hoveredHandle_ = Handle::None;
    Handle draggedHandle_ = Handle::None;
    juce::Rectangle<float> plotArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledPhaserCurveView)
};

}  // namespace magda::daw::ui
