#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <optional>
#include <vector>

namespace magda::device_state {

/**
 * MAGDA-owned state document for an internal device (schema v2).
 *
 * Before v2, `DeviceInfo::pluginState` for an internal device was the engine's
 * own plugin ValueTree serialized as XML: a `<PLUGIN type="..." id="...">` tree
 * carrying engine object ids and engine-side child trees. That made project
 * files and device presets the one place where a Tracktion-shaped blob leaked
 * into MAGDA's own format, so the engine could not be replaced without
 * rewriting every user file.
 *
 * v2 is a MAGDA-defined document with three parts:
 *
 *  - `deviceType` — MAGDA's canonical device id (`DeviceInfo::pluginId`), not an
 *    engine plugin type name.
 *  - `params` — the device's automatable parameters, keyed by the FROZEN
 *    `paramIndex` for that device type. This is the same index the persisted
 *    automation / macro / mod graph is stored against (`ControlTarget::PluginParam`),
 *    so it is a compatibility surface: see `DeviceParamSchema.hpp`. Each entry also
 *    carries the parameter's stable string id so a value can be re-seated if a
 *    device ever has to renumber.
 *  - `root` — everything that is not a parameter (sample paths, DSP source,
 *    sequencer steps, drum-pad chains, ...) as a plain name/value tree under the
 *    device's own property names. No engine ids, no engine container tags.
 *
 * The document is engine-neutral by construction: nothing here knows what a
 * `te::Plugin` is. The engine adapter converts between a live device and this
 * document (`TracktionDeviceStateBridge.hpp` today, the native device state
 * contract later).
 *
 * v1 (engine XML) is still read: `looksLikeLegacyEngineState()` routes an old
 * string down the legacy path so existing projects and presets load unchanged.
 * v1 is never written again.
 */

/// Current schema version written by this build.
inline constexpr int kSchemaVersion = 2;

/**
 * One automatable parameter value, keyed by its frozen index.
 *
 * `index` is authoritative for restore; `id` is a self-describing fallback and
 * the value the frozen-schema check compares against.
 */
struct ParamValue {
    int index = -1;
    juce::String id;
    float value = 0.0f;
};

/// Non-parameter device state: a property bag with optional ordered children.
struct Node {
    juce::String type;  // element name; empty for the document root
    juce::NamedValueSet props;
    std::vector<Node> children;

    bool isEmpty() const {
        return props.size() == 0 && children.empty();
    }
};

/// A complete internal-device state document.
struct Doc {
    int version = kSchemaVersion;
    juce::String deviceType;
    std::vector<ParamValue> params;
    Node root;
};

/// Serialize to the JSON text stored in `DeviceInfo::pluginState`.
juce::String encode(const Doc& doc);

/// Parse a device state document. Returns nullopt for empty input, legacy engine
/// XML, anything malformed, and any schema version this build does not
/// understand - including a NEWER one, which must not be read as if it were v2.
std::optional<Doc> decode(const juce::String& text);

/**
 * True when `text` is a pre-v2 device state (the engine's plugin ValueTree as
 * XML). Callers use this to pick the legacy restore path; it is deliberately
 * cheap and shape-based rather than a full parse.
 */
bool looksLikeLegacyEngineState(const juce::String& text);

/// True when `text` parses as a v2 document.
bool isDeviceStateV2(const juce::String& text);

/// The declared schema version of a MAGDA device state document, whatever this
/// build's support for it. Nullopt for empty input, legacy engine XML, and
/// anything that is not a MAGDA document.
std::optional<int> schemaVersionOf(const juce::String& text);

/**
 * True when `text` is a MAGDA device state document from a schema NEWER than
 * this build understands.
 *
 * Such a document is refused by `decode`, so the device loads its defaults.
 * Capture must then leave the saved state alone: replacing it with a freshly
 * captured v2 document would permanently downgrade a project written by a newer
 * build, merely because it was opened and saved here.
 */
bool isFutureDeviceState(const juce::String& text);

/// Depth-first walk over `root` and all descendants, root first.
void forEachNode(const Node& root, const std::function<void(const Node&)>& visit);

}  // namespace magda::device_state
