#include "TracktionAudioSettings.hpp"

namespace magda {

namespace {

/// Tracktion's wave masks start all-on over this many channels, and MAGDA asked JUCE for as many.
constexpr int kTracktionChannelRange = 256;

/** @brief The mask in @p attribute, or every channel Tracktion covers when it was never saved. */
juce::BigInteger maskOrAll(const juce::XmlElement* element, const juce::String& attribute) {
    juce::BigInteger mask;
    if (element != nullptr && element->hasAttribute(attribute))
        mask.parseString(element->getStringAttribute(attribute), 2);
    else
        mask.setRange(0, kTracktionChannelRange, true);
    return mask;
}

/** @brief The element juce::PropertiesFile stored under @p name, which PropertyStorage writes
 * through. */
const juce::XmlElement* valueNamed(const juce::XmlElement& properties, const juce::String& name) {
    for (const auto* value : properties.getChildWithTagNameIterator("VALUE"))
        if (value->getStringAttribute("name") == name)
            return value->getFirstChildElement();
    return nullptr;
}

std::vector<int> channelsOf(const juce::BigInteger& mask) {
    std::vector<int> channels;
    for (auto bit = mask.findNextSetBit(0); bit >= 0; bit = mask.findNextSetBit(bit + 1))
        channels.push_back(bit);
    return channels;
}

}  // namespace

std::optional<AudioIOSettings> readTracktionAudioSettings(const juce::File& settingsFile) {
    const auto properties = juce::parseXMLIfTagMatches(settingsFile, "PROPERTIES");
    if (properties == nullptr)
        return std::nullopt;

    const auto* setupXml = valueNamed(*properties, "audio_device_setup");
    if (setupXml == nullptr || !setupXml->hasTagName("DEVICESETUP"))
        return std::nullopt;

    const auto& setup = *setupXml;
    const auto backend = setup.getStringAttribute("deviceType");
    const auto* waveMasks = valueNamed(*properties, "audiosettings_" + backend);

    // JUCE writes one name for a backend without separate inputs and outputs.
    const auto sharedName = setup.getStringAttribute("audioDeviceName");
    const auto inputName =
        sharedName.isNotEmpty() ? sharedName : setup.getStringAttribute("audioInputDeviceName");
    const auto outputName =
        sharedName.isNotEmpty() ? sharedName : setup.getStringAttribute("audioOutputDeviceName");

    auto inputs = maskOrAll(&setup, "audioDeviceInChans") & maskOrAll(waveMasks, "inEnabled");
    auto outputs = maskOrAll(&setup, "audioDeviceOutChans") & maskOrAll(waveMasks, "outEnabled");
    if (inputName.isEmpty())
        inputs.clear();
    if (outputName.isEmpty())
        outputs.clear();

    return AudioIOSettings{.backend = backend.toStdString(),
                           .inputInterface = inputName.toStdString(),
                           .outputInterface = outputName.toStdString(),
                           .sampleRate = setup.getDoubleAttribute("audioDeviceRate"),
                           .bufferSize = setup.getIntAttribute("audioDeviceBufferSize"),
                           .inputChannels = channelsOf(inputs),
                           .outputChannels = channelsOf(outputs)};
}

juce::File tracktionSettingsFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("MAGDA")
        .getChildFile("Settings.xml");
}

}  // namespace magda
