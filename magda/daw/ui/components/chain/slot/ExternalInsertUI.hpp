#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <memory>

#include "components/mixer/RoutingSelector.hpp"
#include "core/ChainNodePath.hpp"

namespace magda::daw::ui {

/**
 * @brief Device-slot body for the External FX / External Instrument hardware insert.
 *
 * Two port pickers -- a send and an audio return -- plus a manual latency trim, read
 * from and written to the model's InsertConfig, which both engines build the insert
 * from (#2279). The send is an audio output for FX and a MIDI output for an
 * instrument; the ports are the engine's open hardware channels and the system's
 * MIDI outputs.
 *
 * A status line warns when another insert shares one of this insert's hardware
 * ports (feedback guard, #1623). Export captures the return itself.
 */
class ExternalInsertUI : public juce::Component {
  public:
    explicit ExternalInsertUI(bool isInstrument);
    ~ExternalInsertUI() override = default;

    /** Bind to the slot's device path and (re)build the pickers from the model. */
    void setDevicePath(const magda::ChainNodePath& path);

    void resized() override;

  private:
    void rebuildFromModel();
    void refreshConflictWarning();

    const bool isInstrument_;
    magda::ChainNodePath devicePath_;

    juce::Label sendLabel_;
    juce::Label returnLabel_;
    juce::Label latencyLabel_;
    std::unique_ptr<magda::RoutingSelector> sendSelector_;
    std::unique_ptr<magda::RoutingSelector> returnSelector_;
    juce::Label latencyValue_;  // editable manual-latency field (ms)

    juce::Label warningLabel_;

    // Picker option id -> port name, as the model stores it.
    std::map<int, juce::String> sendNames_;
    std::map<int, juce::String> returnNames_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExternalInsertUI)
};

}  // namespace magda::daw::ui
