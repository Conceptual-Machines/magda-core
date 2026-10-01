#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <magda/sdk/state/StateCodec.hpp>
#include <optional>
#include <string>
#include <vector>

#include "core/DeviceState.hpp"

/**
 * @file DeviceStateDocument.hpp
 * @brief The one place a saved device state becomes the document a device is handed.
 *
 * The tolerant legacy reader lives here: v1 engine XML, the v2 document with its pre-#2317
 * parameter record, the engine's own props and children, and device-type load aliases. All of
 * it is normalised into the SDK's strict document, which has no parameter record and holds
 * only int64, double, bool, string and binary properties.
 */

namespace magda::daw::audio {

/// A saved state read into the SDK document, with what the SDK form cannot hold.
struct NormalisedDeviceState {
    sdk::StateDocument document;

    /// Properties left out because their value is an array, an object or a non-finite number.
    std::vector<std::string> dropped;
};

/**
 * @brief Read @p savedState in either format a project holds it in.
 *
 * Nullopt for empty text, anything unreadable, and a document from a schema newer than this
 * build reads (never read as if it were the current one). The device type is resolved through
 * the registry's load aliases.
 */
std::optional<NormalisedDeviceState> normaliseDeviceState(const juce::String& savedState);

/// A legacy-reader node as an SDK node. Nameless children are skipped, as the tree form always did.
sdk::StateNode toSdkNode(const device_state::Node& node,
                         std::vector<std::string>* dropped = nullptr);

/// An SDK value as the juce value the legacy document holds.
juce::var toJuceVar(const sdk::StateValue& value);

/**
 * @brief Apply state a device reported onto the model's document.
 *
 * The reported node is a patch (sdk::DeviceHost::stateChanged): its properties are written onto
 * the root, and its children replace the root's children of the same types.
 */
void applyReportedState(device_state::Doc& doc, const sdk::StateNode& reported);

/// The document's root as the plain tree the retired restore contract took: a "PLUGIN" root
/// carrying a `type` property that names the device.
juce::ValueTree toLegacyTree(const sdk::StateDocument& document);

}  // namespace magda::daw::audio
