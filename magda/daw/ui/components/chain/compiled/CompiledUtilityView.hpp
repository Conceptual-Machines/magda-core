#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceInfo.hpp"
#include "params/ParamLinkResolver.hpp"
#include "params/ParamSlotComponent.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"

namespace magda::daw::audio::compiled {
class MagdaUtilityCompiledPlugin;
}

namespace magda::daw::ui {

class CompiledUtilityView final : public juce::Component, public CompiledDevicePanel {
  public:
    explicit CompiledUtilityView(juce::String pluginId);
    ~CompiledUtilityView() override;

    void updateFromDevice(const magda::DeviceInfo& device) override;
    void updateFromDevice(const magda::DeviceInfo& device,
                          const ParamLinkContext* linkContext) override;

    juce::Component& component() override {
        return *this;
    }
    void bindPlugin(te::Plugin* plugin) override;
    void setOnParameterChanged(std::function<void(int, float)> cb) override {
        onParameterChanged = std::move(cb);
    }
    void setOnLinkRequested(std::function<void(int, float)> cb) override {
        onLinkRequested = std::move(cb);
    }
    void setOnLinkAmountChanged(std::function<void(int, float)> cb) override {
        onLinkAmountChanged = std::move(cb);
    }
    int preferredHeight() const override {
        return 320;
    }
    bool wantsFullBody() const override {
        return true;
    }

    std::function<void(int slotIndex, float displayValue)> onParameterChanged;

    void resized() override;

  private:
    void syncFromDevice();
    void writeParameter(int slotIndex, float displayValue);
    void configureLinkSlots();
    void refreshLinkSlotContext();
    void updateLinkSlotValues();

    magda::DeviceInfo deviceSnapshot_;
    magda::daw::audio::compiled::MagdaUtilityCompiledPlugin* compiledPlugin_ = nullptr;
    ParamLinkContext linkContext_;
    bool hasLinkContext_ = false;

    magda::DraggableValueLabel gainFader_{magda::DraggableValueLabel::Format::Decibels};
    juce::Label gainValue_;
    magda::DraggableValueLabel panLabel_{magda::DraggableValueLabel::Format::Pan};
    magda::DraggableValueLabel widthLabel_{magda::DraggableValueLabel::Format::Raw};
    magda::DraggableValueLabel xoverLabel_{magda::DraggableValueLabel::Format::Integer};
    ParamSlotComponent gainLinkSlot_{0};
    ParamSlotComponent panLinkSlot_{1};
    bool gainLinkInfoSet_ = false;
    bool panLinkInfoSet_ = false;

    juce::Label gainName_;
    juce::Label panName_;
    juce::Label widthName_;
    juce::Label xoverName_;

    static constexpr std::array<const char*, 4> kBtnLabels{"MONO", "LOW MONO", "FLIP L", "FLIP R"};
    std::array<juce::TextButton, 4> btns_;
    std::function<void(int slotIndex, float amount)> onLinkRequested;
    std::function<void(int slotIndex, float amount)> onLinkAmountChanged;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompiledUtilityView)
};

}  // namespace magda::daw::ui
