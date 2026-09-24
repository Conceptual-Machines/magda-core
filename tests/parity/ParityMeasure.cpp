#include "ParityMeasure.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

#if defined(__APPLE__)
    #include <libproc.h>
    #include <sys/resource.h>
    #include <unistd.h>
#elif defined(__linux__)
    #include <sys/resource.h>
    #include <unistd.h>

    #include <fstream>
#elif defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>

    // psapi.h needs windows.h's types first.
    #include <psapi.h>
#endif

namespace magda::parity {

namespace {

/// Nearest-rank percentile of an ascending vector.
double percentile(const std::vector<double>& sorted, double fraction) {
    if (sorted.empty())
        return 0.0;
    const auto rank =
        static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(sorted.size())));
    return sorted[std::clamp<std::size_t>(rank, 1, sorted.size()) - 1];
}

}  // namespace

BlockTimeSummary BlockTimes::summary() const {
    BlockTimeSummary result;
    if (micros_.empty())
        return result;

    auto sorted = micros_;
    std::sort(sorted.begin(), sorted.end());

    result.blocks = static_cast<int>(sorted.size());
    result.meanUs =
        std::accumulate(sorted.begin(), sorted.end(), 0.0) / static_cast<double>(sorted.size());
    result.p50Us = percentile(sorted, 0.50);
    result.p95Us = percentile(sorted, 0.95);
    result.p99Us = percentile(sorted, 0.99);
    result.maxUs = sorted.back();
    return result;
}

double processCpuSeconds() {
#if defined(_WIN32)
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        return 0.0;
    const auto toSeconds = [](const FILETIME& time) {
        ULARGE_INTEGER value;
        value.LowPart = time.dwLowDateTime;
        value.HighPart = time.dwHighDateTime;
        return static_cast<double>(value.QuadPart) * 1.0e-7;
    };
    return toSeconds(kernel) + toSeconds(user);
#else
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    const auto toSeconds = [](const timeval& time) {
        return static_cast<double>(time.tv_sec) + static_cast<double>(time.tv_usec) * 1.0e-6;
    };
    return toSeconds(usage.ru_utime) + toSeconds(usage.ru_stime);
#endif
}

std::int64_t currentFootprintBytes() {
#if defined(__APPLE__)
    rusage_info_v2 info{};
    if (proc_pid_rusage(getpid(), RUSAGE_INFO_V2, reinterpret_cast<rusage_info_t*>(&info)) != 0)
        return 0;
    return static_cast<std::int64_t>(info.ri_phys_footprint);
#elif defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    long long size = 0;
    long long resident = 0;
    if (!(statm >> size >> resident))
        return 0;
    return static_cast<std::int64_t>(resident) * sysconf(_SC_PAGESIZE);
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof(counters)))
        return 0;
    return static_cast<std::int64_t>(counters.PrivateUsage);
#else
    return 0;
#endif
}

FootprintSampler::FootprintSampler(std::chrono::milliseconds interval) {
    peak_ = currentFootprintBytes();
    thread_ = std::thread([this, interval] {
        while (running_.load(std::memory_order_relaxed)) {
            const auto now = currentFootprintBytes();
            if (now > peak_.load(std::memory_order_relaxed))
                peak_.store(now, std::memory_order_relaxed);
            std::this_thread::sleep_for(interval);
        }
    });
}

FootprintSampler::~FootprintSampler() {
    stop();
}

std::int64_t FootprintSampler::stop() {
    running_ = false;
    if (thread_.joinable())
        thread_.join();
    return std::max(peak_.load(), currentFootprintBytes());
}

}  // namespace magda::parity
