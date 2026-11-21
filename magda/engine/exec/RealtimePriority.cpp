// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "exec/RealtimePriority.hpp"

namespace magda::engine::realtime {

juce::String describe(const Grant& grant) {
    if (grant.priority > 0)
        return "SCHED_RR at priority " + juce::String(grant.priority) +
               (grant.viaRtkit ? " (rtkit)" : "");
    return "time-shared, no real-time priority (RLIMIT_RTPRIO " + juce::String(grant.rlimit) +
           ", rtkit refused)";
}

}  // namespace magda::engine::realtime

#if defined(__linux__)

    #include <dlfcn.h>
    #include <sched.h>
    #include <sys/resource.h>
    #include <sys/syscall.h>
    #include <unistd.h>

    #include <algorithm>
    #include <memory>

namespace magda::engine::realtime {
namespace {

constexpr const char* kService = "org.freedesktop.RealtimeKit1";
constexpr const char* kPath = "/org/freedesktop/RealtimeKit1";

/// sd-bus's opaque handles and its public error struct, declared here so the build needs no header.
struct sd_bus;
struct sd_bus_message;
struct sd_bus_error {
    const char* name = nullptr;
    const char* message = nullptr;
    int needFree = 0;
};

/// libsystemd's sd-bus, dlopened so MAGDA runs where it is missing.
struct Bus {
    int (*open_system)(sd_bus**);
    sd_bus* (*flush_close_unref)(sd_bus*);
    int (*call_method)(sd_bus*, const char*, const char*, const char*, const char*, sd_bus_error*,
                       sd_bus_message**, const char*, ...);
    int (*get_property_trivial)(sd_bus*, const char*, const char*, const char*, const char*,
                                sd_bus_error*, char, void*);
    sd_bus_message* (*message_unref)(sd_bus_message*);
    void (*error_free)(sd_bus_error*);
};

template <typename Fn> bool bind(void* library, Fn& fn, const char* name) {
    fn = reinterpret_cast<Fn>(dlsym(library, name));
    return fn != nullptr;
}

const Bus* bus() {
    static const auto loaded = []() -> std::unique_ptr<Bus> {
        auto* library = dlopen("libsystemd.so.0", RTLD_LAZY);
        if (library == nullptr)
            return nullptr;

        auto api = std::make_unique<Bus>();
        const auto complete =
            bind(library, api->open_system, "sd_bus_open_system") &&
            bind(library, api->flush_close_unref, "sd_bus_flush_close_unref") &&
            bind(library, api->call_method, "sd_bus_call_method") &&
            bind(library, api->get_property_trivial, "sd_bus_get_property_trivial") &&
            bind(library, api->message_unref, "sd_bus_message_unref") &&
            bind(library, api->error_free, "sd_bus_error_free");
        return complete ? std::move(api) : nullptr;
    }();
    return loaded.get();
}

template <typename T>
bool property(const Bus& api, sd_bus* connection, const char* name, char type, T& value) {
    sd_bus_error error;
    const auto result =
        api.get_property_trivial(connection, kService, kPath, kService, name, &error, type, &value);
    api.error_free(&error);
    return result >= 0;
}

/// rtkit refuses a process whose RLIMIT_RTTIME hard limit is unset or above its RTTimeUSecMax.
bool limitRealtimeTime(std::int64_t maxMicroseconds) {
    rlimit limit{};
    if (getrlimit(RLIMIT_RTTIME, &limit) != 0)
        return false;
    const auto most = static_cast<rlim_t>(maxMicroseconds);
    if (limit.rlim_max != RLIM_INFINITY && limit.rlim_max <= most)
        return true;
    limit.rlim_cur = std::min(limit.rlim_cur, most);
    limit.rlim_max = most;
    return setrlimit(RLIMIT_RTTIME, &limit) == 0;
}

int askRtkit(std::int64_t threadId, int priority) {
    const auto* api = bus();
    if (api == nullptr)
        return 0;

    sd_bus* connection = nullptr;
    if (api->open_system(&connection) < 0)
        return 0;
    const std::unique_ptr<sd_bus, sd_bus* (*)(sd_bus*)> closer(connection, api->flush_close_unref);

    std::int32_t ceiling = 0;
    std::int64_t maxTime = 0;
    if (!property(*api, connection, "MaxRealtimePriority", 'i', ceiling) ||
        !property(*api, connection, "RTTimeUSecMax", 'x', maxTime) || ceiling <= 0 ||
        !limitRealtimeTime(maxTime))
        return 0;

    const auto granted = std::min<std::int32_t>(priority, ceiling);
    sd_bus_error error;
    sd_bus_message* reply = nullptr;
    const auto result = api->call_method(
        connection, kService, kPath, kService, "MakeThreadRealtime", &error, &reply, "tu",
        static_cast<std::uint64_t>(threadId), static_cast<std::uint32_t>(granted));
    api->message_unref(reply);
    api->error_free(&error);
    return result >= 0 ? granted : 0;
}

}  // namespace

Grant raise(std::int64_t threadId, int priority) {
    Grant grant;
    rlimit limit{};
    if (getrlimit(RLIMIT_RTPRIO, &limit) == 0)
        grant.rlimit = limit.rlim_cur == RLIM_INFINITY
                           ? priority
                           : static_cast<int>(std::min<rlim_t>(limit.rlim_cur, priority));

    sched_param param{};
    param.sched_priority = grant.rlimit;
    if (grant.rlimit > 0 &&
        sched_setscheduler(static_cast<pid_t>(threadId), SCHED_RR, &param) == 0) {
        grant.priority = grant.rlimit;
        return grant;
    }

    grant.priority = askRtkit(threadId, priority);
    grant.viaRtkit = grant.priority > 0;
    return grant;
}

std::int64_t currentThreadId() {
    return static_cast<std::int64_t>(syscall(SYS_gettid));
}

bool currentThreadIsRealtime() {
    // rtkit and PipeWire set SCHED_RESET_ON_FORK alongside the policy.
    const auto policy = sched_getscheduler(0) & ~SCHED_RESET_ON_FORK;
    return policy == SCHED_FIFO || policy == SCHED_RR;
}

}  // namespace magda::engine::realtime

#else

namespace magda::engine::realtime {

Grant raise(std::int64_t, int) {
    return {};
}

std::int64_t currentThreadId() {
    return 0;
}

bool currentThreadIsRealtime() {
    return true;
}

}  // namespace magda::engine::realtime

#endif
