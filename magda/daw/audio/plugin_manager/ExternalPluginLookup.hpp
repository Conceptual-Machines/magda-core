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
 * @brief The installed plugin @p device names, searched for in four passes.
 *
 * In order, each stricter than the one after it:
 *
 * 1. The file it was loaded from, and the instrument flag agreeing. The flag is
 *    checked here because a plugin often ships an effect and an instrument out
 *    of one file, and loading the wrong one is silent.
 * 2. Name, vendor and the instrument flag. What answers when the plugin moved.
 * 3. The file alone. What answers when a plugin was renamed in place.
 * 4. Name and format alone. What answers for a project that came from another
 *    host: a DAWproject carries the plugin's name and its class id but neither
 *    our file path nor, reliably, its role -- Bitwig exports Serum, an
 *    instrument, as an audio effect -- so neither the vendor nor the flag can
 *    be required by the pass that has to resolve it.
 */
ExternalPluginMatch matchInstalledPlugin(const DeviceInfo& device,
                                         const juce::KnownPluginList& knownPlugins);

}  // namespace magda
