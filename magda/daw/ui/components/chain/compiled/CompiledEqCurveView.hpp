#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "audio/plugins/compiled/MagdaEqCompiledPlugin.hpp"
#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::ui {

/**
 * @brief Magnitude-response visualisation for the 8-band compiled EQ.
 *
 * Polls the live plugin (via te::Plugin) for each band's {Enabled, Type,
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

    int getPreferredHeight() const {
        return 90;
    }

    void setCompiledPlugin(magda::daw::audio::compiled::MagdaEqCompiledPlugin* plugin);
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
    void rebuildSpectrumFft();
    void updateSpectrumOverlay();
    void drawSpectrumOverlay(juce::Graphics& g, juce::Rectangle<float> area);

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

    // Small toggle in the curve's top-right corner that flips the slot
    // between "curve fills body" (collapsed) and "curve + param grid"
    // (expanded). The actual state lives on the plugin's ValueTree via
    // `MagdaEqCompiledPlugin::curveCollapsed_` so it survives project
    // reload; this rect is recomputed in paint() and consulted by mouseDown.
    juce::Rectangle<float> collapseButtonArea_;
    bool collapseButtonHovered_ = false;

    std::function<void()> onLayoutChanged_;

    magda::daw::audio::compiled::MagdaEqCompiledPlugin* compiledPlugin_ = nullptr;
    magda::DeviceInfo deviceSnapshot_;

    juce::Rectangle<float> plotArea_;

    static constexpr int kSpectrumFftOrder = 11;
    static constexpr int kSpectrumFftSize = 1 << kSpectrumFftOrder;
    static constexpr int kSpectrumNumBins = kSpectrumFftSize / 2;
    static constexpr float kSpectrumMinDb = -90.0f;
    static constexpr float kSpectrumMaxDb = 0.0f;

    std::unique_ptr<juce::dsp::FFT> spectrumFft_;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> spectrumWindow_;
    std::vector<float> spectrumReadBuf_;
    std::vector<float> spectrumFftData_;
    std::vector<float> preSpectrumDb_;
    std::vector<float> postSpectrumDb_;
    size_t lastPreSpectrumWritePosition_ = 0;
    size_t lastPostSpectrumWritePosition_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledEqCurveView)
};

}  // namespace magda::daw::ui
