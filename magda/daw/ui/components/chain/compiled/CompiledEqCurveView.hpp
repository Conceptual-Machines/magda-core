#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "audio/plugins/compiled/MagdaEqCompiledPlugin.hpp"
#include "compiled/CompiledPluginPresentation.hpp"
#include "compiled/SpectrumOverlay.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::ui {

/**
 * @brief Magnitude-response visualisation for the 8-band compiled EQ.
 *
 * Polls the live plugin for each band's {Enabled, Type,
 * Freq, Gain, Q}, sums enabled biquad magnitude responses across log-spaced
 * frequency bins, and renders the resulting curve plus per-band dots. Bands
 * set to HP / LP / Notch ignore Gain and draw a dot anchored to 0 dB;
 * LowShelf / HighShelf ignore Q.
 */
class CompiledEqCurveView final : public juce::Component,
                                  public CompiledDevicePanel,
                                  private juce::Timer {
  public:
    explicit CompiledEqCurveView(juce::String pluginId);

    static int getPreferredHeight() {
        return 90;
    }

    void setCompiledPlugin(
        std::shared_ptr<magda::daw::audio::compiled::MagdaEqCompiledPlugin> plugin);
    void updateFromDevice(const magda::DeviceInfo& device) override;

    juce::Component& component() override {
        return *this;
    }
    void bindDevice(std::shared_ptr<magda::daw::audio::MagdaDevice> device) override;
    void setOnParameterChanged(std::function<void(int, float)> cb) override {
        onParameterChanged = std::move(cb);
    }
    void setOnLayoutChanged(std::function<void()> cb) override {
        onLayoutChanged_ = std::move(cb);
    }
    int preferredHeight() const override {
        return getPreferredHeight();
    }

    /// Set by the host slot at construction. Called with the band's host
    /// slot index and the real-world (display) value when the user drags /
    /// scrolls / picks a type on the curve.
    std::function<void(int slotIndex, float displayValue)> onParameterChanged;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

  private:
    using Plugin = magda::daw::audio::compiled::MagdaEqCompiledPlugin;
    using BandType = Plugin::BandType;
    using BandSnapshot = Plugin::BandSnapshot;

    void timerCallback() override;
    void resampleFromDevice();

    // -1 if the cursor isn't near any band. Used by hit-testing and the
    // hover highlight in paint().
    int findBandAt(juce::Point<float> p) const;
    float xToFreq(float x) const;
    float yToDb(float y) const;
    void writeBandParam(int band, int slotOffset, float displayValue);
    void setBandType(int band, BandType type);
    void setBandEnabled(int band, bool enabled);
    void showBandTypeMenu(int band);

    // Cached per-band state used by paint(). Updated on the message thread
    // by the poll timer / device-snapshot path.
    std::array<BandSnapshot, Plugin::kBandCount> bands_{};
    float outputDb_ = 0.0f;

    int hoveredBand_ = -1;
    int draggedBand_ = -1;

    std::function<void()> onLayoutChanged_;

    std::shared_ptr<magda::daw::audio::compiled::MagdaEqCompiledPlugin> compiledPlugin_;
    magda::DeviceInfo deviceSnapshot_;

    juce::Rectangle<float> plotArea_;

    SpectrumOverlay spectrum_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledEqCurveView)
};

}  // namespace magda::daw::ui
