#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"
#include "layout/UtilityGeometry.hpp"
#include "plugins/compiled/MagdaUtilityCompiledPlugin.hpp"
#include "ui/components/common/SegmentedChoice.hpp"

namespace magda::daw::ui {

/// The Utility's faceplate under its knobs: an L-R picture of the image, where the band's
/// position is pan and its size is width, and the Mono / Low mono / Swap / polarity toggles.
class CompiledUtilityView final : public juce::Component, public CompiledDevicePanel {
  public:
    explicit CompiledUtilityView(juce::String pluginId);

    void updateFromDevice(const magda::DeviceInfo& device) override;

    juce::Component& component() override {
        return *this;
    }
    void bindDevice(std::shared_ptr<magda::daw::audio::MagdaDevice> device) override;
    void setOnParameterChanged(std::function<void(int, float)> cb) override {
        onParameterChanged_ = std::move(cb);
    }
    int preferredHeight() const override {
        return 320;
    }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

  private:
    using Plugin = magda::daw::audio::compiled::MagdaUtilityCompiledPlugin;

    enum class Drag { None, Pan, Width };

    float value(int slot, float fallback) const;
    bool isOn(int slot) const;
    void writeParameter(int slot, float value);
    /// The band's left and right edges across the field, 0 to 1.
    juce::Range<float> bandRange() const;
    Drag dragFor(juce::Point<float> position) const;

    magda::DeviceInfo deviceSnapshot_;
    std::shared_ptr<Plugin> compiledPlugin_;
    std::function<void(int slotIndex, float displayValue)> onParameterChanged_;
    ParamControlStyle style_ = ParamControlStyle::Text;
    UtilityGeometry geometry_;

    static constexpr std::array<int, 5> kToggleSlots{Plugin::kMonoSlot, Plugin::kLowMonoSlot,
                                                     Plugin::kSwapSlot, Plugin::kFlipLSlot,
                                                     Plugin::kFlipRSlot};
    std::array<magda::SegmentedChoice, 5> toggles_;

    Drag drag_ = Drag::None;
    float dragStartPan_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledUtilityView)
};

}  // namespace magda::daw::ui
