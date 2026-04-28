#pragma once

#include <juce_core/juce_core.h>

#include <map>

#include "AliasRegistry.hpp"

namespace magda {

/**
 * @brief Code-driven curated aliases for MAGDA's built-in plugins.
 *
 * Internal plugins (Equaliser, Compressor, Reverb, ...) are owned by MAGDA,
 * so their canonical alias mapping ships in code rather than as JSON. The
 * function below returns the full per-plugin curated alias set for every
 * built-in plugin we want to expose under a short pluginKey ("@eq", "@reverb"
 * instead of the longer AutoGen "@equaliser.*").
 *
 * The returned map is keyed by canonical alias name ("eq.low_shelf_freq")
 * with values that are otherwise identical to entries the JSON-driven
 * `CuratedAliasLoader` would produce — so both paths feed the same Curated
 * layer.
 *
 * Third-party plugins (Pro-Q 3, Diva, Serum 2, ...) keep using the JSON
 * loader path because we don't own their parameter lists.
 */
std::map<juce::String, StoredAlias> collectInternalPluginCuratedAliases();

}  // namespace magda
