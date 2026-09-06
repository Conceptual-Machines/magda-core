#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "MgdFixture.hpp"
#include "NullDiffCompare.hpp"
#include "NullDiffHostedPlugin.hpp"
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
 * The corpus hosts plugins now (#2246), and they landed in the gate rather than
 * widening it: every one of those cases is still held to bit identity, because
 * the plugin they host is the corpus's own and does nothing that depends on how
 * a render was framed (NullDiffHostedPlugin.hpp). The epsilon stays where it
 * was written for -- a real plugin in a real project, which owes nobody a
 * sample -- and the cases that could have claimed it do not.
 *
 * One value is refused rather than judged, in both places it could arrive: an
 * epsilon of positive infinity. It is not finite, so a rule written as "does
 * this case declare a bound" reads it as no bound at all and asks for no
 * mechanism; it is also greater than every residual, so the comparison reads it
 * as a bound and passes anything. Left alone it would disable the declaration
 * rule and the comparison together, each because the other looked like it had
 * the case covered.
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

/// The corpus's own plugins, as an installed scan (NullDiffHostedPlugin.hpp).
///
/// It is what makes a case that hosts one measurable here rather than reported
/// as not gated: they are in the binary, where a real plugin would need the
/// folder walk that turns a fast gate into a slow one. So a real project's
/// plugin is absent by construction, skipped below, and named when it is.
InstalledPlugins installedFrom(HostedScan& scan) {
    return {.formats = &scan.formats, .knownPlugins = &scan.knownPlugins};
}

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

    /// Why this project does not render the same twice, at one block size, in
    /// one process. Empty when it does.
    ///
    /// Asked first and reported apart from the rungs, because it is a different
    /// finding and it makes them meaningless. A project whose render depends on
    /// what the process did before it differs at every block size for a reason
    /// that has nothing to do with block size, and reading that as a block-size
    /// dependence sends whoever picks it up into the block loop looking for a
    /// bug that is not there.
    std::string irreproducible;

    bool held() const {
        return unmeasurable.empty() && irreproducible.empty() &&
               std::all_of(rungs.begin(), rungs.end(), [](const Rung& r) { return r.held; });
    }
};

/// What the engine could not do in @p rendered, beyond what @p value declared.
///
/// The same rule the runner applies, and this file needs it more than the runner
/// does: the runner compares a native render against the incumbent, where a
/// device the engine did not build shows up as a residual. This compares native
/// renders against each other, and a passthrough substituted for a plugin is
/// perfectly block-size invariant. Checking only `failure` would certify exactly
/// that -- a project held to bit identity across every size without the plugin
/// ever having run.
std::string undeclaredDiagnostic(const Case& value, const NativeRender& rendered) {
    auto diagnostics = rendered.diagnostics;

    for (const auto& expected : value.expectedDiagnostics) {
        const auto found =
            std::find_if(diagnostics.begin(), diagnostics.end(), [&](const std::string& reported) {
                return reported.find(expected) != std::string::npos;
            });

        if (found != diagnostics.end())
            diagnostics.erase(found);
    }

    return diagnostics.empty() ? std::string{} : diagnostics.front();
}

GateResult gate(const Case& value) {
    GateResult result;

    HostedScan scan;

    const auto external = externalDevicesIn(value);
    const auto allowed = external.empty() ? kBitIdenticalDb : value.blockSizeEpsilonDb;

    // A bound that is not a number is not a bound. Positive infinity admits
    // every residual there is, and it is the one value that would slip past
    // both halves of the rule at once: a finiteness test reads it as "no bound
    // declared" and lets it by without a mechanism, while the comparison reads
    // it as a bound and passes anything. Refused here by name, so neither half
    // is left assuming the other caught it.
    if (allowed != kBitIdenticalDb && !std::isfinite(allowed)) {
        result.unmeasurable = "the declared block-size epsilon is not a bound";
        return result;
    }

    auto reference = value;
    reference.blockSize = kReferenceBlockSize;
    const auto rendered = renderNative(reference, installedFrom(scan));

    if (!rendered.failure.empty()) {
        result.unmeasurable = "at " + std::to_string(kReferenceBlockSize) + ": " + rendered.failure;
        return result;
    }

    if (const auto refused = undeclaredDiagnostic(value, rendered); !refused.empty()) {
        result.unmeasurable = "at " + std::to_string(kReferenceBlockSize) +
                              ", the engine could not honour the case: " + refused;
        return result;
    }

    // The same project, the same size, the second time. Held to the same bit
    // identity the rungs are, and for a stronger reason: a render that is not a
    // function of the timeline is not a function of anything the rest of this
    // file is asking about.
    {
        const auto again = renderNative(reference, installedFrom(scan));
        if (!again.failure.empty()) {
            result.unmeasurable =
                "at " + std::to_string(kReferenceBlockSize) + ", rendered again: " + again.failure;
            return result;
        }

        if (const auto refused = undeclaredDiagnostic(value, again); !refused.empty()) {
            result.unmeasurable =
                "at " + std::to_string(kReferenceBlockSize) +
                ", rendered again, the engine could not honour the case: " + refused;
            return result;
        }

        AudioCompareOptions options;
        options.sampleRate = value.sampleRate;
        options.floorDb = allowed;

        const auto repeat = compareAudio(rendered.audio, again.audio, options);
        if (repeat.lengthDifference != 0 || !repeat.refusal.empty() || !repeat.nulled())
            result.irreproducible = "rendered twice at " + std::to_string(kReferenceBlockSize) +
                                    " it comes back different: peak " + formatDb(repeat.peakDb) +
                                    ", first difference "
                                    "at sample " +
                                    std::to_string(repeat.firstDivergence);
    }

    for (const auto blockSize : kOtherBlockSizes) {
        auto other = value;
        other.blockSize = blockSize;
        const auto second = renderNative(other, installedFrom(scan));

        if (!second.failure.empty()) {
            result.unmeasurable = "at " + std::to_string(blockSize) + ": " + second.failure;
            return result;
        }

        if (const auto refused = undeclaredDiagnostic(value, second); !refused.empty()) {
            result.unmeasurable = "at " + std::to_string(blockSize) +
                                  ", the engine could not honour the case: " + refused;
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

/// Every project the gate covers: the ones built in code and the ones loaded
/// from a file.
///
/// The real projects belong here for the reason they belong in the corpus
/// (#2081). Block-size invariance is a claim about the engine and not about how
/// a case was written, and a project somebody made is where the claim is worth
/// most: the combinations that break an assumption about block boundaries are
/// the ones nobody thought to write down.
///
/// A fixture that will not load is a failure of the corpus rather than of the
/// claim, and comes back named so the caller can say so instead of quietly
/// gating fewer projects than it thinks.
std::vector<Case> everyProject(std::vector<std::string>& refusals) {
    auto cases = sharedCorpus(scratch());

    for (const auto& fixture : mgdFixtures()) {
        const auto loaded = loadFixture(fixture, scratch().getChildFile("projects"));
        if (!loaded.ok) {
            refusals.push_back(std::string(fixture.declaration.name) + ": " + loaded.failure);
            continue;
        }

        cases.push_back(loaded.value);
    }

    return cases;
}

}  // namespace

TEST_CASE("Every project renders the same audio at 64, 512 and 4096", "[nulldiff][blocksize]") {
    // Held for the whole case: the load drives the app's own source install,
    // which clears the pool the code-built corpus is also holding its sources
    // in. See PooledSourcesUnwind.
    const PooledSourcesUnwind unwind;

    // Named rather than skipped by a predicate, and asserted in both
    // directions, so the list cannot quietly grow and a case that starts
    // holding has to be taken off by somebody. It is the same rule the
    // corpus's calibration list lives by.
    //
    // Empty of code-built cases, and it took four fixes to get there; the file
    // comment says which.
    //
    // Four real projects are on it, and they arrived together with the projects
    // themselves (#2081). What they have in common is the one thing no case
    // above them has: a device MAGDA ships. The code-built corpus contains four
    // devices written for it -- a gain, a mono gain, an impulse synth and a
    // multi-out (NullDiffGain.hpp) -- and, since #2246, a hosted plugin also
    // written for it. So until a real project was gated, this claim had never
    // been asked of a Poly Synth, an FM synth, a Drum Grid, a reverb, a limiter,
    // a Clouds, a sidechain or a Faust device.
    //
    // What was measured, at 64, 96 and 4096 against 512:
    //
    //  - project.fmchain diverges at sample 222, before the first note has
    //    finished its attack, by 5 dB. Chain: FM synth, filter, reverb, Clouds,
    //    limiter.
    //  - project.sidechain diverges at the block boundary before the second
    //    beat -- 22016 at 64, 21984 at 96, 20480 at 4096, each the last
    //    multiple of that size before 22050 -- by 7 to 9 dB.
    //  - project.demo diverges by 2 to 4 dB, and it is the only one whose peak
    //    residual is above the render itself.
    //  - project.faust diverges at 64 and 96 and holds at 4096.
    //
    // No mechanism is claimed for any of them. Each is a number this gate
    // measured and nothing more, which is the same standing the corpus's own
    // calibration list gives a residual without an explanation: a bound nobody
    // has justified is how a real difference ends up inside an expected one.
    // They are named so that neither a new failure nor a fixed one is quiet.
    //
    // plugin.compiled.instrument was the fifth, and the only one that arrived
    // with its mechanism: mydsp_poly returns a released voice to the free pool
    // inside compute() when the RMS of that one call falls under
    // VOICE_STOP_LEVEL (poly-dsp.h), so which voice was free -- and therefore
    // which voice the next note-on was handed, and what phase its oscillators
    // were at -- was a function of how the block was cut. It diverged by 0.6 at
    // 64, first at sample 44100, the second chord's onset. The device now
    // judges that level over a fixed window of the render rather than over
    // whatever the host asked for (#2436), and the case holds at every rung, so
    // it is gated like the rest. Every compiled synth MAGDA ships is one of
    // these, which is why it is worth a case of its own.
    const std::set<std::string> knownDependent{
        "project.demo",
        "project.faust",
        "project.fmchain",
        "project.sidechain",
    };

    // Empty, and it is worth its own list rather than being folded into the one
    // above. A project that does not render the same twice differs at every
    // block size for a reason that has nothing to do with block size, and the
    // four above would have been read as evidence of one. Asked first, and the
    // answer today is that every project in the gate is reproducible: what they
    // depend on is the block size and not the process's history.
    const std::set<std::string> knownIrreproducible{};

    HostedScan scan;

    std::vector<std::string> refusals;
    const auto projects = everyProject(refusals);

    INFO("fixtures that would not load: " << join(refusals));
    REQUIRE(refusals.empty());

    std::set<std::string> failed;
    std::set<std::string> irreproducible;

    /// Projects this binary did not gate, and why. Printed rather than counted:
    /// a gate that quietly covered fewer projects than it thinks is the one
    /// failure it cannot report as one.
    std::set<std::string> notRun;

    for (const auto& value : projects) {
        // A case that asserts nothing about its audio renders none to compare:
        // the four MIDI-only cases carry a device that records what arrives
        // instead of sounding. What block size does to a stream of captured
        // events is the runner's question, asked of both engines, not this
        // one's.
        //
        // Keyed on the tier and not on capturesMidi(), which answers a
        // different question. Comparing MIDI streams and asserting nothing
        // about the audio are independent by construction (#2075), and
        // project.mixed is exactly the case that does both: an instrument track
        // beside audio tracks, at AudioTier::Exact. Skipping on MIDI capture
        // would have dropped the only multi-track mixed project in the corpus
        // out of the gate, which is the last one worth losing.
        if (value.tier == AudioTier::None)
            continue;

        // And a project whose plugin nothing here has (#2175). The scan this
        // file renders through holds the corpus's own plugins and nothing else:
        // they are in the binary, where a real plugin would need the folder walk
        // that turns a fast gate into a slow one. So a real project's plugin is
        // absent by construction and a passthrough would be bound in its place.
        //
        // A passthrough is perfectly block-size invariant, which is what makes
        // this the one place the substitution has to be refused rather than
        // measured: the gate compares native renders against each other, so the
        // missing plugin leaves no residual for anything to notice. The
        // undeclared-diagnostic refusal inside gate() catches it and reports
        // unmeasurable; that is the right answer and the wrong place to hear it,
        // because unmeasurable is a complaint and this is not one.
        //
        // The epsilon these projects would buy (#2078) is not dead code waiting
        // for a use. It is what a machine with the plugins installed measures
        // them within, and it is the bound that stops the difference being
        // absorbed on the day one of them is gated here. The corpus's own
        // hosted cases do not take it and do not need it: they get through this
        // gate on bit identity like everything else (#2246).
        if (const auto absent = absentPluginsIn(value, &scan.knownPlugins); !absent.empty()) {
            notRun.insert(value.name + " (" + join(absent) + ")");
            continue;
        }

        INFO(value.name);

        const auto external = externalDevicesIn(value);

        // The one way the external tier could become a way to make a failing
        // case pass. An epsilon is bought by there being something to attribute
        // the difference to, and a case that raises one over a project of
        // internal devices has nothing to point at.
        if (external.empty())
            CHECK(value.demandsBitIdenticalBlocks());

        // And a raised bound has to be a number. Asserted here as well as
        // refused in the gate, because the two say different things: the gate
        // declines to judge a case it cannot judge, and this says the case
        // should never have been written.
        CHECK((value.demandsBitIdenticalBlocks() || std::isfinite(value.blockSizeEpsilonDb)));

        const auto result = gate(value);

        // Never reported as a residual. See the comment on GateResult.
        INFO(result.unmeasurable);
        REQUIRE(result.unmeasurable.empty());
        REQUIRE(result.rungs.size() == kOtherBlockSizes.size());

        // Before the rungs, because it is what makes them mean anything.
        INFO(result.irreproducible);
        if (knownIrreproducible.count(value.name) == 0)
            CHECK(result.irreproducible.empty());

        if (!result.irreproducible.empty())
            irreproducible.insert(value.name);

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

    if (!notRun.empty()) {
        std::string skipped;
        for (const auto& name : notRun)
            skipped += (skipped.empty() ? "" : ", ") + name;

        WARN("not gated, for want of a plugin no scan here could find: " << skipped);
    }

    CHECK(failed == knownDependent);
    CHECK(irreproducible == knownIrreproducible);
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
        track.type = TrackType::Media;
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

    SECTION("a plugin inside a Drum Grid pad counts") {
        // The same miss one level further in. A pad is a chain of devices, so a
        // kit built out of hosted plugins is a project this would otherwise call
        // internal and hold to bit identity -- and every one of those plugins
        // frames its own work, which is exactly what the epsilon exists for.
        auto value = projectWith(PluginFormat::Internal);

        DeviceInfo grid;
        grid.id = 904;
        grid.name = "Drum Grid";
        grid.deviceType = DeviceType::Instrument;
        grid.isInstrument = true;
        grid.format = PluginFormat::Internal;

        auto pads = std::make_unique<RackInfo>();
        pads->id = 1;
        {
            ChainInfo pad;
            pad.id = 1;
            DeviceInfo sampler;
            sampler.id = 905;
            sampler.name = "Padded";
            sampler.format = PluginFormat::VST3;
            pad.elements.emplace_back(std::move(sampler));
            pads->chains.push_back(std::move(pad));
        }
        grid.pads.reset(std::move(pads));

        value.tracks.front().chain.fxChainElements.emplace_back(std::move(grid));

        const auto external = externalDevicesIn(value);
        REQUIRE(external.size() == 1);
        CHECK(external.front() == "Padded");
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
