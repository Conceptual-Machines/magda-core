#include "PlanEditHarness.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>

#include "plan/PlanCompiler.hpp"

/**
 * @file PlanEditHarness.cpp
 * @brief The session the properties publish into (#2077).
 */

namespace magda::edits {
namespace {

/// Distinct enough per device that a chain of them is a level nothing else in
/// the project lands on by accident, and never so far from unity that eighteen
/// of them in series leave nothing to measure.
float gainFor(DeviceId deviceId) {
    constexpr std::array<float, 4> gains{0.75f, 0.8125f, 0.875f, 0.9375f};
    return gains[static_cast<std::size_t>(((deviceId % 4) + 4) % 4)];
}

/// The track a mixer-analysis device belongs to. Device ids are per section and
/// the harness owns that section outright, so the id is the track.
TrackId trackOfCapture(const engine::DeviceKey& key) {
    return key.deviceId - captureDeviceId(0);
}

std::string joined(const std::vector<std::string>& messages) {
    std::ostringstream out;
    for (std::size_t i = 0; i < messages.size(); ++i)
        out << (i == 0 ? "" : "; ") << messages[i];
    return out.str();
}

}  // namespace

// --- the runtime objects -----------------------------------------------------

TestDevice::TestDevice(engine::DeviceKey key, float gain, Ledger& ledger)
    : key_(key), gain_(gain), ledger_(ledger) {
    ++ledger_.created;
}

TestDevice::~TestDevice() {
    ledger_.destroyed.push_back(key_);
    ledger_.lastDestroyingThread = std::this_thread::get_id();
}

void TestDevice::prepare(const engine::RenderContext& context) {
    // Sized for the largest latency a generated device can report rather than
    // for the one it reports now: a plugin that changes what it claims
    // re-prepares the plan, never the instance, so the instance has to be able
    // to honour the new number without being told again.
    rings_.assign(static_cast<std::size_t>(context.numChannels),
                  std::vector<float>(static_cast<std::size_t>(kMaxDeviceLatency + 1), 0.0f));
    cursor_ = 0;
}

void TestDevice::process(engine::DeviceBlock& block) {
    const auto numSamples = block.block.numSamples;
    const auto channels =
        std::min(static_cast<std::size_t>(block.audio.getNumChannels()), rings_.size());
    if (channels == 0)
        return;

    const auto capacity = static_cast<int>(rings_.front().size());
    const auto latency = std::clamp(latency_, 0, capacity - 1);

    auto cursor = cursor_;
    for (std::size_t channel = 0; channel < channels; ++channel) {
        auto* samples = block.audio.getChannelPointer(channel);
        auto& ring = rings_[channel];
        cursor = cursor_;

        for (int sample = 0; sample < numSamples; ++sample) {
            ring[static_cast<std::size_t>(cursor)] = samples[sample] * gain_;
            auto read = cursor - latency;
            if (read < 0)
                read += capacity;
            samples[sample] = ring[static_cast<std::size_t>(read)];
            cursor = (cursor + 1) % capacity;
        }
    }
    cursor_ = cursor;
}

CaptureDevice::CaptureDevice(engine::DeviceKey key, Ledger& ledger) : key_(key), ledger_(ledger) {
    ++ledger_.created;
}

CaptureDevice::~CaptureDevice() {
    ledger_.destroyed.push_back(key_);
    ledger_.lastDestroyingThread = std::this_thread::get_id();
}

void CaptureDevice::process(engine::DeviceBlock& block) {
    const auto* channel =
        block.audio.getNumChannels() > 0 ? block.audio.getChannelPointer(0) : nullptr;
    for (int sample = 0; sample < block.block.numSamples; ++sample)
        samples.push_back(channel == nullptr ? 0.0f : channel[sample]);
}

TrackSource::TrackSource(TrackId trackId, Material material) : material_(material) {
    // Powers of two, so every level and every step of the ramp is exact in
    // float and a comparison between two runs is an equality rather than a
    // tolerance.
    level_ = 0.25f + (0.25f * static_cast<float>(((trackId % 3) + 3) % 3));
}

void TrackSource::render(const engine::BlockInfo& block, juce::dsp::AudioBlock<float> out) {
    juce::ignoreUnused(block);

    const auto numSamples = static_cast<int>(out.getNumSamples());
    for (int sample = 0; sample < numSamples; ++sample) {
        const auto ramp = static_cast<float>(counter_ % 512) / 512.0f;
        const auto value = material_ == Material::Constant ? level_ : level_ * (0.5f + ramp);
        for (std::size_t channel = 0; channel < out.getNumChannels(); ++channel)
            out.getChannelPointer(channel)[sample] = value;
        ++counter_;
    }
}

std::unique_ptr<engine::EngineDevice> Factory::createDevice(engine::DeviceKey key) {
    if (key.segment == ChainSegment::MixerAnalysis) {
        auto capture = std::make_unique<CaptureDevice>(key, ledger_);
        captures[trackOfCapture(key)] = capture.get();
        return capture;
    }

    auto device = std::make_unique<TestDevice>(key, gainFor(key.deviceId), ledger_);
    devices[key] = device.get();
    return device;
}

std::unique_ptr<engine::EngineAudioSource> Factory::createClipAudioSource(TrackId trackId) {
    return std::make_unique<TrackSource>(trackId, material_);
}

namespace {

/// A track with an instrument on it compiles a MIDI source, and an op with no
/// binding is a message the properties would have to allow for. It plays
/// nothing: what MIDI does to a plan is topology, and topology is what is
/// being asserted.
class SilentMidi final : public engine::EngineMidiSource {
  public:
    void render(const engine::BlockInfo&, juce::MidiBuffer&) override {}
};

}  // namespace

std::unique_ptr<engine::EngineMidiSource> Factory::createClipMidiSource(TrackId trackId) {
    juce::ignoreUnused(trackId);
    return std::make_unique<SilentMidi>();
}

std::unique_ptr<engine::LevelTap> Factory::createMeter(const engine::OpKey& key) {
    juce::ignoreUnused(key);
    return std::make_unique<engine::LevelTap>();
}

// --- the harness -------------------------------------------------------------

Harness::Harness(Material material) : factory_(ledger_, material), store_(factory_) {
    output_.setSize(kNumChannels, kBlockSize);
    output_.clear();
}

bool Harness::hasCapture(TrackId trackId) const {
    return factory_.captures.count(trackId) != 0;
}

std::vector<float> Harness::capture(TrackId trackId) const {
    const auto found = factory_.captures.find(trackId);
    return found == factory_.captures.end() ? std::vector<float>{} : found->second->samples;
}

Published Harness::publish(const Project& project) {
    Published published;
    published.compiled = engine::compileRenderPlan(project.tracks, project.master);

    if (!published.compiled.diagnostics.empty()) {
        published.failure =
            "the compiler could not express the model: " + joined(published.compiled.diagnostics);
        return published;
    }
    if (const auto problems = validatePlan(published.compiled); !problems.empty()) {
        published.failure = "the compiled plan does not validate: " + joined(problems);
        return published;
    }

    const auto* previousPlan = livePlan_ == nullptr ? nullptr : livePlan_.get();
    const auto stillFading = live_ == nullptr ? std::vector<char>{} : live_->unfinishedCrossfades();

    published.faded =
        previousPlan == nullptr
            ? engine::CrossfadedPlan{published.compiled, 0, 0}
            : engine::insertCrossfades(*previousPlan, published.compiled, stillFading);
    published.diff = previousPlan == nullptr
                         ? engine::PlanDiff{}
                         : engine::diffPlans(*previousPlan, published.faded.plan);

    if (const auto problems = validatePlan(published.faded.plan); !problems.empty()) {
        published.failure = "the crossfaded plan does not validate: " + joined(problems);
        return published;
    }

    auto plan = std::make_shared<engine::RenderPlan>(published.faded.plan);

    engine::PlanValues values;
    if (const auto problems =
            engine::resolvePlanValues(*plan, project.tracks, project.master, values);
        !problems.empty()) {
        published.failure = "values do not resolve: " + joined(problems);
        return published;
    }

    auto bindings = store_.realise(*plan, context_);

    // What each instance reports, set after it exists and before the plan is
    // prepared against it. A device that was already there is told its new
    // number here, which is the whole of what a latency change is.
    for (const auto& [key, device] : factory_.devices) {
        const auto found = project.deviceLatency.find(key);
        device->setLatency(found == project.deviceLatency.end() ? 0 : found->second);
    }

    published.layout = engine::resolveLayout(*plan, deviceLatencyPerOp(*plan, project));

    auto executor = std::make_unique<engine::PlanExecutor>();
    if (const auto problems = executor->prepare(*plan, bindings, context_, live_.get());
        !problems.empty()) {
        published.failure = "the plan does not prepare: " + joined(problems);
        return published;
    }

    published.carriedDelayLines = executor->carriedDelayLines();
    published.carriedCrossfades = executor->carriedCrossfades();
    published.reportedLatency = executor->latencySamples();

    const auto destroyedBefore = ledger_.destroyed.size();

    // The swap, in the order the session does it: the epoch being replaced is
    // let go of once the new one has taken what it could carry, and what the
    // model no longer names goes after that.
    plans_.push_back(plan);
    livePlan_ = plan;
    values_ = std::move(values);
    live_ = std::move(executor);

    store_.releaseDeleted(*plan, engine::collectRuntimeStateIds(project.tracks, project.master));

    for (auto i = destroyedBefore; i < ledger_.destroyed.size(); ++i)
        published.destroyed.push_back(ledger_.destroyed[i]);

    // A destroyed instance is one nothing may reach again, so the maps that
    // hand them out have to lose it at the same moment.
    for (const auto& key : published.destroyed) {
        factory_.devices.erase(key);
        if (key.segment == ChainSegment::MixerAnalysis)
            factory_.captures.erase(key.deviceId - captureDeviceId(0));
    }

    return published;
}

void Harness::render(int blocks) {
    if (live_ == nullptr)
        return;

    for (int block = 0; block < blocks; ++block) {
        engine::BlockInfo info;
        info.numSamples = kBlockSize;
        info.playing = true;
        info.startBeat = static_cast<double>(timeline_) / kSampleRate;
        info.endBeat = static_cast<double>(timeline_ + kBlockSize) / kSampleRate;
        info.continuous = timeline_ > 0;

        output_.clear();
        live_->process(values_, info, output_);
        timeline_ += kBlockSize;
    }
}

// --- shared readings ---------------------------------------------------------

std::vector<int> deviceLatencyPerOp(const engine::RenderPlan& plan, const Project& project) {
    std::vector<int> latency(plan.ops.size(), 0);

    for (std::size_t i = 0; i < plan.ops.size(); ++i) {
        if (plan.ops[i].kind != engine::OpKind::Device)
            continue;
        const auto found = project.deviceLatency.find(plan.ops[i].key.deviceKey());
        if (found != project.deviceLatency.end())
            latency[i] = found->second;
    }

    return latency;
}

int fadeSamples() {
    return std::max(1, static_cast<int>(std::lround(engine::kCrossfadeSeconds * kSampleRate)));
}

}  // namespace magda::edits
