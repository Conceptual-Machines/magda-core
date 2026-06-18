#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "core/ChainNodePath.hpp"
#include "core/ParameterInfo.hpp"
#include "custom_ui/LayoutStableTabbedComponent.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"

namespace magda::daw::audio {
class IFaustEditorModel;
}

namespace magda::daw::ui {

class FaustUI;

/**
 * @brief Tabbed custom UI for the Faust INSTRUMENT device (4OSC-style, wider
 *        than effect slots).
 *
 * Top strip embeds a FaustUI (logo / DSP name / Load / Save / Edit), reusing
 * the whole .dsp load+edit flow. Below it, a tab per top-level Faust group
 * (FaustParamSlot::group) — a DSP with no groups shows a single "Params" tab.
 * Each tab lays its group's parameters out as LinkableTextSliders, so
 * mod / macro / automation / MIDI-Learn drag-linking works via the standard
 * DeviceSlotComponent::setupCustomUILinking() path (which binds each slider by
 * its setParamIndex()).
 *
 * Wired through DeviceCustomUIManager exactly like FourOscUI.
 */
class FaustInstrumentTabbedUI : public juce::Component {
  public:
    FaustInstrumentTabbedUI();
    ~FaustInstrumentTabbedUI() override;

    /// Bind the live editor model (drives the header + the pool the tabs read).
    void setPlugin(magda::daw::audio::IFaustEditorModel* model);
    void setDevicePath(const magda::ChainNodePath& path);

    /// Rebuild tabs from the pool when its layout changed, then push the
    /// current values from `params` into the sliders. Called on every device
    /// refresh by DeviceCustomUIManager.
    void updateFromParameters(const std::vector<magda::ParameterInfo>& params);

    /// Flat list of every tab's sliders (pool index carried via setParamIndex).
    /// Consumed by DeviceSlotComponent::setupCustomUILinking().
    std::vector<LinkableTextSlider*> getLinkableSliders();

    std::function<void(int paramIndex, float value)> onParameterChanged;

    int getCurrentTabIndex() const;
    void setCurrentTabIndex(int index);

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    struct Row {
        int slotIndex = -1;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<LinkableTextSlider> slider;
    };

    // One tab's page: owns its rows and lays them out in a simple grid.
    class GroupPage : public juce::Component {
      public:
        std::vector<Row> rows;
        void resized() override;
    };

    void rebuildTabs();
    juce::String poolSignature() const;

    std::unique_ptr<FaustUI> header_;
    std::unique_ptr<LayoutStableTabbedComponent> tabs_;
    std::vector<std::unique_ptr<GroupPage>> pages_;

    magda::daw::audio::IFaustEditorModel* model_ = nullptr;
    magda::ChainNodePath devicePath_;
    juce::String lastSignature_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaustInstrumentTabbedUI)
};

}  // namespace magda::daw::ui
