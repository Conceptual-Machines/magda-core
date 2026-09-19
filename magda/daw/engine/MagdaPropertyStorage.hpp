#pragma once

/**
 * @file MagdaPropertyStorage.hpp
 * @brief Tracktion's Settings.xml, kept from overwriting its interface while it has none (#2747).
 */

#include <tracktion_engine/tracktion_engine.h>

namespace magda {

/**
 * @brief Tracktion's settings, less the audio interface when Tracktion does not open one.
 *
 * With no backends, Tracktion's DeviceManager saves an interface with no names over the one it
 * opens next time, so a switch back to the Tracktion engine would come up silent.
 */
class MagdaPropertyStorage final : public tracktion::PropertyStorage {
  public:
    MagdaPropertyStorage(juce::String appName, bool savesAudioInterface)
        : tracktion::PropertyStorage(std::move(appName)),
          savesAudioInterface_(savesAudioInterface) {}

    void setXmlProperty(tracktion::SettingID setting, const juce::XmlElement& xml) override {
        if (!savesAudioInterface_ && setting == tracktion::SettingID::audio_device_setup)
            return;
        tracktion::PropertyStorage::setXmlProperty(setting, xml);
    }

  private:
    bool savesAudioInterface_;
};

}  // namespace magda
