#include "core/DeviceCatalogMetadata.hpp"

#include "audio/plugins/InternalPluginRegistry.hpp"
#include "audio/plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw {

DeviceCatalogMetadata findDeviceCatalogMetadata(const juce::String& pluginId) {
    const auto safe = [](const char* text) { return text != nullptr ? text : ""; };

    if (const auto* spec = audio::compiled::findCompiledPluginSpec(pluginId))
        return {safe(spec->displayName), safe(spec->browserCategory), safe(spec->description),
                true};

    if (const auto* spec = audio::findInternalPluginSpec(pluginId))
        return {safe(spec->displayName), safe(spec->browserCategory), safe(spec->description),
                true};

    return {};
}

}  // namespace magda::daw
