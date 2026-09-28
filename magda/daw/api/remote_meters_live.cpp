#include <algorithm>
#include <cmath>
#include <limits>

#include "../audio/TrackMeters.hpp"
#include "../audio/io/AudioIOControl.hpp"
#include "../engine/AudioEngine.hpp"
#include "remote_diagnostics.hpp"
#include "remote_subscriptions.hpp"

namespace magda::remote {
namespace {

/**
 * Reads the levels the engine has already published, and nothing else. The
 * remote snapshot stays current even without subscriptions or recent reads.
 */
class LiveMeterSource final : public MeterSource {
  public:
    explicit LiveMeterSource(AudioEngine& engine) : engine_(engine) {}

    std::vector<TrackLevels> sample() override {
        std::vector<TrackLevels> levels;

        auto& meters = engine_.meters();
        const auto now = juce::Time::getMillisecondCounter();
        constexpr juce::uint32 maxSampleAgeMs = 1000;

        for (TrackId trackId = 0; trackId < MeteringBuffer::kMaxTracks; ++trackId) {
            const auto& data = meters.remoteLatest[static_cast<size_t>(trackId)];
            if (!data.available.load(std::memory_order_acquire) ||
                now - data.publishedAtMs.load(std::memory_order_relaxed) > maxSampleAgeMs)
                continue;
            const auto left = data.peakL.load(std::memory_order_relaxed);
            const auto right = data.peakR.load(std::memory_order_relaxed);
            if (std::isfinite(left) && std::isfinite(right) && left >= 0.0f && right >= 0.0f)
                levels.push_back(
                    {trackId, left, right, data.clipped.load(std::memory_order_relaxed)});
        }

        // The master bus is metered separately — it has no track id and no ring,
        // just a pair of atomics the same 30 Hz pass writes. It is reported under
        // the master sentinel so a client addresses it the way it addresses the
        // master track everywhere else in the API.
        if (meters.hasMasterPeak() &&
            now - meters.masterPublishedAtMs.load(std::memory_order_relaxed) <= maxSampleAgeMs) {
            const auto left = meters.getMasterPeakL();
            const auto right = meters.getMasterPeakR();
            if (std::isfinite(left) && std::isfinite(right) && left >= 0.0f && right >= 0.0f)
                levels.push_back({MASTER_TRACK_ID, left, right, left > 1.0f || right > 1.0f});
        }
        return levels;
    }

    void projectReplaced() override {
        engine_.meters().clearRemotePeaks();
    }

  private:
    AudioEngine& engine_;
};

juce::var object() {
    return new juce::DynamicObject();
}

class LiveDiagnosticsSource final : public DiagnosticsSource {
  public:
    LiveDiagnosticsSource(AudioEngine& engine, std::shared_ptr<MeterSource> meters)
        : engine_(engine), meters_(std::move(meters)) {
        projectReplaced();
    }

    void projectReplaced() override {
        sinceMs_ = juce::Time::currentTimeMillis();
        problems_.clear();
        discarded_ = 0;
        missingDeviceReported_ = false;
        meters_->projectReplaced();
        auto* io = engine_.getAudioIO();
        lastXruns_ = io != nullptr ? io->status().xruns : 0;
        accumulatedXruns_ = 0;
    }

    juce::var health() override {
        const auto now = juce::Time::currentTimeMillis();
        const auto* io = engine_.getAudioIO();
        const auto status = io != nullptr ? io->status() : AudioIOControl::Status{};
        const bool deviceOpen = io != nullptr && status.deviceOpen;
        const bool projectBound = engine_.hasActiveEdit();
        if (io != nullptr) {
            if (status.xruns < lastXruns_) {
                addProblem("xrun_counter_reset", now, 1);
            } else if (status.xruns > lastXruns_) {
                const int delta = status.xruns - lastXruns_;
                accumulatedXruns_ =
                    std::min(static_cast<juce::int64>(std::numeric_limits<int>::max()),
                             accumulatedXruns_ + static_cast<juce::int64>(delta));
                addProblem("audio_xrun", now, delta);
            }
            lastXruns_ = status.xruns;
        }
        if (projectBound && !deviceOpen && !missingDeviceReported_) {
            addProblem("audio_device_unavailable", now, 1);
            missingDeviceReported_ = true;
        }
        if (deviceOpen)
            missingDeviceReported_ = false;

        auto result = object();
        auto* obj = result.getDynamicObject();
        obj->setProperty("engine", engine_.engineName());
        obj->setProperty("observedAtMs", now);
        obj->setProperty("sinceMs", projectBound ? juce::var(sinceMs_) : juce::var());
        obj->setProperty("projectBound", projectBound);
        obj->setProperty("audioDeviceOpen", deviceOpen);
        obj->setProperty("xrunCount", projectBound && io != nullptr
                                          ? juce::var(static_cast<int>(accumulatedXruns_))
                                          : juce::var());
        // JUCE exposes one combined xrun counter. A separate dropout count is
        // unavailable; reporting zero here would mislead smoke clients.
        obj->setProperty("dropoutCount", juce::var());
        obj->setProperty("callbackLoad",
                         deviceOpen && std::isfinite(status.cpuUsage) && status.cpuUsage >= 0.0
                             ? juce::var(status.cpuUsage)
                             : juce::var());
        obj->setProperty("problemCoverage", "audioIoObservations");
        juce::Array<juce::var> problems;
        for (const auto& problem : problems_)
            problems.add(problem);
        obj->setProperty("problems", problems);
        obj->setProperty("discardedProblemCount", discarded_);
        return result;
    }

    juce::var meters(const std::vector<TrackId>& trackIds) override {
        const auto now = juce::Time::currentTimeMillis();
        const auto sampled = meters_->sample();
        auto result = object();
        auto* obj = result.getDynamicObject();
        obj->setProperty("observedAtMs", now);
        juce::Array<juce::var> tracks;
        constexpr size_t limit = MeteringBuffer::kMaxTracks;
        for (size_t i = 0; i < std::min(trackIds.size(), limit); ++i) {
            const auto trackId = trackIds[i];
            auto entry = object();
            auto* item = entry.getDynamicObject();
            item->setProperty("trackId", trackId);
            auto found = std::find_if(sampled.begin(), sampled.end(), [trackId](const auto& level) {
                return level.trackId == trackId;
            });
            item->setProperty("available", found != sampled.end());
            item->setProperty("peakL",
                              found != sampled.end() ? juce::var(found->peakL) : juce::var());
            item->setProperty("peakR",
                              found != sampled.end() ? juce::var(found->peakR) : juce::var());
            item->setProperty("clipped",
                              found != sampled.end() ? juce::var(found->clipped) : juce::var());
            tracks.add(entry);
        }
        obj->setProperty("tracks", tracks);
        obj->setProperty("truncatedTrackCount",
                         static_cast<int>(trackIds.size() > limit ? trackIds.size() - limit : 0));
        auto master = object();
        auto* masterObj = master.getDynamicObject();
        const auto found = std::find_if(sampled.begin(), sampled.end(), [](const auto& level) {
            return level.trackId == MASTER_TRACK_ID;
        });
        masterObj->setProperty("available", found != sampled.end());
        masterObj->setProperty("peakL",
                               found != sampled.end() ? juce::var(found->peakL) : juce::var());
        masterObj->setProperty("peakR",
                               found != sampled.end() ? juce::var(found->peakR) : juce::var());
        masterObj->setProperty("clipped",
                               found != sampled.end() ? juce::var(found->clipped) : juce::var());
        obj->setProperty("master", master);
        return result;
    }

  private:
    void addProblem(const char* code, juce::int64 atMs, int count) {
        auto entry = object();
        entry.getDynamicObject()->setProperty("code", code);
        entry.getDynamicObject()->setProperty("atMs", atMs);
        entry.getDynamicObject()->setProperty("count", count);
        if (problems_.size() == 32) {
            problems_.erase(problems_.begin());
            ++discarded_;
        }
        problems_.push_back(entry);
    }

    AudioEngine& engine_;
    std::shared_ptr<MeterSource> meters_;
    juce::int64 sinceMs_ = 0;
    juce::int64 accumulatedXruns_ = 0;
    int lastXruns_ = 0;
    bool missingDeviceReported_ = false;
    int discarded_ = 0;
    std::vector<juce::var> problems_;
};

}  // namespace

std::unique_ptr<MeterSource> makeLiveMeterSource(AudioEngine& engine) {
    return std::make_unique<LiveMeterSource>(engine);
}

std::unique_ptr<DiagnosticsSource> makeLiveDiagnosticsSource(AudioEngine& engine,
                                                             std::shared_ptr<MeterSource> meters) {
    return std::make_unique<LiveDiagnosticsSource>(engine, std::move(meters));
}

}  // namespace magda::remote
