#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "AllocationWatch.hpp"
#include "EngineSessionScaffold.hpp"
#include "core/TrackInfo.hpp"
#include "exec/EngineSession.hpp"
#include "exec/RuntimeStateStore.hpp"
#include "io/LiveInput.hpp"
#include "io/RecordStream.hpp"
#include "io/TakeRecorder.hpp"
#include "plan/RenderPlan.hpp"
#include "tap/RecordTap.hpp"
#include "transport/TempoMap.hpp"
#include "transport/TransportState.hpp"

/**
 * @file test_take_across_swap.cpp
 * @brief A take that survives a plan swap (#2465).
 *
 * Every other slice of recording fails loudly. This one fails by losing a
 * performance, so the cases are about what an edit does to a recording it has
 * nothing to do with -- a device added to another track, a rename, a clip moved
 * -- and about the three edits that genuinely end one.
 *
 * A real EngineSession rather than a take driven by hand, because the swap is
 * the session's: the plan, the executor and everything the store holds are
 * replaced between two callbacks, and the claim is that the take is not.
 *
 * Every input sample says which arrival it is, so a file read back afterwards
 * says where the take began and whether anything is missing from the middle of
 * it -- a rebuilt writer shows up as a repeat or a gap, not as a wrong length.
 */

using namespace magda;
using magda::engine::AudioFileFormat;
using magda::engine::ClosedTake;
using magda::engine::EngineAudioSource;
using magda::engine::EngineSession;
using magda::engine::LiveAudioInput;
using magda::engine::LiveInputBlock;
using magda::engine::LiveInputFeed;
using magda::engine::RecordedTake;
using magda::engine::RecordMaterial;
using magda::engine::RecordTap;
using magda::engine::RenderContext;
using magda::engine::RuntimeStateFactory;
using magda::engine::TakeKey;
using magda::engine::TakeRecorder;
using magda::engine::TakeRecorderSettings;
using magda::engine::TransportSnapshot;
using magda::test::AllocationWatch;

namespace {

constexpr double kSampleRate = 8000.0;
constexpr int kBlockSize = 64;
constexpr int kNumChannels = 2;

constexpr TrackId kArmed = 1;
constexpr TrackId kOther = 2;

constexpr TakeKey kTakeKey{kArmed, RecordMaterial::audio};

/// What arrival @p sample of @p channel carries, as test_take_recorder numbers
/// it: inside full scale and far enough apart that two arrivals cannot round
/// onto one float.
float material(std::int64_t sample, int channel) {
    return static_cast<float>(sample + 1 + (static_cast<std::int64_t>(channel) * 50000)) * 1.0e-5f;
}

/// The arrival a stored sample came from, which is what makes a file readable
/// as a stretch of the input rather than as a length.
std::int64_t arrivalAt(const juce::AudioBuffer<float>& stored, int at) {
    return std::llround(static_cast<double>(stored.getSample(0, at)) * 1.0e5) - 1;
}

juce::File emptyDirectory(const juce::String& name) {
    auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("magda_take_swap_test")
                         .getChildFile(name);
    directory.deleteRecursively();
    directory.createDirectory();
    return directory;
}

juce::AudioBuffer<float> readBack(const juce::File& file) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    REQUIRE(reader != nullptr);

    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels),
                                    static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
    return buffer;
}

/// A track pointed at hardware, which is what makes its input route external.
TrackInfo armedTrack(TrackId id) {
    auto track = magda::test::makeTrack(id);
    track.audioInputDevice = "In 1 + 2";
    track.recordArmed = true;
    return track;
}

/// Binds the live input ops so the armed track's chain is a chain the plan can
/// render. The take reads the feed itself and not this, which is the point: an
/// input op is topology and a take is not.
class InputFactory final : public RuntimeStateFactory {
  public:
    std::unique_ptr<EngineAudioSource> createAudioInput(TrackId) override {
        if (feed == nullptr)
            return nullptr;

        static constexpr std::array<int, 2> kChannels{0, 1};
        return std::make_unique<LiveAudioInput>(*feed, kChannels);
    }

    /// Set once the session exists, since the feed is the session's.
    const LiveInputFeed* feed = nullptr;
};

/**
 * @brief One session, one armed track, one take, driven a callback at a time.
 *
 * An edit is a mutation of the track list followed by a republish, which is
 * what an edit is from the session's side and the only thing this rig does
 * differently from any other engine test.
 */
class Rig {
  public:
    explicit Rig(const juce::File& directory)
        : directory_(directory),
          session_(factory_),
          input_(kNumChannels, kBlockSize),
          output_(kNumChannels, kBlockSize) {
        tracks_.push_back(armedTrack(kArmed));
        tracks_.push_back(magda::test::makeTrack(kOther));

        factory_.feed = &session_.liveInputs();
        session_.liveInputs().prepare(kNumChannels, kBlockSize);

        transport_.tempo = magda::engine::TempoMap({{0.0, 120.0, 0.0f}}, {{0.0, 4, 4}});
        session_.publishTransport(transport_);

        REQUIRE(publish());
        startTake();
    }

    /// Start a take on the armed track, named apart from any before it so a
    /// case can tell two files from each other.
    void startTake() {
        TakeRecorderSettings settings;
        settings.channels = {0, 1};
        settings.directory = directory_;
        settings.name = "take" + juce::String(++takes_);
        settings.file.format = AudioFileFormat::wav;
        settings.file.bitDepth = 32;

        session_.startTake(kTakeKey, {}, [this, &settings](RecordTap& tap) {
            auto recorder = std::make_unique<TakeRecorder>(session_.liveInputs(), context_, tap,
                                                           std::move(settings));
            recorder_ = recorder.get();
            return recorder;
        });
    }

    void play() {
        ++transport_.request.generation;
        transport_.request.playing = true;
        transport_.request.locate = true;
        transport_.request.positionBeat = 0.0;
        session_.publishTransport(transport_);
    }

    /// Feed @p numSamples of callbacks through the session.
    void run(int numSamples) {
        for (auto left = numSamples; left > 0;) {
            const auto callback = std::min(kBlockSize, left);
            deliver(callback);
            left -= callback;
        }
    }

    /// One callback, with the heap watched. What the swap claim is measured on.
    std::size_t runWatched(int numSamples) {
        const AllocationWatch watch;
        deliver(numSamples);
        return watch.total();
    }

    /// An edit: mutate the project, compile it again, swap it in.
    bool publish() {
        return magda::test::publishProject(session_, tracks_, master_, context_).published;
    }

    /// The edits the cases perform.
    void addDeviceTo(TrackId id) {
        auto& track = trackFor(id);

        DeviceInfo device;
        device.id = nextDeviceId_++;
        device.name = "Gain " + juce::String(device.id);
        device.format = PluginFormat::Internal;

        track.chain.fxChainElements.emplace_back(std::move(device));
    }

    void rename(TrackId id) {
        trackFor(id).name += " (edited)";
    }

    void disarm(TrackId id) {
        trackFor(id).recordArmed = false;
    }

    void removeInputFrom(TrackId id) {
        trackFor(id).audioInputDevice = {};
    }

    void deleteTrack(TrackId id) {
        std::erase_if(tracks_, [id](const auto& track) { return track.id == id; });
    }

    EngineSession& session() {
        return session_;
    }

    TakeRecorder& recorder() {
        return *recorder_;
    }

    /// The take the session is feeding, as the callback would reach it, or
    /// null. What says a swap moved nothing.
    const TakeRecorder* held() const {
        return recorder_;
    }

  private:
    TrackInfo& trackFor(TrackId id) {
        for (auto& track : tracks_)
            if (track.id == id)
                return track;

        FAIL("no such track");
        return tracks_.front();
    }

    void deliver(int numSamples) {
        for (auto channel = 0; channel < input_.getNumChannels(); ++channel)
            for (auto at = 0; at < numSamples; ++at)
                input_.setSample(channel, at, material(arrival_ + at, channel));

        const LiveInputBlock block{juce::dsp::AudioBlock<const float>(input_).getSubBlock(
                                       0, static_cast<std::size_t>(numSamples)),
                                   {}};

        session_.process(numSamples, output_, block);
        arrival_ += numSamples;
    }

    juce::File directory_;

    InputFactory factory_;
    EngineSession session_;

    std::vector<TrackInfo> tracks_;
    TrackInfo master_ = magda::test::makeMaster();

    RenderContext context_{kSampleRate, kBlockSize, kNumChannels};
    TransportSnapshot transport_;

    juce::AudioBuffer<float> input_;
    juce::AudioBuffer<float> output_;

    /// Owned by the session; kept here to say whether it is the same one.
    TakeRecorder* recorder_ = nullptr;

    std::int64_t arrival_ = 0;
    DeviceId nextDeviceId_ = 100;
    int takes_ = 0;
};

/// Every sample of @p stored follows the one before it, from wherever it
/// began. A rebuilt writer shows up here as a repeat or a gap.
void requireUnbroken(const juce::AudioBuffer<float>& stored) {
    REQUIRE(stored.getNumSamples() > 0);

    const auto first = arrivalAt(stored, 0);
    for (auto at = 0; at < stored.getNumSamples(); ++at)
        if (arrivalAt(stored, at) != first + at) {
            INFO("sample " << at << " came from arrival " << arrivalAt(stored, at) << ", not "
                           << first + at);
            FAIL();
        }
}

/// A take that records nothing, for the store-level case: what matters there
/// is that one is held, not what it holds.
class StubTake final : public magda::engine::TakeCapture {
  public:
    void capture(const magda::engine::BlockInfo&, bool, const magda::engine::LoopRange&) override {}

    const RecordTap& tap() const override {
        return tap_;
    }

    magda::engine::RecordStream& stream() override {
        return stream_;
    }

  private:
    class NullSink final : public magda::engine::RecordSink {};

    RecordTap tap_{RecordMaterial::audio, {}};
    NullSink sink_;
    magda::engine::RecordStream stream_{sink_};
};

/// A factory that builds nothing, for a store driven without a session.
class NoFactory final : public RuntimeStateFactory {};

/// What a closed take became, for a case that reads it back.
RecordedTake finish(ClosedTake& closed) {
    REQUIRE(closed.take != nullptr);
    REQUIRE(closed.tap != nullptr);

    auto* recorder = dynamic_cast<TakeRecorder*>(closed.take.get());
    REQUIRE(recorder != nullptr);
    return recorder->finish();
}

}  // namespace

TEST_CASE("A recompile during a pass leaves the take one unbroken file",
          "[engine][exec][session][record][2465]") {
    Rig rig(emptyDirectory("swaps"));
    rig.play();

    const auto* before = rig.held();

    // Ten edits, none of them about the armed track, spread through the pass.
    for (auto edit = 0; edit < 10; ++edit) {
        rig.run(128);

        if (edit % 2 == 0)
            rig.addDeviceTo(kOther);
        else
            rig.rename(kOther);

        REQUIRE(rig.publish());
    }

    rig.run(128);

    // The same object throughout: a swap that rebuilt it would be a file split
    // in two whatever the audio looked like.
    REQUIRE(rig.held() == before);
    REQUIRE(rig.recorder().rolling());

    auto closed = rig.session().stopTake(kTakeKey);
    const auto take = finish(closed);

    REQUIRE_FALSE(take.failed);
    REQUIRE(take.samplesLost == 0);

    const auto stored = readBack(take.file);
    REQUIRE(stored.getNumSamples() == 11 * 128);
    requireUnbroken(stored);
}

TEST_CASE("An edit to the armed track's own chain carries the take",
          "[engine][exec][session][record][2465]") {
    Rig rig(emptyDirectory("armed-chain"));
    rig.play();

    const auto* before = rig.held();

    rig.run(256);
    rig.addDeviceTo(kArmed);
    REQUIRE(rig.publish());
    rig.run(256);

    REQUIRE(rig.held() == before);

    auto closed = rig.session().stopTake(kTakeKey);
    const auto take = finish(closed);

    const auto stored = readBack(take.file);
    REQUIRE(stored.getNumSamples() == 512);
    requireUnbroken(stored);
}

TEST_CASE("Disarming mid-pass closes the take and materialises it",
          "[engine][exec][session][record][2465]") {
    Rig rig(emptyDirectory("disarm"));
    rig.play();
    rig.run(256);

    rig.disarm(kArmed);
    REQUIRE(rig.publish());

    // Out of the callback's set, and nothing else in the session is recording.
    auto closed = rig.session().takeClosedTakes();
    REQUIRE(closed.size() == 1);
    REQUIRE(closed.front().key == kTakeKey);

    // Blocks after the edit reach no take, so nothing is added to it.
    rig.run(256);

    const auto take = finish(closed.front());
    REQUIRE_FALSE(take.failed);

    const auto stored = readBack(take.file);
    REQUIRE(stored.getNumSamples() == 256);
    requireUnbroken(stored);
    REQUIRE(arrivalAt(stored, 0) == 0);
}

TEST_CASE("A track that loses its input closes the take it was feeding",
          "[engine][exec][session][record][2465]") {
    Rig rig(emptyDirectory("input-gone"));
    rig.play();
    rig.run(192);

    rig.removeInputFrom(kArmed);
    REQUIRE(rig.publish());

    auto closed = rig.session().takeClosedTakes();
    REQUIRE(closed.size() == 1);

    const auto take = finish(closed.front());
    REQUIRE_FALSE(take.failed);
    REQUIRE(readBack(take.file).getNumSamples() == 192);
}

TEST_CASE("Deleting the recording track closes the file and loses nothing",
          "[engine][exec][session][record][2465]") {
    Rig rig(emptyDirectory("deleted"));
    rig.play();
    rig.run(320);

    rig.deleteTrack(kArmed);
    REQUIRE(rig.publish());

    auto closed = rig.session().takeClosedTakes();
    REQUIRE(closed.size() == 1);

    const auto take = finish(closed.front());
    REQUIRE_FALSE(take.failed);
    REQUIRE(take.samplesLost == 0);

    const auto stored = readBack(take.file);
    REQUIRE(stored.getNumSamples() == 320);
    requireUnbroken(stored);
    REQUIRE(arrivalAt(stored, 0) == 0);
}

TEST_CASE("An edit that ends no take publishes nothing to the callback",
          "[engine][exec][session][record][2465]") {
    Rig rig(emptyDirectory("untouched"));
    rig.play();
    rig.run(128);

    rig.addDeviceTo(kOther);
    REQUIRE(rig.publish());

    REQUIRE(rig.session().takeClosedTakes().empty());
    REQUIRE(rig.recorder().rolling());
}

TEST_CASE("A second take on the same key closes the one it displaces",
          "[engine][exec][session][record][2465]") {
    Rig rig(emptyDirectory("displaced"));
    rig.play();
    rig.run(128);

    rig.startTake();
    rig.run(128);

    auto closed = rig.session().takeClosedTakes();
    REQUIRE(closed.size() == 1);

    const auto first = finish(closed.front());
    REQUIRE(readBack(first.file).getNumSamples() == 128);

    auto second = rig.session().stopTake(kTakeKey);
    const auto take = finish(second);

    const auto stored = readBack(take.file);
    REQUIRE(stored.getNumSamples() == 128);
    REQUIRE(arrivalAt(stored, 0) == 128);
}

TEST_CASE("The store keeps the tap of a take it is still holding",
          "[engine][exec][store][record][2465]") {
    NoFactory factory;
    magda::engine::RuntimeStateStore store(factory);

    store.realiseTakeTap(kTakeKey, {});
    store.holdTake(kTakeKey, std::make_unique<StubTake>());

    // The model has lost the track and the plan never named it, which is the
    // eviction that would free a tap the take is about to write to. The take
    // is still the callback's, so neither may go.
    const magda::engine::RenderPlan empty;
    store.releaseDeleted(empty, {}, nullptr);

    REQUIRE(store.takeTap(kTakeKey) != nullptr);
    REQUIRE(store.releaseTake(kTakeKey).tap != nullptr);

    // Once it has, the abandoned tap is the store's to drop.
    store.realiseTakeTap(kTakeKey, {});
    store.releaseDeleted(empty, {}, nullptr);
    REQUIRE(store.takeTap(kTakeKey) == nullptr);
}

TEST_CASE("A take the live plan's model does not name is not fed",
          "[engine][exec][session][record][2465]") {
    Rig rig(emptyDirectory("ineligible"));
    rig.play();
    rig.run(128);

    // Disarming closes the take the rig started and leaves an epoch whose model
    // names no take on this key.
    rig.disarm(kArmed);
    REQUIRE(rig.publish());
    REQUIRE(rig.session().takeClosedTakes().size() == 1);

    // A take started afterwards is in the callback's set and is still not fed.
    // Which is the point: eligibility rides with the epoch, so the block that
    // first renders an edit's plan is already the block that stops feeding a
    // take that edit ended -- whatever the publishing thread has reached.
    rig.startTake();
    rig.run(256);

    auto closed = rig.session().stopTake(kTakeKey);
    REQUIRE(finish(closed).empty());
}

TEST_CASE("A swap during a pass neither allocates nor frees on the audio thread",
          "[engine][exec][session][record][2465]") {
    if (!magda::test::allocationWatchWorks())
        SKIP("this build's operator new is somebody else's, so nothing would be counted");

    Rig rig(emptyDirectory("no-alloc"));
    rig.play();
    rig.run(256);

    // Warm: the first callbacks of a session touch things a later one does not.
    REQUIRE(rig.runWatched(kBlockSize) == 0);

    rig.addDeviceTo(kOther);
    REQUIRE(rig.publish());

    // The first block on the new epoch, with the take still in flight.
    REQUIRE(rig.runWatched(kBlockSize) == 0);
    REQUIRE(rig.runWatched(kBlockSize) == 0);

    auto closed = rig.session().stopTake(kTakeKey);
    const auto take = finish(closed);
    requireUnbroken(readBack(take.file));
}
