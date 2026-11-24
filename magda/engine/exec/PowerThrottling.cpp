// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "exec/PowerThrottling.hpp"

#include <juce_core/juce_core.h>

#if JUCE_WINDOWS
    #include <windows.h>

    #include <cstdlib>
    #include <cstring>
#endif

namespace magda::engine::power_throttling {

#if JUCE_WINDOWS
namespace {

bool allowed() {
    static const bool allow = [] {
        const auto* value = std::getenv("MAGDA_POWER_THROTTLING");
        return value != nullptr && std::strcmp(value, "allow") == 0;
    }();
    return allow;
}

}  // namespace

void exemptCurrentThread() {
    if (allowed())
        return;
    // A cleared state bit under a set control bit means "never throttle" for that bit.
    THREAD_POWER_THROTTLING_STATE state{};
    state.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &state, sizeof(state));
}

void exemptProcess(bool exempt) {
    if (allowed())
        return;
    // A zero control mask returns the decision to Windows rather than forcing throttling on.
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = exempt ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;
    if (!SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state)))
        juce::Logger::writeToLog("[engine] power throttling opt-out refused, error " +
                                 juce::String(static_cast<int>(GetLastError())));
}
#else
void exemptCurrentThread() {}
void exemptProcess(bool) {}
#endif

}  // namespace magda::engine::power_throttling
