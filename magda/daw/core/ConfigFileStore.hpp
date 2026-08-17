#pragma once

#include <juce_core/juce_core.h>

/**
 * @brief Writing the settings file, separated from its contents.
 *
 * Config::save() used to call replaceWithText() straight on config.json, which
 * truncates the file before the new contents land. A crash, a force-quit or a
 * yanked volume in that window leaves a half-written settings file behind and
 * every preference in it -- API keys included -- is gone (issue #2104). Doing
 * the write here rather than in Config keeps it testable against a scratch file
 * instead of the developer's own.
 */
namespace magda::ConfigFileStore {

/**
 * Write the settings file atomically.
 *
 * The contents go to a temporary file in the same directory and are renamed
 * over the target, so the existing file is either untouched or wholly replaced.
 * There is no window in which it is truncated.
 *
 * @return true if the file was written
 */
bool write(const juce::File& file, const juce::String& json);

}  // namespace magda::ConfigFileStore
