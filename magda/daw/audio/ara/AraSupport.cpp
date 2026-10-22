#include "AraSupport.hpp"

#include "plugin_manager/ExternalPluginLookup.hpp"

namespace magda::daw::audio::ara {

bool supportsAra(const DeviceInfo& device, const juce::KnownPluginList& known) {
    const auto match = matchInstalledPlugin(device, known);
    return match.found && match.description.hasARAExtension;
}

}  // namespace magda::daw::audio::ara
