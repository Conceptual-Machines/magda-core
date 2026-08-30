#include "ExternalPluginLookup.hpp"

#include <array>
#include <functional>

namespace magda {

namespace {

/// JUCE's own name for a format, which is what a PluginDescription carries and
/// what the scan wrote. Empty for an internal device, which has no format and
/// never reaches this file.
juce::String formatNameOf(PluginFormat format) {
    switch (format) {
        case PluginFormat::VST3:
            return "VST3";
        case PluginFormat::AU:
            return "AudioUnit";
        case PluginFormat::LV2:
            return "LV2";
        case PluginFormat::Internal:
            break;
    }

    return {};
}

}  // namespace

juce::PluginDescription describeSavedPlugin(const DeviceInfo& device) {
    juce::PluginDescription description;
    description.name = device.name;
    description.manufacturerName = device.manufacturer;
    description.fileOrIdentifier = device.fileOrIdentifier;
    description.isInstrument = device.isInstrument;
    description.pluginFormatName = formatNameOf(device.format);
    return description;
}

ExternalPluginMatch matchInstalledPlugin(const DeviceInfo& device,
                                         const juce::KnownPluginList& knownPlugins) {
    const auto saved = describeSavedPlugin(device);

    // The four passes, as predicates over one list. Written this way rather
    // than as four loops because what matters about them is the order, and the
    // order is invisible when it is four hundred lines of the same loop.
    //
    // The two file passes ask nothing when the device saved no file, because an
    // empty path matches every description that also has none and that is a
    // match on the absence of evidence. The app guarded this at its call sites
    // by declining to search at all; the guard belongs to the pass that needs
    // it, so the other two still answer.
    const auto hasFile = device.fileOrIdentifier.isNotEmpty();

    const std::array<std::function<bool(const juce::PluginDescription&)>, 4> passes{
        [&](const juce::PluginDescription& known) {
            return hasFile && known.fileOrIdentifier == device.fileOrIdentifier &&
                   known.isInstrument == device.isInstrument;
        },
        [&](const juce::PluginDescription& known) {
            return known.name == device.name && known.manufacturerName == device.manufacturer &&
                   known.isInstrument == device.isInstrument;
        },
        [&](const juce::PluginDescription& known) {
            return hasFile && known.fileOrIdentifier == device.fileOrIdentifier;
        },
        [&](const juce::PluginDescription& known) {
            return known.name == device.name && known.pluginFormatName == saved.pluginFormatName;
        },
    };

    for (const auto& matches : passes)
        for (const auto& known : knownPlugins.getTypes())
            if (matches(known))
                return {.description = known, .found = true};

    return {.description = saved, .found = false};
}

}  // namespace magda
