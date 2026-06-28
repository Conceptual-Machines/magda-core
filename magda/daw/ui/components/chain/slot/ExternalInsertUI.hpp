#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <memory>

#include "components/mixer/RoutingSelector.hpp"
#include "core/ChainNodePath.hpp"

namespace magda::daw::ui {

/**
 * @brief Device-slot body for the External FX / External Instrument hardware
 *        insert (te::InsertPlugin).
 *
 * Two device pickers — a send target and an audio return source — plus a manual
 * latency trim. Options come straight from te::InsertPlugin::getPossibleDeviceNames
 * so the chosen strings round-trip into the plugin's inputDevice/outputDevice
 * CachedValues. The send is an audio output for FX, a MIDI output for an
 * instrument; the return is always an audio input. Selections are written
 * directly to the live plugin (then updateDeviceTypes()), so they persist through
 * the normal internal-plugin state round-trip.
 *
 * The live plugin may not exist when the slot first builds this UI, so the
 * pickers are (re)populated in setDevicePath(), called once the slot's device
 * path is bound (mirrors the analyzer/Faust UIs).
 */
class ExternalInsertUI : public juce::Component {
  public:
    explicit ExternalInsertUI(bool isInstrument);
    ~ExternalInsertUI() override = default;

    /** Bind to the slot's device path and (re)build the pickers from the live
     *  plugin's possible-device lists + current selection. */
    void setDevicePath(const magda::ChainNodePath& path);

    void resized() override;

  private:
    void rebuildFromPlugin();

    const bool isInstrument_;
    magda::ChainNodePath devicePath_;

    juce::Label sendLabel_;
    juce::Label returnLabel_;
    juce::Label latencyLabel_;
    std::unique_ptr<magda::RoutingSelector> sendSelector_;
    std::unique_ptr<magda::RoutingSelector> returnSelector_;
    juce::Label latencyValue_;  // editable manual-latency field (ms)

    // Picker option id -> TE device name, for mapping a selection back to the
    // string the InsertPlugin expects.
    std::map<int, juce::String> sendNames_;
    std::map<int, juce::String> returnNames_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExternalInsertUI)
};

}  // namespace magda::daw::ui
