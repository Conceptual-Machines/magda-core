// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DebugPluginFaults.hpp"

namespace magda::debug_plugin {

Faults Faults::parse(const juce::String& spec) {
    Faults faults;

    for (const auto& token : juce::StringArray::fromTokens(spec, ",", {})) {
        const auto key = token.upToFirstOccurrenceOf("=", false, false).trim();
        const auto value = token.fromFirstOccurrenceOf("=", false, false).trim().getIntValue();

        if (key == "max-instances")
            faults.maxInstances = value;
        else if (key == "slow-load")
            faults.slowLoadMs = value;
        else if (key == "latency-change")
            faults.latencyChange = value;
        else if (key == "no-editor")
            faults.noEditor = true;
        else if (key == "silent")
            faults.silent = true;
        else if (key == "throw-on-state")
            faults.throwOnState = true;
        else if (key == "crash-on-editor")
            faults.crashOnEditor = true;
    }

    return faults;
}

Faults Faults::fromEnvironment() {
    return parse(juce::SystemStats::getEnvironmentVariable(kFaultsVariable, {}));
}

juce::String Faults::describe() const {
    juce::StringArray active;
    if (maxInstances > 0)
        active.add("max-instances=" + juce::String(maxInstances));
    if (slowLoadMs > 0)
        active.add("slow-load=" + juce::String(slowLoadMs));
    if (latencyChange > 0)
        active.add("latency-change=" + juce::String(latencyChange));
    if (noEditor)
        active.add("no-editor");
    if (silent)
        active.add("silent");
    if (throwOnState)
        active.add("throw-on-state");
    if (crashOnEditor)
        active.add("crash-on-editor");

    return active.isEmpty() ? juce::String("no faults") : active.joinIntoString(", ");
}

std::atomic<int>& liveInstances() {
    static std::atomic<int> live{0};
    return live;
}

}  // namespace magda::debug_plugin
