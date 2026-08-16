#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "PlanEditSequence.hpp"
#include "exec/PlanExecutor.hpp"
#include "exec/PlanLayout.hpp"
#include "exec/PlanValues.hpp"
#include "exec/RuntimeStateStore.hpp"
#include "plan/PlanCrossfade.hpp"
#include "plan/PlanDiff.hpp"

/**
 * @file PlanEditHarness.hpp
 * @brief One project, published over and over, with everything a property
 *        needs to look at afterwards (#2077).
 *
 * This is a session in every respect the differ can see: a runtime store that
 * owns the instances, a plan compiled and crossfaded against the one before it,
 * an executor prepared against its predecessor, and the predecessor destroyed
 * once the new one has taken what it could carry. What it is not is
 * EngineSession, and the difference is deliberate. The session owns a transport
 * and a publish protocol that are tested where they live; a property test wants
 * the timeline to be a number it controls, and it wants the counts the executor
 * keeps about what it adopted, which are not a session's to report.
 *
 * The one thing it copies exactly is the order things happen in, because that
 * order is a claim: the epoch being replaced is destroyed after the new one has
 * prepared against it, so every property that renders afterwards is also the
 * assertion that carrying state across a swap survives the old epoch going away.
 */

namespace magda::edits {

/// What the sources play, chosen per property rather than once for the harness.
///
/// A ramp makes every sample of a track distinguishable from every other, so a
/// reader that restarted, a delay line that was rebuilt and a fade that ran
/// where none was called for are all visible in a comparison. A constant makes
/// the steady state a number, which is what a discontinuity bound has to be
/// measured against, and it is exactly what a ramp cannot give.
enum class Material : std::uint8_t { Ramp, Constant };

/// The device properties every harness prepares for.
constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 64;
constexpr int kNumChannels = 2;

/// The longest a generated device may claim, and the most a generated project
/// may hold. Between them they bound how long a render has to run before the
/// last delay line has filled, which is what lets both legs of a comparison
/// render the same fixed number of blocks.
constexpr int kMaxDeviceLatency = 32;

/// Where a device instance was destroyed, and which one. The store promises to
/// destroy nothing the live plan or the model still names, and to do it on the
/// thread that publishes; both are only checkable from outside the store.
struct Ledger {
    std::vector<engine::DeviceKey> destroyed;
    std::thread::id lastDestroyingThread;
    int created = 0;
};

/// Gain and a delay of exactly the latency it reports, so latency in the plan
/// is latency in the signal and a compensation error is audible rather than
/// merely stated. The ring is sized for the largest latency a generated device
/// can claim, because reporting a new one is a re-prepare of the plan and never
/// a prepare of the instance.
class TestDevice final : public engine::EngineDevice {
  public:
    TestDevice(engine::DeviceKey key, float gain, Ledger& ledger);
    ~TestDevice() override;

    TestDevice(const TestDevice&) = delete;
    TestDevice& operator=(const TestDevice&) = delete;
    TestDevice(TestDevice&&) = delete;
    TestDevice& operator=(TestDevice&&) = delete;

    void prepare(const engine::RenderContext& context) override;
    void process(engine::DeviceBlock& block) override;

    int latencySamples() const override {
        return latency_;
    }

    void setLatency(int samples) {
        latency_ = samples;
    }

  private:
    engine::DeviceKey key_;
    float gain_ = 1.0f;
    int latency_ = 0;
    Ledger& ledger_;
    std::vector<std::vector<float>> rings_;
    int cursor_ = 0;
};

/// The analysis device every track carries: transparent, and it keeps what
/// passed through it. It goes into the ledger like any other device, because it
/// is one: the model names it, the store owns it, and a track that leaves the
/// project takes it along.
class CaptureDevice final : public engine::EngineDevice {
  public:
    CaptureDevice(engine::DeviceKey key, Ledger& ledger);
    ~CaptureDevice() override;

    CaptureDevice(const CaptureDevice&) = delete;
    CaptureDevice& operator=(const CaptureDevice&) = delete;
    CaptureDevice(CaptureDevice&&) = delete;
    CaptureDevice& operator=(CaptureDevice&&) = delete;

    void process(engine::DeviceBlock& block) override;

    std::vector<float> samples;

  private:
    engine::DeviceKey key_;
    Ledger& ledger_;
};

/// A track's material. Counts its own samples rather than reading the block's
/// position, so a source that was rebuilt mid-sequence would be visible as a
/// discontinuity rather than silently producing the right thing again.
class TrackSource final : public engine::EngineAudioSource {
  public:
    TrackSource(TrackId trackId, Material material);

    void render(const engine::BlockInfo& block, juce::dsp::AudioBlock<float> out) override;

  private:
    Material material_;
    float level_ = 1.0f;
    int counter_ = 0;
};

class Factory final : public engine::RuntimeStateFactory {
  public:
    Factory(Ledger& ledger, Material material) : ledger_(ledger), material_(material) {}

    std::unique_ptr<engine::EngineDevice> createDevice(engine::DeviceKey key) override;
    std::unique_ptr<engine::EngineAudioSource> createClipAudioSource(TrackId trackId) override;
    std::unique_ptr<engine::EngineMidiSource> createClipMidiSource(TrackId trackId) override;
    std::unique_ptr<engine::LevelTap> createMeter(const engine::OpKey& key) override;

    /// Live instances, so the harness can tell one what to report and a
    /// property can read what one captured.
    std::map<engine::DeviceKey, TestDevice*> devices;
    std::map<TrackId, CaptureDevice*> captures;

  private:
    Ledger& ledger_;
    Material material_;
};

/// Everything one publish produced, for the properties to read.
struct Published {
    /// Empty when the publish went through. Anything else is a failure the
    /// property reports as it stands: a plan that does not compile cleanly is
    /// not a plan the differ's decisions can be judged against.
    std::string failure;

    engine::RenderPlan compiled;
    engine::CrossfadedPlan faded;
    engine::PlanDiff diff;
    engine::PreparedLayout layout;

    /// What the executor said it adopted rather than built.
    int carriedDelayLines = 0;
    int carriedCrossfades = 0;
    int reportedLatency = 0;

    /// Device instances the store destroyed on this publish.
    std::vector<engine::DeviceKey> destroyed;
};

/**
 * @brief The project, the epochs it has been through, and the samples they made.
 */
class Harness {
  public:
    explicit Harness(Material material);

    /// Compile, crossfade, prepare and swap. The plan handed to the executor is
    /// the crossfaded one, which is what the session publishes and therefore
    /// what the next diff has to be taken against.
    Published publish(const Project& project);

    /// Render @p blocks of kBlockSize. The timeline runs on across publishes,
    /// because it is a property of the session rather than of any plan.
    void render(int blocks);

    /// What a track's analysis device has seen since the harness was made. By
    /// value: a track removed by a later edit takes its device with it, and a
    /// reference into one is a reference into something a publish may destroy.
    std::vector<float> capture(TrackId trackId) const;

    bool hasCapture(TrackId trackId) const;

    /// The plan the last publish left live, for a property that wants to look
    /// at what is playing rather than at what was decided.
    const engine::RenderPlan* livePlan() const {
        return livePlan_.get();
    }

    const Ledger& ledger() const {
        return ledger_;
    }

  private:
    Ledger ledger_;
    Factory factory_;
    engine::RuntimeStateStore store_;
    engine::RenderContext context_{kSampleRate, kBlockSize, kNumChannels};

    /// Every plan the harness has published. The executor points into its plan
    /// and the next crossfade pass reads the one before, so they are kept
    /// rather than replaced.
    std::vector<std::shared_ptr<engine::RenderPlan>> plans_;
    std::shared_ptr<engine::RenderPlan> livePlan_;

    std::unique_ptr<engine::PlanExecutor> live_;
    engine::PlanValues values_;
    juce::AudioBuffer<float> output_;
    std::int64_t timeline_ = 0;
};

/// Per op: what a Device op's instance reports, and zero everywhere else. The
/// same numbers the harness gives the instances, so a property comparing its
/// own layout against the executor's is comparing two readings of one project.
std::vector<int> deviceLatencyPerOp(const engine::RenderPlan& plan, const Project& project);

/// The samples a fade takes at kSampleRate, which is what the executor builds.
int fadeSamples();

}  // namespace magda::edits
