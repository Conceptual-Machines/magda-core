#include "AudioDeviceOpenMark.hpp"

namespace magda {

namespace {

std::optional<juce::String>& crashedThisLaunch() {
    static std::optional<juce::String> crashed;
    return crashed;
}

}  // namespace

AudioDeviceOpenMark::AudioDeviceOpenMark(juce::File file, const juce::String& interfaceName)
    : file_(std::move(file)) {
    if (file_ != juce::File())
        file_.replaceWithText(interfaceName);
}

AudioDeviceOpenMark::~AudioDeviceOpenMark() {
    if (file_ != juce::File())
        file_.deleteFile();
}

std::optional<juce::String> takeCrashedAudioDevice(const juce::File& file) {
    if (file == juce::File() || !file.existsAsFile())
        return std::nullopt;

    auto interfaceName = file.loadFileAsString().trim();
    file.deleteFile();
    juce::Logger::writeToLog("[audio-io] the last launch died opening \"" + interfaceName +
                             "\"; opening the default interface instead (#3090)");
    crashedThisLaunch() = interfaceName;
    return interfaceName;
}

std::optional<juce::String> crashedAudioDeviceThisLaunch() {
    return crashedThisLaunch();
}

}  // namespace magda
