#pragma once

#include <juce_core/juce_core.h>

#include "ChainNodePath.hpp"
#include "CommandPattern.hpp"

namespace magda::daw::audio {
class MagdaDevice;
}

namespace magda {

class AudioEngine;

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
    LoadImpulseResponseCommand(ChainNodePath devicePath, juce::String irName,
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

/**
 * @brief Patch a device's non-slot settings into its state document.
 *
 * What every faceplate whose settings are authored state writes through: the
 * model holds them, the projection pushes them to whatever is rendering, and a
 * save carries them. Not undoable -- a control's own position is the state, and
 * nobody reaches for undo after moving one.
 *
 * @return whether the document changed, which is false for a path the model
 *         has no internal device at.
 */
bool writeDeviceSettings(const ChainNodePath& devicePath, const juce::NamedValueSet& settings);

/**
 * @brief Push a state document onto a device that is already running.
 *
 * The projection's other half: the fork hands its plugin the tree and the
 * plugin hands it to the device, so a device the native engine holds is given
 * the same tree directly (#2663).
 *
 * @p deviceType names the device for an empty @p docText, which is still a
 * state -- "nothing authored" -- and has to reach a device whose contract reads
 * absence as none.
 */
void projectAuthoredStateToDevice(daw::audio::MagdaDevice& device, const juce::String& docText,
                                  const juce::String& deviceType);

/// The model's authored state at @p devicePath onto the device @p engine renders there.
void projectAuthoredStateToRenderedDevice(const AudioEngine& engine,
                                          const ChainNodePath& devicePath);

}  // namespace magda
