#include "../audio/AudioBridge.hpp"
#include "../engine/AudioEngine.hpp"
#include "remote_subscriptions.hpp"

namespace magda::remote {
namespace {

/**
 * Reads the levels the audio path has already published, and nothing else.
 *
 * The audio thread's part of metering is over before this runs: `LevelMeasurer`
 * clients are drained on the message thread at 30 Hz and pushed into a lock-free
 * ring, and this drains that ring. So there is no WebSocket work on the audio
 * thread, no work added to it, and no new tap in the graph — a meter subscriber
 * costs the audio path exactly nothing.
 *
 * `drainToLatest` rather than `peekLatest`: the ring is eight deep and pushes
 * keep arriving whether or not anyone reads, so a reader that never consumes
 * sees the ring fill and then stops seeing anything new. Draining to the newest
 * value is also the sampling policy the subscription asks for — latest value
 * wins, intermediate readings discarded.
 *
 * This is a dedicated ring, not the one the mixer reads, because popping from
 * that one would take frames away from the on-screen meters.
 */
class LiveMeterSource final : public MeterSource {
  public:
    explicit LiveMeterSource(AudioEngine& engine) : engine_(engine) {}

    std::vector<TrackLevels> sample() override {
        std::vector<TrackLevels> levels;

        auto* bridge = engine_.getAudioBridge();
        if (bridge == nullptr)
            return levels;

        // The ring is indexed by track id, so ask the tracks that exist rather
        // than sweeping all 128 slots. Ids come from the ring's own bound, which
        // is what makes the loop safe without knowing the project.
        auto& buffer = bridge->getRemoteMeteringBuffer();
        for (TrackId trackId = 0; trackId < MeteringBuffer::kMaxTracks; ++trackId) {
            MeterData data;
            if (!buffer.drainToLatest(trackId, data))
                continue;
            levels.push_back({trackId, data.peakL, data.peakR, data.clipped});
        }

        // The master bus is metered separately — it has no track id and no ring,
        // just a pair of atomics the same 30 Hz pass writes. It is reported under
        // the master sentinel so a client addresses it the way it addresses the
        // master track everywhere else in the API.
        levels.push_back({MASTER_TRACK_ID, bridge->getMasterPeakL(), bridge->getMasterPeakR(),
                          bridge->getMasterPeakL() > 1.0f || bridge->getMasterPeakR() > 1.0f});
        return levels;
    }

  private:
    AudioEngine& engine_;
};

}  // namespace

std::unique_ptr<MeterSource> makeLiveMeterSource(AudioEngine& engine) {
    return std::make_unique<LiveMeterSource>(engine);
}

}  // namespace magda::remote
