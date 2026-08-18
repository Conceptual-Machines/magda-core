#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "NullDiffCompare.hpp"
#include "NullDiffNativeLeg.hpp"

/**
 * The block-size invariance gate (#2078).
 *
 * RenderContext requires that output is a function of timeline position and
 * nothing else. That is the claim the whole offline render rests on, and it is
 * the claim a user cashes in when they bounce a project at a large buffer and
 * expect it to sound like what they heard at a small one. This file is where
 * the claim is held to, permanently, over the corpus rather than over a test
 * device.
 *
 * It has already earned its place, four times, and none of the four were
 * visible from reading the code. Three came out of the first run at two sizes:
 * a rate that varies within a block resolved from that block's own two ends, so
 * a speed ramp, auto tempo across a tempo change and a warped clip approximated
 * their curve differently at every block size; a stretcher that framed whatever
 * sizes it was handed; and a cell reading past its block's end taking its beats
 * from a straight line through that block rather than from the tempo map, so a
 * cell straddling a step tempo change ran at the wrong ratio.
 *
 * The fourth is why 4096 is here. Both SoundTouch modes had been green at 64,
 * 96 and 512 all along, and they came apart at 4096 on the first run that
 * included it: SoundTouchClipStretcher::preRollSamples sized its cushion from
 * the block, so the stretcher was primed with 4096 samples of surplus at 4096
 * where it had 512 at 512. Priming is what sets a phase vocoder's state, so the
 * same clip rendered a decibel apart at the two sizes. A path that behaves at
 * 512 and misbehaves at one end is a path that was tested at one size, which is
 * the argument for the sizes below rather than a happier pair.
 *
 * ## Why these sizes
 *
 * 4096 is the size that carries the guarantee: a large-buffer offline render
 * that sounds like the small-buffer one. 64 is where anything that assumed it
 * had room to work fails. 512 is what the corpus renders at everywhere else and
 * is therefore the reference the other two are compared against. A path that
 * behaves at 512 and misbehaves at both ends is a path that was tested at one
 * size.
 *
 * 96 is the fourth, and it is not a power of two on purpose. The engine feeds a
 * stretcher in 128-sample cells anchored to where a clip's event begins, so at
 * 512 and 4096 every block boundary is also a cell boundary and the holdover
 * buffer, the head-drop and the mid-cell resume never run for a clip that
 * starts at zero. 64 puts a boundary at half a cell, always the same half. 96
 * rotates through every phase the cell grid has, which is where that machinery
 * does the most work and where the invariance claim would otherwise be weakest.
 * It is here because it is what this check ran at before it was a gate, and
 * dropping it to reach three sizes would be trading coverage for a round number.
 *
 * ## What each project owes
 *
 * A project of internal devices owes bit identity. Not a floor, not an epsilon:
 * the same bits, because there is no mechanism by which a deterministic graph
 * fed the same timeline could produce anything else, and a floor would let a
 * real dependence hide under it until it grew.
 *
 * A project hosting an external plugin owes less, because a plugin owes nobody
 * a sample and frames its own work how it likes. Those are compared within the
 * epsilon the case declares, and the plugins are named beside the residual. The
 * naming is the point: the difference is attributed to what can account for it,
 * never absorbed into a wider number. So the epsilon is refused outright on a
 * project with no plugin in it, which is the one way this tier could be turned
 * into a way to make a failing case pass.
 *
 * The corpus hosts no plugin today, so every case here is held to bit identity
 * and the external path is exercised by the two hand-built cases at the bottom.
 * That is deliberate: #1893 brings the first plugin, and it should find a gate
 * to land in rather than a gate to widen.
 *
 * It runs in the model-only target because the native engine needs no Edit, and
 * the incumbent is not party to this claim: Tracktion owes nobody block-size
 * invariance and could not be fixed here if it did.
 */

using namespace magda;
using namespace magda::nulldiff;

namespace {

juce::File scratch() {
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("magda_null_diff_block_size");
    root.createDirectory();
    return root;
}

/// The reference every other size is compared against, and the size the rest of
/// the corpus renders at.
constexpr int kReferenceBlockSize = 512;

/// The rungs, in the order they are rendered. See the file comment.
constexpr std::array<int, 3> kOtherBlockSizes{64, 96, 4096};

/// Bit identity, written as a level so it goes through the same comparator
/// everything else does.
///
/// compareAudio's floor is a linear threshold the peak residual has to stay at
/// or under, and fromDb takes negative infinity to exactly zero. So a floor of
/// -inf makes `nulled()` mean "the peak difference was zero", and
/// `firstDivergence` the first sample that differed at all rather than the first
/// that differed audibly. A second comparator would have been a second thing to
/// keep honest.
constexpr double kBitIdenticalDb = -std::numeric_limits<double>::infinity();

std::string join(const std::vector<std::string>& parts) {
    std::string joined;
    for (const auto& part : parts)
        joined += (joined.empty() ? "" : ", ") + part;
    return joined;
}

/// What the gate found for one case at one size.
struct Rung {
    int blockSize = 0;
    AudioComparison residual;
    bool held = false;
};

/// Render @p value at every size and compare each against the reference.
///
/// Returns nothing for a case the gate cannot judge: a leg that would not
/// render, or renders of different lengths. Neither is reported as a residual,
/// for the reason the runner gives: a number about something other than the
/// claim reads exactly like the claim failing.
struct GateResult {
    std::vector<Rung> rungs;
    std::string unmeasurable;

    bool held() const {
        return unmeasurable.empty() &&
               std::all_of(rungs.begin(), rungs.end(), [](const Rung& r) { return r.held; });
    }
};

GateResult gate(const Case& value) {
    GateResult result;

    const auto external = externalDevicesIn(value);
    const auto allowed = external.empty() ? kBitIdenticalDb : value.blockSizeEpsilonDb;

    auto reference = value;
    reference.blockSize = kReferenceBlockSize;
    const auto rendered = renderNative(reference);

    if (!rendered.failure.empty()) {
        result.unmeasurable = "at " + std::to_string(kReferenceBlockSize) + ": " + rendered.failure;
        return result;
    }

    for (const auto blockSize : kOtherBlockSizes) {
        auto other = value;
        other.blockSize = blockSize;
        const auto second = renderNative(other);

        if (!second.failure.empty()) {
            result.unmeasurable = "at " + std::to_string(blockSize) + ": " + second.failure;
            return result;
        }

        AudioCompareOptions options;
        options.sampleRate = value.sampleRate;
        options.floorDb = allowed;

        Rung rung;
        rung.blockSize = blockSize;
        rung.residual = compareAudio(rendered.audio, second.audio, options);

        // A render that came back a different length is a bug in the engine
        // whatever its samples say, and `nulled()` already refuses it. Kept as
        // its own message because "the peak residual was -inf" beside "the
        // renders differ in length by 4032 samples" is the second thing a reader
        // needs, not the first.
        if (rung.residual.lengthDifference != 0) {
            result.unmeasurable = "at " + std::to_string(blockSize) + ": the renders differ in " +
                                  "length by " + std::to_string(rung.residual.lengthDifference) +
                                  " samples";
            return result;
        }

        if (!rung.residual.refusal.empty()) {
            result.unmeasurable = "at " + std::to_string(blockSize) + ": " + rung.residual.refusal;
            return result;
        }

        rung.held = rung.residual.nulled();
        result.rungs.push_back(std::move(rung));
    }

    return result;
}

}  // namespace

TEST_CASE("Every project renders the same audio at 64, 512 and 4096", "[nulldiff][blocksize]") {
    // Named rather than skipped by a predicate, and asserted in both
    // directions, so the list cannot quietly grow and a case that starts
    // holding has to be taken off by somebody. It is the same rule the
    // corpus's calibration list lives by.
    //
    // Empty, and it took four fixes to get there; the file comment says which.
    const std::set<std::string> knownDependent{};

    std::set<std::string> failed;

    for (const auto& value : sharedCorpus(scratch())) {
        // A MIDI case renders no audio at all: the device standing in for a
        // synth records what arrives instead of sounding. What block size does
        // to a stream of captured events is the runner's question, asked of
        // both engines, not this one's.
        if (value.capturesMidi())
            continue;

        INFO(value.name);

        const auto external = externalDevicesIn(value);

        // The one way the external tier could become a way to make a failing
        // case pass. An epsilon is bought by there being something to attribute
        // the difference to, and a case that raises one over a project of
        // internal devices has nothing to point at.
        if (external.empty())
            CHECK(value.blockSizeEpsilonDb == kBitIdenticalDb);

        const auto result = gate(value);

        // Never reported as a residual. See the comment on GateResult.
        INFO(result.unmeasurable);
        REQUIRE(result.unmeasurable.empty());
        REQUIRE(result.rungs.size() == kOtherBlockSizes.size());

        // Asserted per rung rather than per case, so the size that failed and
        // what it failed by are on the failure rather than in a report the
        // reader has to go and find. "stretch.soundtouch.normal did not hold"
        // is the start of a diagnosis; "at 4096 it is -47 dB away from 512,
        // first at sample 22050" is most of one.
        for (const auto& rung : result.rungs) {
            INFO("block size " << rung.blockSize << " against " << kReferenceBlockSize << ": peak "
                               << formatDb(rung.residual.peakDb) << ", rms "
                               << formatDb(rung.residual.rmsDb) << ", first difference at sample "
                               << rung.residual.firstDivergence);

            // Attributed rather than absorbed. On an internal project this says
            // there is nothing to blame it on, which is itself the finding.
            INFO((external.empty() ? std::string("no external plugin in this project")
                                   : "external plugins: " + join(external)));

            if (knownDependent.count(value.name) == 0)
                CHECK(rung.held);
        }

        if (!result.held())
            failed.insert(value.name);
    }

    CHECK(failed == knownDependent);
}

TEST_CASE("A stretched clip survives a block shorter than a cell", "[nulldiff][blocksize]") {
    // A device at 64 samples still drives whole 128-sample cells through the
    // stretcher, so everything sized against one has to be sized against the
    // cell rather than the block. Sized against the block, SoundTouch fills
    // half a cell and zero-fills the rest, and the reading clamp truncates
    // every cell's read: alternating material and silence, at exactly the block
    // sizes a low-latency interface uses, and no error anywhere.
    //
    // Distinct from the gate above, which would pass on this: two renders that
    // are both holed in the same places at every block size are bit identical
    // and say nothing. What catches it is looking at one render on its own.
    for (const auto& value : sharedCorpus(scratch())) {
        if (value.capturesMidi() || value.name.rfind("stretch", 0) != 0)
            continue;

        INFO(value.name);

        auto small = value;
        small.blockSize = 64;
        const auto rendered = renderNative(small);

        REQUIRE(rendered.failure.empty());
        CHECK(rendered.diagnostics.empty());
        CHECK(rendered.audio.getMagnitude(0, rendered.audio.getNumSamples()) > 0.01f);

        // The failure this exists for is not silence everywhere, it is silence
        // every other cell. So the material is checked in cell-sized pieces
        // across a stretch of the clip rather than as one peak.
        // From a second in, past the priming a stretcher needs before it has
        // anything to give: that opening quiet is the engine working, and
        // counting it here would measure latency rather than the hole this is
        // looking for.
        const auto from = static_cast<int>(value.sampleRate);
        auto quietCells = 0;
        const auto cells = std::min(64, std::max(0, (rendered.audio.getNumSamples() - from) / 128));
        REQUIRE(cells > 0);

        for (auto cell = 0; cell < cells; ++cell) {
            auto peak = 0.0f;
            for (auto sample = 0; sample < 128; ++sample)
                peak = std::max(peak,
                                std::abs(rendered.audio.getSample(0, from + cell * 128 + sample)));
            if (peak < 1.0e-6f)
                ++quietCells;
        }

        INFO(quietCells << " of " << cells << " cells were silent");
        CHECK(quietCells == 0);
    }
}

TEST_CASE("An epsilon is bought by a plugin, not declared", "[nulldiff][blocksize]") {
    // The tier doing its job, on the two projects the corpus cannot supply
    // today. Written against externalDevicesIn rather than against a render,
    // because what is being tested is which of the two bars a project is held
    // to and that is decided before anything is rendered.
    const auto projectWith = [](PluginFormat format) {
        Case value;
        value.name = "block.size.attribution";
        value.covers = "which bar a project is held to";

        TrackInfo track;
        track.id = 1;
        track.type = TrackType::Audio;
        track.name = "Track";
        track.audioOutputDevice = "master";

        DeviceInfo device;
        device.id = 900;
        device.name = "Reverb";
        device.deviceType = DeviceType::Effect;
        device.format = format;
        track.chain.fxChainElements.emplace_back(std::move(device));

        value.tracks.push_back(std::move(track));
        value.master.id = MASTER_TRACK_ID;
        value.master.type = TrackType::Master;
        return value;
    };

    SECTION("an internal project has nobody to attribute a difference to") {
        CHECK(externalDevicesIn(projectWith(PluginFormat::Internal)).empty());
    }

    SECTION("a hosted plugin is named, so the difference can be pinned on it") {
        const auto external = externalDevicesIn(projectWith(PluginFormat::VST3));
        REQUIRE(external.size() == 1);
        CHECK(external.front() == "Reverb");
    }

    SECTION("a plugin inside a rack counts, however deep") {
        // The one way this can be wrong in the direction nobody notices: a rack
        // full of plugins read as an internal project would be held to bit
        // identity, and the failure would name no cause.
        auto value = projectWith(PluginFormat::Internal);

        auto rack = std::make_unique<RackInfo>();
        rack->id = 1;

        auto inner = std::make_unique<RackInfo>();
        inner->id = 2;
        {
            ChainInfo chain;
            chain.id = 2;
            DeviceInfo device;
            device.id = 902;
            device.name = "Nested";
            device.format = PluginFormat::AU;
            chain.elements.emplace_back(std::move(device));
            inner->chains.push_back(std::move(chain));
        }

        {
            ChainInfo chain;
            chain.id = 1;
            chain.elements.emplace_back(std::move(inner));
            rack->chains.push_back(std::move(chain));
        }

        value.tracks.front().chain.fxChainElements.emplace_back(std::move(rack));

        const auto external = externalDevicesIn(value);
        REQUIRE(external.size() == 1);
        CHECK(external.front() == "Nested");
    }

    SECTION("a plugin in the post-FX stage counts too") {
        auto value = projectWith(PluginFormat::Internal);

        PostFxChainElement element;
        element.device.id = 903;
        element.device.name = "Limiter";
        element.device.format = PluginFormat::LV2;
        value.master.chain.postFxChainElements.push_back(std::move(element));

        const auto external = externalDevicesIn(value);
        REQUIRE(external.size() == 1);
        CHECK(external.front() == "Limiter");
    }
}
