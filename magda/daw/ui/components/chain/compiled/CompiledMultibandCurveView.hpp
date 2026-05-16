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
        LowThreshold,
        LowLimit,
        MidThreshold,
        MidLimit,
        HighThreshold,
        HighLimit,
    };

    void timerCallback() override;
    void resampleFromPlugin();

    float xToFreq(float x) const;
    float freqToX(float hz) const;
    float dbToY(float db) const;
    float yToDb(float y) const;

    int bandAtX(float x) const;
    static int thresholdSlotForBand(int band);
    static int ratioSlotForBand(int band);
    static int rangeSlotForBand(int band);
    static int limitSlotForBand(int band);
    static int bandForHandle(Handle h);
    static bool isLimitHandle(Handle h);
    int slotForHandle(Handle h) const;
    Handle pickHandle(float x, float y) const;

    magda::daw::audio::compiled::MagdaMultibandCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;

    float lowXoHz_ = 120.0f;
    float highXoHz_ = 2500.0f;
    std::array<float, 3> thresholdDb_{{-42.0f, -36.0f, -45.0f}};
    std::array<float, 3> ratio_{{8.0f, 8.0f, 8.0f}};
    std::array<float, 3> rangeDb_{{24.0f, 24.0f, 24.0f}};
    std::array<float, 3> limitDb_{{0.0f, 0.0f, 0.0f}};

    Handle hoveredHandle_ = Handle::None;
    Handle draggedHandle_ = Handle::None;
    juce::Rectangle<float> plotArea_;

    juce::Rectangle<float> collapseButtonArea_;
    bool collapseButtonHovered_ = false;
    int ratioScrollBand_ = -1;
    bool rangeScrollActive_ = false;

    std::function<void()> onLayoutChanged_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledMultibandCurveView)
};

}  // namespace magda::daw::ui
