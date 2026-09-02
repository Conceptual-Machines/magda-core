#pragma once

#include <juce_core/juce_core.h>

#include "ChainNodePath.hpp"
#include "CommandPattern.hpp"

namespace magda {

/**
 * @brief Undoable authored-state edits on an internal device's state document.
 *
 * The model owns a device's authored state (`DeviceInfo::pluginState`, the v2
 * document) and the engines are projections of it (#2317). An edit is a patch
 * applied to the document through TrackManager::updateDeviceAuthoredState;
 * undo puts the whole previous document back. The engine is never asked what
 * it currently holds - the snapshot IS the model's document, so undo works
 * whether or not a live plugin exists.
 */

/**
 * @brief Load an impulse response into a convolution device.
 *
 * The file's bytes and display name are written into the device's state
 * document, and the projection reloads the live convolution from them - the
 * same route a project load takes. Undo restores the previous document (and
 * with it the previous IR, when there was one).
 */
class LoadImpulseResponseCommand : public SnapshotCommand<juce::String> {
  public:
    LoadImpulseResponseCommand(const ChainNodePath& devicePath, const juce::String& irName,
                               juce::MemoryBlock irData);

    juce::String getDescription() const override;
    bool canExecute() const override;

  protected:
    juce::String captureState() override;
    void restoreState(const juce::String& state) override;
    void performAction() override;

  private:
    ChainNodePath devicePath_;
    juce::String irName_;
    juce::MemoryBlock irData_;
};

}  // namespace magda
