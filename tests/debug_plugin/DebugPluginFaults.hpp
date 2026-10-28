// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

#include <atomic>

namespace magda::debug_plugin {

/// The environment variable the faults are read from, e.g. "max-instances=1,no-editor".
inline constexpr const char* kFaultsVariable = "MAGDA_DEBUG_PLUGIN_FAULTS";

/**
 * @brief What this instance has been asked to get wrong, read when it is created.
 *
 * Read through juce::SystemStats so a host that set the variable reaches a plugin with
 * its own C runtime on Windows.
 */
struct Faults {
    int maxInstances = 0;  ///< Live instances past which the factory refuses; 0 is no limit.
    int slowLoadMs = 0;
    int latencyChange = 0;  ///< Latency reported from the first block on; 0 leaves it at 0.
    bool noEditor = false;
    bool silent = false;
    bool throwOnState = false;
    bool crashOnEditor = false;

    static Faults parse(const juce::String& spec);
    static Faults fromEnvironment();

    juce::String describe() const;
};

/// Processors alive in this process, which the factory's refusal counts.
std::atomic<int>& liveInstances();

}  // namespace magda::debug_plugin
