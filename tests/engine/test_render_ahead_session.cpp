// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "clip/SessionPlayback.hpp"
#include "core/TrackInfo.hpp"
#include "exec/EngineSession.hpp"
#include "exec/PlanValues.hpp"
#include "exec/RuntimeStateStore.hpp"
#include "plan/PlanCompiler.hpp"
#include "plan/PlanHandoff.hpp"

// Session tracks rendered ahead from copies of their launch handles (#3316).

using namespace magda;
using namespace magda::engine;

namespace {

constexpr int kBlockSize = 64;
constexpr int kDepth = 3;

TrackInfo makeTrack(TrackId id, TrackType type = TrackType::Media) {
    TrackInfo track;
    track.id = id;
    track.type = type;
    track.name = "Track " + juce::String(id);
    track.audioOutputDevice = "master";
    return track;
}

TrackInfo makeMaster() {
    auto master = makeTrack(MASTER_TRACK_ID, TrackType::Master);
    master.audioOutputDevice = {};
    return master;
}

/// While a slot sounds, each sample is one past the material beat it plays, plus ten per scene,
/// so a launch, a wrap or a stop a sample off shows. Track 1 writes the left channel and track 2
/// the right.
class RunPositionSource final : public EngineAudioSource {
  public:
    RunPositionSource(TrackId trackId, LaunchHandleFeed& handles)
        : trackId_(trackId), handles_(handles) {}

    void render(const BlockInfo& block, juce::dsp::AudioBlock<float> out) override {
        out.clear();
        const LaunchHandleFeed::Reader table(handles_, block);
        if (!table || block.numSamples <= 0)
            return;
        const auto channel = static_cast<std::size_t>(trackId_ == 2 ? 1 : 0);
        if (channel >= out.getNumChannels())
            return;
        const auto perSample = block.beats.length() / static_cast<double>(block.numSamples);

        const auto [first, last] = table->rangeFor(trackId_);
        for (const auto* entry = first; entry != last; ++entry) {
            if (entry->handle == nullptr)
                continue;
            const auto& status = entry->handle->blockStatus();
            const auto write = [&](const BlockPiece& piece, int from, int to) {
                if (!piece.origin)
                    return;
                for (int i = from; i < to; ++i) {
                    const auto beat = block.beats.start + (perSample * i) - piece.origin->beat;
                    out.addSample(static_cast<int>(channel), i,
                                  static_cast<float>(1.0 + beat + (10.0 * entry->key.sceneIndex)));
                }
            };
            const auto split = splitSample(block, status).value;
            write(status.beforeEvent, 0, split);
            if (status.afterEvent)
                write(*status.afterEvent, split, block.numSamples);
        }
    }

  private:
    TrackId trackId_;
    LaunchHandleFeed& handles_;
};

class SessionFactory final : public RuntimeStateFactory {
  public:
    std::unique_ptr<EngineAudioSource> createSessionAudioSource(TrackId trackId) override {
        return std::make_unique<RunPositionSource>(trackId, *handles);
    }
    LaunchHandleFeed* handles = nullptr;
};

/// Two slots on each of @p tracks: the first loops a beat and follows into the second after two
/// passes, the second loops three quarters of a beat.
std::shared_ptr<const ClipSnapshot> twoSlots(const std::vector<TrackId>& tracks) {
    auto snapshot = std::make_shared<ClipSnapshot>();
    for (const auto id : tracks) {
        TrackClipPlayback track;
        track.trackId = id;
        for (int scene = 0; scene < 2; ++scene) {
            SessionSlotPlayback slot;
            slot.sceneIndex = scene;
            slot.lengthBeats = scene == 0 ? 1.0 : 0.75;
            slot.loopBeats = slot.lengthBeats;
            if (scene == 0)
                slot.follow = SlotFollow{
                    .action = SlotAction::next, .loopCount = 2, .lengthBeats = slot.lengthBeats};
            slot.audio.emplace_back();
            track.session.push_back(std::move(slot));
        }
        snapshot->tracks.push_back(std::move(track));
    }
    return snapshot;
}

TransportSnapshot rolling() {
    TransportSnapshot transport;
    transport.request.generation = 1;
    transport.request.playing = true;
    transport.request.positionBeat = 0.0;
    return transport;
}

struct Rig {
    SessionFactory factory;
    EngineSession session{factory};
    std::vector<TrackId> tracks;
    std::set<TrackId> atCallback;
    int renderedAhead = 0;

    explicit Rig(int depth, std::vector<TrackId> trackIds = {1}, std::set<TrackId> live = {},
                 bool roll = true)
        : tracks(std::move(trackIds)), atCallback(std::move(live)) {
        factory.handles = &session.launchHandleFeed();
        if (depth > 0)
            session.setRenderAhead(depth, false);
        session.publishClips(twoSlots(tracks));
        publishPlan();
        if (roll)
            session.publishTransport(rolling());
    }

    void publishPlan() {
        std::vector<TrackInfo> model;
        for (const auto id : tracks)
            model.push_back(makeTrack(id));
        CompileOptions options;
        options.sessionTracks = atCallback;
        const auto plan = std::make_shared<const RenderPlan>(
            insertHandoffs(compileRenderPlan(model, makeMaster(), options)));
        PlanValues values;
        resolvePlanValues(*plan, model, makeMaster(), values);
        REQUIRE(session
                    .publish(plan, RenderContext{44100.0, kBlockSize, 2},
                             collectRuntimeStateIds(model, makeMaster()), std::move(values))
                    .published);
    }

    void callback(juce::AudioBuffer<float>& out) {
        session.process(kBlockSize, out);
        renderedAhead += session.renderAheadOnce();
    }

    void ask(const std::function<void(LaunchRequestQueue::Gesture&)>& gesture) {
        LaunchRequestQueue::Gesture asked(session.launchRequests());
        gesture(asked);
    }

    /// Where the transport's next block starts, in monotonic beats.
    double nextBeat() const {
        return session.syncPoint().monotonicBeat;
    }
};

bool same(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b) {
    for (int channel = 0; channel < 2; ++channel)
        if (std::memcmp(a.getReadPointer(channel), b.getReadPointer(channel),
                        kBlockSize * sizeof(float)) != 0)
            return false;
    return true;
}

/// The first sample of @p channel that sounds, counted from the first callback, if any did.
struct FirstSound {
    std::optional<int> at;
    void note(const juce::AudioBuffer<float>& out, int channel, int callback) {
        for (int i = 0; i < kBlockSize && !at; ++i)
            if (out.getSample(channel, i) != 0.0f)
                at = (callback * kBlockSize) + i;
    }
};

}  // namespace

TEST_CASE("A session track rendered ahead plays its launches as the callback does",
          "[engine][session][3316]") {
    // A launch, the follow action it leads to and a stop, each asked well before its beat.
    Rig whole(0), ahead(kDepth);
    const SlotKey first{1, 0}, second{1, 1};
    juce::AudioBuffer<float> a(2, kBlockSize), b(2, kBlockSize);
    bool sounded = false, followed = false;
    for (int callback = 0; callback < 2400; ++callback) {
        if (callback == 10)
            for (auto* rig : {&whole, &ahead})
                rig->ask([&](auto& gesture) { gesture.play(first, 1.0, 1.0); });
        if (callback == 1800)
            for (auto* rig : {&whole, &ahead})
                rig->ask([&](auto& gesture) { gesture.backToArrangement(second, 6.0, 1.0); });

        whole.callback(a);
        ahead.callback(b);
        INFO("callback " << callback);
        REQUIRE(same(a, b));
        sounded = sounded || a.getMagnitude(0, 0, kBlockSize) > 0.0f;
        followed = followed || a.getSample(0, 0) > 10.0f;
    }

    CHECK(sounded);
    CHECK(followed);
    // The follow action took the track to the second slot, and the stop took it back.
    CHECK(whole.session.launchTap(second)->read().playing == false);
    CHECK(ahead.renderedAhead > 2300);
    CHECK(ahead.session.renderAheadMisses() == 0);
}

TEST_CASE("A launch asked too late for the ahead side lands on the next beat of its grid",
          "[engine][session][3316]") {
    // The ahead side has rendered past the beat asked for, so both of its handles take the next
    // one; asked for that beat outright, the callback plays the same.
    Rig whole(0), ahead(kDepth);
    const SlotKey first{1, 0};
    juce::AudioBuffer<float> a(2, kBlockSize), b(2, kBlockSize);
    for (int callback = 0; callback < 1200; ++callback) {
        if (callback == 200) {
            const auto beat = ahead.nextBeat() + 0.002;
            ahead.ask([&](auto& gesture) { gesture.play(first, beat, 0.25); });
            whole.ask([&](auto& gesture) { gesture.play(first, beat + 0.25, 0.25); });
        }
        whole.callback(a);
        ahead.callback(b);
        INFO("callback " << callback);
        REQUIRE(same(a, b));
    }
    CHECK(a.getMagnitude(0, 0, kBlockSize) > 0.0f);
}

TEST_CASE("An unquantized launch rendered ahead sounds within the look-ahead, where the UI says",
          "[engine][session][3316]") {
    Rig ahead(kDepth);
    const SlotKey first{1, 0};
    constexpr int kAsked = 100;
    const auto perSample = 2.0 / 44100.0;

    juce::AudioBuffer<float> out(2, kBlockSize);
    FirstSound sound;
    for (int callback = 0; callback < 400; ++callback) {
        if (callback == kAsked)
            ahead.ask([&](auto& gesture) { gesture.play(first); });
        ahead.callback(out);
        sound.note(out, 0, callback);

        // The real handle the tap reads, to its 1/4096 of a beat, has covered as much of the run
        // as the copy played.
        if (sound.at && callback > kAsked + kDepth + 2) {
            INFO("callback " << callback);
            const auto heard = static_cast<double>(out.getSample(0, kBlockSize - 1)) - 1.0;
            CHECK(ahead.session.launchTap(first)->read().elapsedBeats ==
                  Catch::Approx(heard + perSample).margin(3e-4));
        }
    }

    REQUIRE(sound.at.has_value());
    CHECK(*sound.at > kAsked * kBlockSize);
    CHECK(*sound.at <= (kAsked + kDepth + 2) * kBlockSize);
}

TEST_CASE("A launch from stopped starts with the transport, on its first sample",
          "[engine][session][3316]") {
    // Not moved past the look-ahead: the roll's first block renders at the callback, which then
    // seeds the copies the ahead side plays on from.
    Rig whole(0, {1}, {}, false), ahead(kDepth, {1}, {}, false);
    juce::AudioBuffer<float> a(2, kBlockSize), b(2, kBlockSize);
    FirstSound sound;
    for (int callback = 0; callback < 600; ++callback) {
        if (callback == 40)
            for (auto* rig : {&whole, &ahead}) {
                rig->ask([](auto& gesture) { gesture.play(SlotKey{1, 0}); });
                rig->session.publishTransport(rolling());
            }
        whole.callback(a);
        ahead.callback(b);
        sound.note(b, 0, callback);
        INFO("callback " << callback);
        REQUIRE(same(a, b));
    }
    CHECK(sound.at == 40 * kBlockSize);
    CHECK(ahead.renderedAhead > 500);
}

TEST_CASE("A scene asked as soon as possible starts its tracks together across the sides",
          "[engine][session][3316]") {
    // Track 2 renders at the callback and track 1 ahead; the scene waits for the ahead one.
    Rig ahead(kDepth, {1, 2}, {2});
    juce::AudioBuffer<float> out(2, kBlockSize);
    FirstSound left, right;
    for (int callback = 0; callback < 200; ++callback) {
        if (callback == 50)
            ahead.ask([&](auto& gesture) {
                gesture.playScene(SlotKey{1, 0}, std::vector<SlotKey>{{2, 0}});
            });
        ahead.callback(out);
        left.note(out, 0, callback);
        right.note(out, 1, callback);
    }
    REQUIRE(left.at.has_value());
    CHECK(left.at == right.at);
}

TEST_CASE("A slot added under a track rendered ahead joins its copies, the playing one carrying on",
          "[engine][session][3316]") {
    // No new plan: the copies take the new table between two of the ahead side's rounds, and the
    // slot already playing keeps the state the ahead side had moved it to.
    Rig whole(0), ahead(kDepth);
    auto withThird = std::make_shared<ClipSnapshot>(*twoSlots({1}));
    SessionSlotPlayback third;
    third.sceneIndex = 2;
    third.lengthBeats = 1.0;
    third.loopBeats = 1.0;
    third.audio.emplace_back();
    withThird->tracks.front().session.push_back(std::move(third));

    juce::AudioBuffer<float> a(2, kBlockSize), b(2, kBlockSize);
    for (int callback = 0; callback < 1400; ++callback) {
        for (auto* rig : {&whole, &ahead}) {
            if (callback == 10)
                rig->ask([](auto& gesture) { gesture.play(SlotKey{1, 0}, 1.0, 1.0); });
            if (callback == 600)
                rig->session.publishClips(withThird);
            if (callback == 700)
                rig->ask([](auto& gesture) { gesture.play(SlotKey{1, 2}, 3.0, 1.0); });
        }
        whole.callback(a);
        ahead.callback(b);
        INFO("callback " << callback);
        REQUIRE(same(a, b));
    }
    CHECK(a.getSample(0, 0) > 20.0f);
    CHECK(ahead.renderedAhead > 1300);
}

TEST_CASE("A launch moves only when the ahead side cannot hear it on its beat",
          "[engine][launch][3316]") {
    LaunchRequest request{.key = {1, 0}, .position = 4.0, .gridBeats = 1.0};
    CHECK(aheadPosition(request, 3.5) == 4.0);
    CHECK(aheadPosition(request, 4.0) == 4.0);
    CHECK(aheadPosition(request, 4.2) == 5.0);
    CHECK(aheadPosition(request, 6.5) == 7.0);

    request.gridBeats = 0.0;
    CHECK(aheadPosition(request, 4.2) == 4.2);
    request.position.reset();
    CHECK(aheadPosition(request, 4.2) == 4.2);

    request.kind = LaunchRequest::Kind::loop;
    request.position = 2.0;
    CHECK(aheadPosition(request, 4.2) == 2.0);
}
