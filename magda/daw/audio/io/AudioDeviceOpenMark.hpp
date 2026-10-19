#pragma once

/**
 * @file AudioDeviceOpenMark.hpp
 * @brief Notices that the last launch died opening its audio interface, so this one skips it
 *        (#3090).
 */

#include <juce_core/juce_core.h>

#include <optional>

namespace magda {

/**
 * @brief A file naming the interface being opened, there only while the open runs.
 *
 * A driver that crashes the process leaves it behind for the next launch to find. An empty
 * file marks nothing, which is what tests pass.
 */
class AudioDeviceOpenMark {
  public:
    AudioDeviceOpenMark(juce::File file, const juce::String& interfaceName);
    ~AudioDeviceOpenMark();

    AudioDeviceOpenMark(const AudioDeviceOpenMark&) = delete;
    AudioDeviceOpenMark& operator=(const AudioDeviceOpenMark&) = delete;

  private:
    juce::File file_;
};

/**
 * @brief The interface a mark left in @p file names, which it then clears; nothing if the last
 *        open finished.
 *
 * What is found is also kept for crashedAudioDeviceThisLaunch().
 */
std::optional<juce::String> takeCrashedAudioDevice(const juce::File& file);

/** @brief What takeCrashedAudioDevice() found this launch, for the UI to explain. */
std::optional<juce::String> crashedAudioDeviceThisLaunch();

}  // namespace magda
