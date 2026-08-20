#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/audio/automation/ModulationBaker.hpp"
#include "../magda/daw/core/AutomationCommands.hpp"
#include "../magda/daw/core/AutomationManager.hpp"
#include "../magda/daw/core/LinkModeManager.hpp"
#include "../magda/daw/core/TrackManager.hpp"
#include "../magda/daw/core/UndoManager.hpp"

using namespace magda;
using Catch::Approx;

namespace {

/** Constant-tempo map: beat <-> seconds at a fixed bpm. */
class FixedTempoMap : public TempoMap {
  public:
    explicit FixedTempoMap(double bpm) : bpm_(bpm) {}

    double beatToTime(double beat) const override {
        return beat * 60.0 / bpm_;
    }
    double timeToBeat(double seconds) const override {
        return seconds * bpm_ / 60.0;
    }
    double bpmAt(double) const override {
        return bpm_;
    }

  private:
    double bpm_;
};

ModInfo makeLFO(LFOWaveform waveform, SyncDivision division) {
    ModInfo mod;
    mod.type = ModType::LFO;
    mod.enabled = true;
    mod.tempoSync = true;
    mod.syncDivision = division;
    mod.waveform = waveform;
    return mod;
}

ModulationBaker::Source makeSource(const ModInfo& mod, float amount, bool bipolar = false) {
    ModulationBaker::Source source;
    source.mod = mod;
    source.link.target = ControlTarget::trackVolume(1);
    source.link.amount = amount;
    source.link.bipolar = bipolar;
    source.link.enabled = true;
    return source;
}

double valueAtBeat(const std::vector<AutomationPoint>& points, double beat) {
    // Linear interpolation over the baked points, mirroring lane playback.
    REQUIRE(points.size() >= 2);
    for (size_t i = 1; i < points.size(); ++i) {
        if (points[i].beatPosition >= beat) {
            const auto& a = points[i - 1];
            const auto& b = points[i];
            const double span = b.beatPosition - a.beatPosition;
            const double t = span > 0.0 ? (beat - a.beatPosition) / span : 0.0;
            return a.value + t * (b.value - a.value);
        }
    }
    return points.back().value;
}

}  // namespace

TEST_CASE("ModulationBaker - bakeability by mod type", "[automation][bake]") {
    ModInfo mod;
    mod.type = ModType::LFO;
    REQUIRE(ModulationBaker::isBakeable(mod));
    mod.type = ModType::Envelope;
    REQUIRE_FALSE(ModulationBaker::isBakeable(mod));
    mod.type = ModType::Random;
    REQUIRE_FALSE(ModulationBaker::isBakeable(mod));
    mod.setType(ModType::Follower);
    REQUIRE_FALSE(ModulationBaker::isBakeable(mod));
}

TEST_CASE("ModulationBaker - beats per cycle", "[automation][bake]") {
    REQUIRE(ModulationBaker::beatsPerCycle(SyncDivision::Quarter) == 1.0);
    REQUIRE(ModulationBaker::beatsPerCycle(SyncDivision::Whole) == 4.0);
    REQUIRE(ModulationBaker::beatsPerCycle(SyncDivision::TwoBars) == 8.0);
    REQUIRE(ModulationBaker::beatsPerCycle(SyncDivision::Sixteenth) == 0.25);
    REQUIRE(ModulationBaker::beatsPerCycle(SyncDivision::DottedQuarter) == 1.5);
    REQUIRE(ModulationBaker::beatsPerCycle(SyncDivision::TripletEighth) == Approx(1.0 / 3.0));
}

TEST_CASE("ModulationBaker - tempo-synced phase is locked to beats", "[automation][bake]") {
    FixedTempoMap tempo(120.0);
    auto mod = makeLFO(LFOWaveform::Saw, SyncDivision::Quarter);

    REQUIRE(ModulationBaker::phaseAtBeat(mod, 0.0, tempo) == Approx(0.0));
    REQUIRE(ModulationBaker::phaseAtBeat(mod, 0.25, tempo) == Approx(0.25));
    REQUIRE(ModulationBaker::phaseAtBeat(mod, 3.75, tempo) == Approx(0.75));

    mod.syncDivision = SyncDivision::Whole;  // 4 beats per cycle
    REQUIRE(ModulationBaker::phaseAtBeat(mod, 2.0, tempo) == Approx(0.5));

    mod.phaseOffset = 0.25f;
    REQUIRE(ModulationBaker::phaseAtBeat(mod, 2.0, tempo) == Approx(0.75));
    REQUIRE(ModulationBaker::phaseAtBeat(mod, 4.0, tempo) == Approx(0.25));
}

TEST_CASE("ModulationBaker - Hz phase goes through the tempo map", "[automation][bake]") {
    FixedTempoMap tempo(120.0);  // 1 beat = 0.5 s
    ModInfo mod = makeLFO(LFOWaveform::Saw, SyncDivision::Quarter);
    mod.tempoSync = false;
    mod.rate = 1.0f;  // 1 Hz -> 1 cycle every 2 beats at 120 bpm

    REQUIRE(ModulationBaker::phaseAtBeat(mod, 0.0, tempo) == Approx(0.0));
    REQUIRE(ModulationBaker::phaseAtBeat(mod, 1.0, tempo) == Approx(0.5));
    REQUIRE(ModulationBaker::phaseAtBeat(mod, 3.0, tempo) == Approx(0.5));
}

TEST_CASE("ModulationBaker - contribution math", "[automation][bake]") {
    FixedTempoMap tempo(120.0);
    // Saw over one beat: value == phase, easy to reason about.
    auto mod = makeLFO(LFOWaveform::Saw, SyncDivision::Quarter);

    SECTION("unipolar: contribution = value * amount") {
        std::vector<ModulationBaker::Source> sources{makeSource(mod, 0.5f)};
        REQUIRE(ModulationBaker::contributionAtBeat(sources, 0.5, tempo) == Approx(0.25));
        REQUIRE(ModulationBaker::contributionAtBeat(sources, 0.0, tempo) == Approx(0.0));
    }

    SECTION("bipolar: value 0..1 maps to -amount..+amount") {
        std::vector<ModulationBaker::Source> sources{makeSource(mod, 0.5f, true)};
        REQUIRE(ModulationBaker::contributionAtBeat(sources, 0.0, tempo) == Approx(-0.5));
        REQUIRE(ModulationBaker::contributionAtBeat(sources, 0.5, tempo) == Approx(0.0));
        REQUIRE(ModulationBaker::contributionAtBeat(sources, 0.999999, tempo) ==
                Approx(0.5).margin(0.001));
    }

    SECTION("negative amount inverts") {
        std::vector<ModulationBaker::Source> sources{makeSource(mod, -1.0f)};
        REQUIRE(ModulationBaker::contributionAtBeat(sources, 0.5, tempo) == Approx(-0.5));
    }

    SECTION("invertOutput flips the level curve") {
        auto inverted = mod;
        inverted.invertOutput = true;
        std::vector<ModulationBaker::Source> sources{makeSource(inverted, 1.0f)};
        REQUIRE(ModulationBaker::contributionAtBeat(sources, 0.25, tempo) == Approx(0.75));
    }

    SECTION("disabled mod or link contributes nothing") {
        auto disabledMod = makeSource(mod, 1.0f);
        disabledMod.mod.enabled = false;
        auto disabledLink = makeSource(mod, 1.0f);
        disabledLink.link.enabled = false;
        std::vector<ModulationBaker::Source> sources{disabledMod, disabledLink};
        REQUIRE(ModulationBaker::contributionAtBeat(sources, 0.5, tempo) == Approx(0.0));
    }

    SECTION("multiple links sum") {
        // Saw value at beat 0.5 is 0.5, so each link contributes 0.5 * 0.25.
        std::vector<ModulationBaker::Source> sources{makeSource(mod, 0.25f),
                                                     makeSource(mod, 0.25f)};
        REQUIRE(ModulationBaker::contributionAtBeat(sources, 0.5, tempo) == Approx(0.25));
    }
}

TEST_CASE("ModulationBaker - bake covers the range and clamps", "[automation][bake]") {
    FixedTempoMap tempo(120.0);
    auto mod = makeLFO(LFOWaveform::Sine, SyncDivision::Whole);

    ModulationBaker::Options opts;
    opts.startBeat = 0.0;
    opts.endBeat = 8.0;
    opts.fallbackBaseValue = 0.8;  // sine * 0.5 pushes above 1.0 -> clamp

    auto points = ModulationBaker::bake({makeSource(mod, 0.5f)}, opts, tempo);

    REQUIRE(points.size() >= 4);
    REQUIRE(points.front().beatPosition == Approx(0.0));
    REQUIRE(points.back().beatPosition == Approx(8.0));
    for (const auto& p : points) {
        REQUIRE(p.value >= 0.0);
        REQUIRE(p.value <= 1.0);
        REQUIRE(p.id == INVALID_AUTOMATION_POINT_ID);
        REQUIRE(p.curveType == AutomationCurveType::Linear);
    }
    // Sine peak (phase 0.25 -> beat 1): 0.8 + 1.0 * 0.5 clamps at 1.0.
    REQUIRE(valueAtBeat(points, 1.0) == Approx(1.0).margin(0.01));
    // Sine trough (phase 0.75 -> beat 3): unipolar v = 0 -> base only.
    REQUIRE(valueAtBeat(points, 3.0) == Approx(0.8).margin(0.01));
}

TEST_CASE("ModulationBaker - baked shape matches the analytic waveform", "[automation][bake]") {
    FixedTempoMap tempo(120.0);
    auto mod = makeLFO(LFOWaveform::Triangle, SyncDivision::Whole);

    ModulationBaker::Options opts;
    opts.startBeat = 0.0;
    opts.endBeat = 4.0;
    opts.fallbackBaseValue = 0.0;

    auto points = ModulationBaker::bake({makeSource(mod, 1.0f)}, opts, tempo);

    // Interpolating the simplified points must stay within epsilon-ish of the
    // true triangle at arbitrary probe beats.
    for (double beat = 0.0; beat <= 4.0; beat += 0.173) {
        const double phase = beat / 4.0;
        const double expected = phase < 0.5 ? phase * 2.0 : 2.0 - phase * 2.0;
        REQUIRE(valueAtBeat(points, beat) == Approx(expected).margin(0.02));
    }
    // And the simplifier must have actually thinned the dense samples.
    REQUIRE(points.size() < 40);
}

TEST_CASE("ModulationBaker - base automation is preserved under the modulation",
          "[automation][bake]") {
    FixedTempoMap tempo(120.0);
    auto mod = makeLFO(LFOWaveform::Square, SyncDivision::Whole);

    ModulationBaker::Options opts;
    opts.startBeat = 0.0;
    opts.endBeat = 4.0;

    // Base ramps 0 -> 0.5 over 4 beats; square adds 0.25 in its high half.
    auto baseRamp = [](double beat) { return beat / 8.0; };
    auto points = ModulationBaker::bake({makeSource(mod, 0.25f)}, opts, tempo, baseRamp);

    REQUIRE(valueAtBeat(points, 1.0) == Approx(1.0 / 8.0 + 0.25).margin(0.02));
    REQUIRE(valueAtBeat(points, 3.0) == Approx(3.0 / 8.0).margin(0.02));
}

TEST_CASE("ModulationBaker - empty or non-bakeable input yields no points", "[automation][bake]") {
    FixedTempoMap tempo(120.0);
    ModulationBaker::Options opts;
    opts.startBeat = 0.0;
    opts.endBeat = 4.0;

    REQUIRE(ModulationBaker::bake({}, opts, tempo).empty());

    auto follower = makeSource(makeLFO(LFOWaveform::Sine, SyncDivision::Quarter), 1.0f);
    follower.mod.setType(ModType::Follower);
    REQUIRE(ModulationBaker::bake({follower}, opts, tempo).empty());

    ModulationBaker::Options degenerate;
    degenerate.startBeat = 4.0;
    degenerate.endBeat = 4.0;
    REQUIRE(
        ModulationBaker::bake({makeSource(makeLFO(LFOWaveform::Sine, SyncDivision::Quarter), 1.0f)},
                              degenerate, tempo)
            .empty());
}

// ============================================================================
// BakeModulationCommand + AutomationManager::replacePointsInRange
// ============================================================================

namespace {

void resetManagers() {
    LinkModeManager::getInstance().exitAllLinkModes();
    AutomationManager::getInstance().clearAll();
    TrackManager::getInstance().clearAllTracks();
    UndoManager::getInstance().clearHistory();
}

AutomationPoint bakedPoint(double beat, double value) {
    AutomationPoint p;
    p.beatPosition = beat;
    p.value = value;
    return p;
}

}  // namespace

TEST_CASE("replacePointsInRange - replaces inside, preserves outside", "[automation][bake]") {
    resetManagers();
    auto& autoMgr = AutomationManager::getInstance();
    auto trackId = TrackManager::getInstance().createTrack("T", TrackType::Audio);
    auto target = ControlTarget::trackVolume(trackId);

    auto laneId = autoMgr.getOrCreateLane(target, AutomationLaneType::Absolute);
    autoMgr.clearLanePoints(laneId);
    autoMgr.addPoint(laneId, 0.0, 0.1);
    autoMgr.addPoint(laneId, 2.0, 0.2);
    autoMgr.addPoint(laneId, 4.0, 0.3);
    autoMgr.addPoint(laneId, 8.0, 0.4);
    const auto before = autoMgr.getLane(laneId)->absolutePoints;

    auto removed = autoMgr.replacePointsInRange(laneId, 2.0, 4.0,
                                                {bakedPoint(2.5, 0.9), bakedPoint(3.5, 0.7)});

    REQUIRE(removed.size() == 2);
    REQUIRE(removed[0].beatPosition == Approx(2.0));
    REQUIRE(removed[1].beatPosition == Approx(4.0));

    const auto& pts = autoMgr.getLane(laneId)->absolutePoints;
    REQUIRE(pts.size() == 4);
    REQUIRE(pts[0].beatPosition == Approx(0.0));
    REQUIRE(pts[1].beatPosition == Approx(2.5));
    REQUIRE(pts[2].beatPosition == Approx(3.5));
    REQUIRE(pts[3].beatPosition == Approx(8.0));

    // Points outside the range kept their ids; new points got fresh ids.
    REQUIRE(pts[0].id == before[0].id);
    REQUIRE(pts[3].id == before[3].id);
    REQUIRE(pts[1].id != INVALID_AUTOMATION_POINT_ID);

    // Restoring the removed points (valid ids) preserves them verbatim.
    autoMgr.replacePointsInRange(laneId, 2.0, 4.0, removed);
    const auto& restored = autoMgr.getLane(laneId)->absolutePoints;
    REQUIRE(restored.size() == 4);
    REQUIRE(restored[1].id == before[1].id);
    REQUIRE(restored[2].id == before[2].id);
    REQUIRE(restored[1].value == Approx(0.2));
}

TEST_CASE("BakeModulationCommand - execute/undo round-trip with link disable",
          "[automation][bake]") {
    resetManagers();
    auto& tracks = TrackManager::getInstance();
    const auto& constTracks = tracks;
    auto& autoMgr = AutomationManager::getInstance();
    auto& undoMgr = UndoManager::getInstance();

    auto trackId = tracks.createTrack("T", TrackType::Audio);
    auto path = ChainNodePath::trackLevel(trackId);
    auto target = ControlTarget::trackVolume(trackId);

    // Track mod 0: an LFO linked to track volume.
    tracks.addMod(path, 0, ModType::LFO);
    tracks.setModTarget(path, 0, target);
    tracks.setModLinkAmount(path, 0, target, 0.5f);

    auto laneId = autoMgr.getOrCreateLane(target, AutomationLaneType::Absolute);
    autoMgr.clearLanePoints(laneId);
    autoMgr.addPoint(laneId, 0.0, 0.1);
    autoMgr.addPoint(laneId, 3.0, 0.3);
    autoMgr.addPoint(laneId, 8.0, 0.5);
    autoMgr.setLaneEnabled(laneId, false);
    const auto before = autoMgr.getLane(laneId)->absolutePoints;

    std::vector<BakeModulationCommand::ModLinkRef> refs;
    refs.push_back({path, 0, target});
    LinkModeManager::getInstance().enterModLinkMode(path, 0);
    REQUIRE(LinkModeManager::getInstance().isInLinkMode());
    undoMgr.executeCommand(std::make_unique<BakeModulationCommand>(
        laneId, 2.0, 4.0, std::vector<AutomationPoint>{bakedPoint(2.0, 0.6), bakedPoint(4.0, 0.8)},
        refs));

    const auto& pts = autoMgr.getLane(laneId)->absolutePoints;
    REQUIRE(pts.size() == 4);  // 0.0 and 8.0 kept, 3.0 replaced by 2.0/4.0
    REQUIRE(pts[1].beatPosition == Approx(2.0));
    REQUIRE(pts[1].value == Approx(0.6));
    REQUIRE(pts[2].beatPosition == Approx(4.0));
    REQUIRE(autoMgr.getLane(laneId)->authorityState == AutomationAuthorityState::Reading);
    REQUIRE_FALSE(LinkModeManager::getInstance().isInLinkMode());

    {
        const auto node = constTracks.resolveChainNode(path);
        REQUIRE(node.valid());
        const auto* link = (*node.mods)[0].getLink(target);
        REQUIRE(link != nullptr);
        REQUIRE_FALSE(link->enabled);
    }

    REQUIRE(undoMgr.undo());

    const auto& restored = autoMgr.getLane(laneId)->absolutePoints;
    REQUIRE(restored.size() == before.size());
    for (size_t i = 0; i < before.size(); ++i) {
        REQUIRE(restored[i].id == before[i].id);
        REQUIRE(restored[i].beatPosition == Approx(before[i].beatPosition));
        REQUIRE(restored[i].value == Approx(before[i].value));
    }
    REQUIRE(autoMgr.getLane(laneId)->authorityState == AutomationAuthorityState::Disabled);
    REQUIRE_FALSE(LinkModeManager::getInstance().isInLinkMode());

    {
        const auto node = constTracks.resolveChainNode(path);
        const auto* link = (*node.mods)[0].getLink(target);
        REQUIRE(link != nullptr);
        REQUIRE(link->enabled);
    }
}

TEST_CASE("BakeModulationToClipCommand - activates the lane and restores disabled state on undo",
          "[automation][bake]") {
    resetManagers();
    auto& tracks = TrackManager::getInstance();
    const auto& constTracks = tracks;
    auto& autoMgr = AutomationManager::getInstance();
    auto& undoMgr = UndoManager::getInstance();

    const auto trackId = tracks.createTrack("T", TrackType::Audio);
    const auto path = ChainNodePath::trackLevel(trackId);
    const auto target = ControlTarget::trackVolume(trackId);

    tracks.addMod(path, 0, ModType::LFO);
    tracks.setModTarget(path, 0, target);

    const auto laneId = autoMgr.getOrCreateLane(target, AutomationLaneType::ClipBased);
    autoMgr.setLaneEnabled(laneId, false);

    const std::vector<BakeModulationCommand::ModLinkRef> refs{{path, 0, target}};
    LinkModeManager::getInstance().enterModLinkMode(path, 0);
    undoMgr.executeCommand(std::make_unique<BakeModulationToClipCommand>(
        laneId, 2.0, 6.0,
        std::vector<AutomationPoint>{bakedPoint(2.0, 0.25), bakedPoint(6.0, 0.75)}, refs));

    const auto* lane = autoMgr.getLane(laneId);
    REQUIRE(lane != nullptr);
    REQUIRE(lane->authorityState == AutomationAuthorityState::Reading);
    REQUIRE(lane->clipIds.size() == 1);
    REQUIRE_FALSE(LinkModeManager::getInstance().isInLinkMode());

    const auto* clip = autoMgr.getClip(lane->clipIds.front());
    REQUIRE(clip != nullptr);
    REQUIRE(clip->points.size() == 2);
    REQUIRE(clip->points.front().beatPosition == Approx(0.0));
    REQUIRE(clip->points.back().beatPosition == Approx(4.0));

    {
        const auto node = constTracks.resolveChainNode(path);
        REQUIRE(node.valid());
        const auto* link = (*node.mods)[0].getLink(target);
        REQUIRE(link != nullptr);
        REQUIRE_FALSE(link->enabled);
    }

    REQUIRE(undoMgr.undo());
    REQUIRE(autoMgr.getLane(laneId)->authorityState == AutomationAuthorityState::Disabled);
    REQUIRE(autoMgr.getLane(laneId)->clipIds.empty());

    const auto node = constTracks.resolveChainNode(path);
    const auto* link = (*node.mods)[0].getLink(target);
    REQUIRE(link != nullptr);
    REQUIRE(link->enabled);

    REQUIRE(undoMgr.redo());
    REQUIRE(autoMgr.getLane(laneId)->authorityState == AutomationAuthorityState::Reading);
    REQUIRE(autoMgr.getLane(laneId)->clipIds.size() == 1);
    REQUIRE_FALSE(LinkModeManager::getInstance().isInLinkMode());
    const auto redoneNode = constTracks.resolveChainNode(path);
    REQUIRE(redoneNode.valid());
    const auto* redoneLink = (*redoneNode.mods)[0].getLink(target);
    REQUIRE(redoneLink != nullptr);
    REQUIRE_FALSE(redoneLink->enabled);
}

TEST_CASE("BakeModulationCommand - new lane is part of the bake transition", "[automation][bake]") {
    resetManagers();
    auto& tracks = TrackManager::getInstance();
    const auto& constTracks = tracks;
    auto& autoMgr = AutomationManager::getInstance();
    auto& undoMgr = UndoManager::getInstance();

    const auto trackId = tracks.createTrack("T", TrackType::Audio);
    const auto path = ChainNodePath::trackLevel(trackId);
    const auto target = ControlTarget::trackVolume(trackId);
    tracks.addMod(path, 0, ModType::LFO);
    tracks.setModTarget(path, 0, target);

    auto previousLaneState = captureBakeAutomationLaneState(target);
    REQUIRE(previousLaneState.captured);
    REQUIRE_FALSE(previousLaneState.laneExisted);

    const auto laneId = autoMgr.getOrCreateLane(target, AutomationLaneType::Absolute);
    autoMgr.setTargetUserTouched(target, true);
    autoMgr.beginTargetGesture(target);

    const std::vector<BakeModulationCommand::ModLinkRef> refs{{path, 0, target}};
    undoMgr.executeCommand(std::make_unique<BakeModulationCommand>(
        laneId, 0.0, 4.0,
        std::vector<AutomationPoint>{bakedPoint(0.0, 0.25), bakedPoint(4.0, 0.75)}, refs,
        std::move(previousLaneState)));

    const auto* bakedLane = autoMgr.getLane(laneId);
    REQUIRE(bakedLane != nullptr);
    REQUIRE(bakedLane->authorityState == AutomationAuthorityState::Reading);
    REQUIRE_FALSE(autoMgr.isTargetUserTouched(target));

    REQUIRE(undoMgr.undo());
    REQUIRE(autoMgr.getLaneForTarget(target) == INVALID_AUTOMATION_LANE_ID);
    const auto undoNode = constTracks.resolveChainNode(path);
    REQUIRE(undoNode.valid());
    REQUIRE((*undoNode.mods)[0].getLink(target)->enabled);

    REQUIRE(undoMgr.redo());
    const auto* redoneLane = autoMgr.getLane(laneId);
    REQUIRE(redoneLane != nullptr);
    REQUIRE(redoneLane->authorityState == AutomationAuthorityState::Reading);
    const auto redoNode = constTracks.resolveChainNode(path);
    REQUIRE(redoNode.valid());
    REQUIRE_FALSE((*redoNode.mods)[0].getLink(target)->enabled);
}

TEST_CASE("BakeModulationToClipCommand - undo restores lane preparation state",
          "[automation][bake]") {
    resetManagers();
    auto& tracks = TrackManager::getInstance();
    auto& autoMgr = AutomationManager::getInstance();
    auto& undoMgr = UndoManager::getInstance();

    const auto trackId = tracks.createTrack("T", TrackType::Audio);
    const auto path = ChainNodePath::trackLevel(trackId);
    const auto target = ControlTarget::trackVolume(trackId);
    tracks.addMod(path, 0, ModType::LFO);
    tracks.setModTarget(path, 0, target);

    const auto laneId = autoMgr.getOrCreateLane(target, AutomationLaneType::Absolute);
    autoMgr.clearLanePoints(laneId);
    autoMgr.setLaneVisible(laneId, false);
    autoMgr.setLaneEnabled(laneId, false);
    auto previousLaneState = captureBakeAutomationLaneState(target);

    REQUIRE(autoMgr.retypeEmptyLane(laneId, AutomationLaneType::ClipBased));
    autoMgr.setLaneVisible(laneId, true);

    const std::vector<BakeModulationCommand::ModLinkRef> refs{{path, 0, target}};
    undoMgr.executeCommand(std::make_unique<BakeModulationToClipCommand>(
        laneId, 2.0, 6.0, std::vector<AutomationPoint>{bakedPoint(2.0, 0.2), bakedPoint(6.0, 0.8)},
        refs, 0.0, std::move(previousLaneState)));

    const auto* bakedLane = autoMgr.getLane(laneId);
    REQUIRE(bakedLane != nullptr);
    REQUIRE(bakedLane->isClipBased());
    REQUIRE(bakedLane->visible);
    REQUIRE(bakedLane->authorityState == AutomationAuthorityState::Reading);
    REQUIRE(bakedLane->clipIds.size() == 1);

    REQUIRE(undoMgr.undo());
    const auto* restoredLane = autoMgr.getLane(laneId);
    REQUIRE(restoredLane != nullptr);
    REQUIRE(restoredLane->isAbsolute());
    REQUIRE(restoredLane->absolutePoints.empty());
    REQUIRE_FALSE(restoredLane->visible);
    REQUIRE(restoredLane->authorityState == AutomationAuthorityState::Disabled);
    REQUIRE(restoredLane->clipIds.empty());

    REQUIRE(undoMgr.redo());
    const auto* redoneLane = autoMgr.getLane(laneId);
    REQUIRE(redoneLane != nullptr);
    REQUIRE(redoneLane->isClipBased());
    REQUIRE(redoneLane->visible);
    REQUIRE(redoneLane->authorityState == AutomationAuthorityState::Reading);
    REQUIRE(redoneLane->clipIds.size() == 1);
}

TEST_CASE("BakeModulationCommand - undo does not re-enable links that were already disabled",
          "[automation][bake]") {
    resetManagers();
    auto& tracks = TrackManager::getInstance();
    const auto& constTracks = tracks;
    auto& autoMgr = AutomationManager::getInstance();
    auto& undoMgr = UndoManager::getInstance();

    auto trackId = tracks.createTrack("T", TrackType::Audio);
    auto path = ChainNodePath::trackLevel(trackId);
    auto target = ControlTarget::trackVolume(trackId);

    tracks.addMod(path, 0, ModType::LFO);
    tracks.setModTarget(path, 0, target);
    tracks.setModLinkEnabled(path, 0, target, false);  // user had it off already

    auto laneId = autoMgr.getOrCreateLane(target, AutomationLaneType::Absolute);
    std::vector<BakeModulationCommand::ModLinkRef> refs;
    refs.push_back({path, 0, target});
    undoMgr.executeCommand(std::make_unique<BakeModulationCommand>(
        laneId, 0.0, 4.0, std::vector<AutomationPoint>{bakedPoint(1.0, 0.5)}, refs));
    REQUIRE(undoMgr.undo());

    const auto node = constTracks.resolveChainNode(path);
    const auto* link = (*node.mods)[0].getLink(target);
    REQUIRE(link != nullptr);
    REQUIRE_FALSE(link->enabled);
}
