// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <memory>

#include "compiled/CompiledPluginPresentation.hpp"
#include "components/mixer/RoutingSelector.hpp"
#include "core/Config.hpp"
#include "core/SelectionManager.hpp"
#include "layout/DashedAddButton.hpp"
#include "slot/ExternalInsertLayout.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/themes/ActiveTheme.hpp"

namespace magda::daw::ui {

/**
 * @brief Faceplate for the External FX / External Instrument hardware insert.
 *
 * Send and return pickers and a manual latency trim, read from and written to the model's
 * InsertConfig, which the engine builds the insert from (#2279). The send is an audio output for
 * FX and a MIDI output, with its channel, for an instrument.
 *
 * Under them, MIDI CC knobs (#3164): device parameters the param grid draws in the Device
 * controls style, each sent as one CC. An instrument's go to its MIDI send; an effect's to a port
 * of their own. Clicking a knob selects it for the footer's CC picker and for Learn.
 *
 * The footer says what is routed, and warns when another insert shares one of this insert's
 * hardware ports (feedback guard, #1623).
 */
class ExternalInsertUI final : public juce::Component,
                               public CompiledDevicePanel,
                               private magda::ConfigListener,
                               private magda::SelectionManagerListener {
  public:
    ExternalInsertUI();
    ~ExternalInsertUI() override;

    juce::Component& component() override {
        return *this;
    }
    void updateFromDevice(const magda::DeviceInfo& device) override;
    void updateFromDevice(const magda::DeviceInfo& device, const ParamLinkContext* link) override;
    void bindDevice(std::shared_ptr<magda::daw::audio::MagdaDevice>) override {}
    void setOnParameterChanged(std::function<void(int, float)>) override {}
    void setOnPageRequested(std::function<void(int)> cb) override {
        onPageRequested_ = std::move(cb);
    }
    int preferredHeight() const override {
        return 240;
    }

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    void configChanged() override;
    void selectionTypeChanged(magda::SelectionType) override {}
    void paramSelectionChanged(const magda::ParamSelection& selection) override;

    /// The pickers' ports, read once the slot's path is known.
    void buildPickerOptions();
    void syncPickers();
    void refreshKnobEditor();
    int selectedKnob() const;
    int pageCount() const;
    void showPage(int page);
    void editKnobs(const juce::String& description, std::function<void(magda::DeviceInfo&)> edit);
    void setLearning(bool learning);
    void listenForCc();

    bool isInstrument() const {
        return device_.isInstrument;
    }

    magda::DeviceInfo device_;
    magda::ChainNodePath path_;
    ParamControlStyle style_ = ParamControlStyle::Text;
    int page_ = 0;
    int selectedParam_ = -1;
    bool learning_ = false;
    std::function<void(int)> onPageRequested_;

    juce::Label sendLabel_;
    juce::Label returnLabel_;
    juce::Label latencyLabel_;
    magda::RoutingSelector sendSelector_;
    magda::RoutingSelector channelSelector_;
    magda::RoutingSelector returnSelector_;
    juce::Label latencyValue_;  // editable manual-latency field (ms)

    juce::Label ccLabel_;
    juce::Label ccTarget_;  // where an instrument's knobs go
    magda::RoutingSelector ccPortSelector_;
    magda::RoutingSelector ccChannelSelector_;
    std::unique_ptr<magda::SvgButton> learnButton_;
    // The slot sidebar's page arrows, in its page label's dim colour.
    juce::ArrowButton prevPage_{"Previous page", 0.5f,
                                ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2)};
    juce::ArrowButton nextPage_{"Next page", 0.0f,
                                ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2)};
    juce::Label pageLabel_;
    DashedAddButton addKnob_;

    magda::RoutingSelector knobCcSelector_;
    juce::TextButton removeKnob_;

    // Picker option id -> port name, as the model stores it.
    std::map<int, juce::String> sendNames_;
    std::map<int, juce::String> returnNames_;
    std::map<int, juce::String> ccPortNames_;

    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExternalInsertUI)
};

}  // namespace magda::daw::ui
