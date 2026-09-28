#include "ActiveMidiInputs.hpp"

#include <algorithm>

#include "../../core/Config.hpp"

namespace magda {

ActiveMidiInputs::ActiveMidiInputs(juce::AudioDeviceManager& devices,
                                   juce::Array<juce::MidiDeviceInfo> available)
    : devices_(devices), available_(std::move(available)) {
    const auto& config = Config::getInstance();
    for (const auto& input : available_)
        devices_.setMidiInputDeviceEnabled(input.identifier, config.isMidiInputActive(input.name));
    shown_ = ticked();
}

bool ActiveMidiInputs::saveChanges() {
    auto& config = Config::getInstance();
    auto inactive = config.getInactiveMidiInputs();
    const auto now = ticked();

    auto changed = false;
    for (const auto& input : available_) {
        const auto wasTicked = shown_.contains(input.identifier);
        const auto isTicked = now.contains(input.identifier);
        if (wasTicked == isTicked)
            continue;

        const auto sameName = [&input](const std::string& name) {
            return input.name.equalsIgnoreCase(juce::String(name));
        };
        std::erase_if(inactive, sameName);
        if (!isTicked)
            inactive.push_back(input.name.toStdString());
        changed = true;
    }

    shown_ = now;
    if (!changed)
        return false;

    config.setInactiveMidiInputs(std::move(inactive));
    config.save();
    return true;
}

std::set<juce::String> ActiveMidiInputs::ticked() const {
    std::set<juce::String> identifiers;
    for (const auto& input : available_)
        if (devices_.isMidiInputDeviceEnabled(input.identifier))
            identifiers.insert(input.identifier);
    return identifiers;
}

}  // namespace magda
