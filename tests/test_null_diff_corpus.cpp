#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <set>

#include "NullDiffCase.hpp"
#include "plan/PlanCompiler.hpp"

/**
 * The corpus's own declarations (#2040).
 *
 * The runner asserts what each case claims. What nothing else asserts is that
 * the claims are honest, and one of them in particular: a case may raise its
 * floor or ask for a shift only with a mechanism written beside it. Without
 * this, the way to make a failing case pass is to widen its bound and say
 * nothing, which is the failure mode the whole slice is arranged against.
 *
 * These run in the model-only target because the corpus is model values. What
 * the two engines make of them is the runner's business.
 */

using namespace magda;
using namespace magda::nulldiff;

namespace {

juce::File scratch() {
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("magda_null_diff_corpus_test");
    root.createDirectory();
    return root;
}

}  // namespace

TEST_CASE("The corpus builds and covers what the slice claims", "[nulldiff][corpus]") {
    const auto corpus = sharedCorpus(scratch());

    REQUIRE(corpus.size() == 38);

    std::set<std::string> names;
    for (const auto& value : corpus)
        CHECK(names.insert(value.name).second);

    // The coverage the issue asks for, case by case. Named rather than counted,
    // so that removing one is a decision rather than an accident.
    for (const auto* expected : {"placement.grid",
                                 "placement.trims",
                                 "fades.curves",
                                 "fades.speedramp",
                                 "loop.tiling",
                                 "rate.48k",
                                 "reverse.plain",
                                 "speed.ratio",
                                 "pitch.analog",
                                 "tempo.auto",
                                 "stretch.signalsmith",
                                 "stretch.soundtouch.normal",
                                 "stretch.soundtouch.better",
                                 "stretch.broadband",
                                 "warp.audio",
                                 "takes.comp",
                                 "mix.summing",
                                 "mix.volume",
                                 "mix.volume.silent",
                                 "mix.pan",
                                 "mix.mute",
                                 "mix.solo",
                                 "mix.master",
                                 "mix.volume.clamp",
                                 "param.base",
                                 "param.automation",
                                 "param.modifier",
                                 "param.both",
                                 "param.hostwrite.automation",
                                 "param.hostwrite.modifier",
                                 "macro.track",
                                 "macro.device",
                                 "project.mixed",
                                 "midi.notes",
                                 "midi.cc",
                                 "midi.mpe",
                                 "midi.fold",
                                 "midi.offset"})
        CHECK(names.count(expected) == 1);
}

TEST_CASE("Every allowance carries a mechanism", "[nulldiff][corpus]") {
    // The rule the corpus lives by: change the shape of the comparison, never
    // the size of the allowance. An allowance with no mechanism beside it is
    // how a real bug ends up inside an expected difference.
    for (const auto& value : sharedCorpus(scratch())) {
        INFO(value.name);

        // What counts as an allowance is something the corpus lets the two
        // engines differ by: a shift applied before comparing, a floor raised
        // above the arithmetic one, a case that measures instead of asserting,
        // or notes expected to land offset.
        //
        // A None tier on its own is not one of those. It says where the case is
        // judged rather than how much it may differ by, and it is not a choice
        // either: a MIDI track here carries a capture device instead of a
        // synth, so neither engine produces any audio to compare.
        const auto allows = value.tier == AudioTier::Spectral || value.tier == AudioTier::Aligned ||
                            value.tier == AudioTier::Measured ||
                            value.tier == AudioTier::Invariants || value.floorDb > -120.0 ||
                            value.declaredFractionalShiftSamples != 0.0 ||
                            value.declaredMidiShiftBeats != 0.0 ||
                            // A raised block-size epsilon is the same kind of
                            // thing, against the engine's own second render
                            // rather than against the incumbent (#2078). The
                            // gate itself refuses one on a project with no
                            // plugin to pin it on; this is the other half, so
                            // that a project which does host one still has to
                            // say what about it frames its own work.
                            //
                            // Asked as "has it moved off the default" rather
                            // than as "is it finite": positive infinity is not
                            // finite and admits every residual, so a finiteness
                            // test would wave through the one value that
                            // disables the gate outright.
                            !value.demandsBitIdenticalBlocks();

        if (allows)
            CHECK_FALSE(value.mechanism.empty());
        else
            CHECK(value.floorDb <= -120.0);
    }
}

TEST_CASE("A tier without the figure it needs is refused", "[nulldiff][corpus]") {
    // Two tiers cannot be judged from the render alone: Aligned undoes an offset
    // the case declares, and Invariants has no residual to fall back on, so it
    // needs a discontinuity bound. Declared as a corpus rule rather than only as
    // a runner branch, because the runner's branch first executes on the day
    // somebody writes such a case, which is exactly the wrong day to discover
    // that the figure is missing.
    for (const auto& value : sharedCorpus(scratch())) {
        INFO(value.name);

        if (value.tier == AudioTier::Aligned)
            CHECK(value.declaredFractionalShiftSamples != 0.0);

        if (value.tier == AudioTier::Invariants)
            CHECK(value.maxStepPerSample > 0.0);
    }
}

TEST_CASE("Every case says what it covers and what it plays", "[nulldiff][corpus]") {
    for (const auto& value : sharedCorpus(scratch())) {
        INFO(value.name);

        CHECK_FALSE(value.covers.empty());
        CHECK(value.endBeat > value.startBeat);
        CHECK(value.sampleRate > 0.0);
        CHECK_FALSE(value.clips.empty());
        CHECK(value.master.id == MASTER_TRACK_ID);
        REQUIRE_FALSE(value.tracks.empty());

        // Every clip lands on a track the case declares. A clip pointing at a
        // track that is not there would be dropped by the snapshot and skipped
        // by the sync, so both legs would render the same silence and the case
        // would pass by covering nothing.
        for (const auto& clip : value.clips) {
            const auto known =
                std::any_of(value.tracks.begin(), value.tracks.end(),
                            [&](const TrackInfo& track) { return track.id == clip.trackId; });
            CHECK(known);
        }

        // Every audio clip's source was written and pooled, and every case that
        // captures MIDI has something to capture through: a plan compiles no
        // ClipMidi op for a track whose chain consumes no MIDI, so a MIDI case
        // without a device would compare two silences and pass.
        auto audioClips = 0;
        for (const auto& clip : value.clips)
            if (clip.isAudio())
                ++audioClips;

        if (audioClips > 0)
            CHECK_FALSE(value.sources.empty());

        for (const auto& source : value.sources) {
            CHECK(source.id != INVALID_SOURCE_ID);
            CHECK(juce::File(source.path).existsAsFile());
            CHECK(source.sampleRate > 0.0);
            CHECK(source.durationSeconds > 0.0);
        }

        // Any track, not the first one. Both legs put a capture on every track
        // whose chain consumes MIDI, so an instrument sitting behind an audio
        // track is a project they handle; a check that looked only at the front
        // would fail it here before either leg got the chance to render it.
        //
        // Asked through the compiler's own predicate for the same reason the
        // incumbent leg asks through it: what consumes MIDI has one definition.
        if (value.capturesMidi())
            CHECK(std::any_of(value.tracks.begin(), value.tracks.end(), [](const TrackInfo& track) {
                return magda::engine::chainConsumesMidi(track);
            }));
    }
}

TEST_CASE("Every lane and every link names something the project has", "[nulldiff][corpus]") {
    // The way a parameter case asserts nothing is for its target to miss: the
    // table drops the lane or the link with a diagnostic, both legs render the
    // same unmodulated audio, and the case passes having compared nothing.
    //
    // The runner catches that when the corpus is rendered. This catches it in
    // the model-only target, in a second, without an Edit.
    for (const auto& value : sharedCorpus(scratch())) {
        INFO(value.name);

        // Matched on the whole path, not the device id in it: an id is unique
        // within a chain segment and not across the hierarchy (#1899), so a
        // post-FX target would otherwise be satisfied by the FX device with the
        // same number.
        const auto namesADeviceParameter = [&](const ControlTarget& target) {
            if (target.kind != ControlTarget::Kind::PluginParam)
                return true;  // Not this rule's business; the table reports it.

            const auto declares = [&](const DeviceInfo& device, const ChainNodePath& path) {
                if (path != target.devicePath)
                    return false;

                for (const auto& parameter : device.parameters)
                    if (parameter.paramIndex == target.paramIndex)
                        return true;

                return false;
            };

            for (const auto& track : value.tracks) {
                for (const auto& element : track.chain.fxChainElements)
                    if (magda::isDevice(element)) {
                        const auto& device = magda::getDevice(element);
                        if (declares(device, ChainNodePath::topLevelDevice(track.id, device.id)))
                            return true;
                    }

                for (const auto& element : track.chain.postFxChainElements)
                    if (declares(element.device,
                                 ChainNodePath::postFxDevice(track.id, element.device.id)))
                        return true;

                for (const auto& element : track.chain.mixerAnalysisElements)
                    if (declares(element.device,
                                 ChainNodePath::mixerAnalysisDevice(track.id, element.device.id)))
                        return true;
            }

            return false;
        };

        for (const auto& lane : value.lanes) {
            // A lane the model is not playing is one the table leaves out, so
            // a case carrying one asserts the base under a name that says
            // automation.
            CHECK(lane.authorityState == AutomationAuthorityState::Reading);
            CHECK(lane.hasData());
            CHECK(namesADeviceParameter(lane.target));
        }

        for (const auto& track : value.tracks) {
            for (const auto& mod : track.mods)
                for (const auto& link : mod.links)
                    if (mod.enabled && link.enabled)
                        CHECK(namesADeviceParameter(link.target));

            for (const auto& macro : track.macros)
                for (const auto& link : macro.links)
                    CHECK(namesADeviceParameter(link.target));

            for (const auto& element : track.chain.fxChainElements) {
                if (!magda::isDevice(element))
                    continue;

                const auto& device = magda::getDevice(element);

                for (const auto& mod : device.mods)
                    for (const auto& link : mod.links)
                        if (mod.enabled && link.enabled)
                            CHECK(namesADeviceParameter(link.target));

                for (const auto& macro : device.macros)
                    for (const auto& link : macro.links)
                        CHECK(namesADeviceParameter(link.target));
            }
        }
    }
}

TEST_CASE("Render cases change tempo in steps rather than ramps", "[nulldiff][corpus]") {
    // A ramped tempo is the one place the two tempo maps are known to be able
    // to disagree, because the engine subdivides where the fork integrates. A
    // render case built on one would report that as a clip bug. Ramps are
    // pinned in the tempo-map comparison, where the answer is a number.
    for (const auto& value : sharedCorpus(scratch())) {
        INFO(value.name);
        REQUIRE_FALSE(value.tempo.empty());
        CHECK(value.tempo.front().beat == 0.0);

        for (std::size_t index = 1; index < value.tempo.size(); ++index)
            CHECK(value.tempo[index].beat > value.tempo[index - 1].beat);
    }
}

TEST_CASE("A grooving case carries the template both engines will read", "[nulldiff][corpus]") {
    // One XML string feeds both legs, which is what makes "the same groove" a
    // fact rather than two parsers agreeing.
    for (const auto& value : sharedCorpus(scratch())) {
        auto namesAGroove = false;
        for (const auto& clip : value.clips)
            if (clip.grooveTemplate.isNotEmpty())
                namesAGroove = true;

        INFO(value.name);
        CHECK(namesAGroove == value.grooveXml.isNotEmpty());

        if (!namesAGroove)
            continue;

        const auto document = juce::parseXML(value.grooveXml);
        REQUIRE(document != nullptr);
        CHECK(document->getChildByName("GROOVETEMPLATE") != nullptr);
        CHECK(document->getChildByName("GROOVETEMPLATE")->getStringAttribute("name") ==
              juce::String(kGrooveName));
    }
}
