#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/DeviceInfo.hpp"

/**
 * @file ExternalPluginLookup.hpp
 * @brief Which installed plugin a saved device meant (#2243).
 *
 * A project stores what it can about an external plugin: the name it had, its
 * vendor, the file it was loaded from, whether it was an instrument. What it
 * cannot store is the plugin, so opening the project means finding that plugin
 * again among whatever this machine has installed, which is not the machine
 * that saved it.
 *
 * The passes below are that search, and none of them is incidental. Each was
 * added for a project that would otherwise have loaded nothing or, worse,
 * loaded the wrong plugin: an effect where an instrument was saved, a plugin of
 * the same name from another vendor, a plugin whose installer moved it between
 * versions.
 *
 * It lives here, in one function, because there is one answer to it. The two
 * engines cannot each have an opinion about which plugin a project meant, and
 * before this the app itself had two: the track path tried the file path on its
 * own after the vendor pass and the rack path did not, so a plugin that had
 * both moved and been renamed resolved on a track and not inside a rack.
 */

namespace magda {

/**
 * @brief What the search found, and whether it found anything.
 *
 * @ref description is the installed plugin when @ref found is true, and the
 * saved fields as a description when it is false. The second is not a
 * placeholder: a host is free to try to load it anyway, which is what lets a
 * plugin resolve through a format's own lookup when the scan is out of date,
 * and it is what the app has always done.
 */
struct ExternalPluginMatch {
    juce::PluginDescription description;
    bool found = false;
};

/**
 * @brief The description @p device carries, before anything installed is asked.
 *
 * Name, vendor, file and instrument flag as saved, with the format named the
 * way JUCE names it. This is the query; matchInstalledPlugin() is the search.
 */
juce::PluginDescription describeSavedPlugin(const DeviceInfo& device);

/**
 * @brief The installed plugin @p device names, searched for in five passes.
 *
 * In order, each stricter than the one after it:
 *
 * 1. JUCE's own identifier, which is what DeviceInfo::uniqueId holds
 *    (PluginDescription::createIdentifierString). Exact identity, and it is
 *    first because nothing below it can tell two plugins apart that a bundle
 *    ships out of one file with one role: a shell format's dozen effects share
 *    a path, and the pass below would return whichever of them the scan listed
 *    first.
 * 2. The file it was loaded from, and the instrument flag agreeing. The flag is
 *    checked here because a plugin often ships an effect and an instrument out
 *    of one file, and loading the wrong one is silent.
 * 3. Name, vendor, format and the instrument flag. What answers when the plugin
 *    moved. The format is required because a machine commonly has the same
 *    plugin twice, as an AU and as a VST3, with the same name, vendor and role:
 *    the two have different parameter layouts and different state, so
 *    substituting one for the other loads a plugin and renders a different
 *    project.
 * 4. The file alone. What answers when a plugin was renamed in place.
 * 5. Name and format alone. What answers for a project that came from another
 *    host: a DAWproject carries the plugin's name and its class id but neither
 *    our file path nor, reliably, its role -- Bitwig exports Serum, an
 *    instrument, as an audio effect -- so neither the vendor nor the flag can
 *    be required by the pass that has to resolve it.
 *
 * A pass that has nothing to ask with asks nothing: a device that saved no file
 * skips the file passes, one with no name skips the name passes, one with no
 * identifier skips the first. An empty field matching an empty field is a match
 * on the absence of evidence.
 */
ExternalPluginMatch matchInstalledPlugin(const DeviceInfo& device,
                                         const juce::KnownPluginList& knownPlugins);

/**
 * @brief Enrich @p device with facts from the exact installed plugin selected.
 *
 * Resolution is also metadata discovery. An imported DAWproject may have no
 * vendor or path and may call an instrument an effect; a plugin may have moved
 * since the project was saved. The native plan must see the installed role and
 * channel topology before it compiles, otherwise the successfully resolved
 * instrument receives no MIDI or a device is wired at the wrong width.
 *
 * This does not author a new plugin assignment and therefore deliberately does
 * not change DeviceInfo::pluginAssignmentGeneration. It only replaces mutable
 * saved/scan facts for the assignment already in the slot.
 */
void applyResolvedPluginDescription(DeviceInfo& device, const juce::PluginDescription& description);

}  // namespace magda
