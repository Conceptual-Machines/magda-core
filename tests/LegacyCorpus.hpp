#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <map>
#include <vector>

#include "magda/daw/core/RackInfo.hpp"
#include "magda/daw/core/TrackInfo.hpp"

/**
 * The legacy corpus (#2079): real projects and presets saved by released
 * versions of MAGDA, checked in and loaded on every test run.
 *
 * Everything else in the validation harness builds its cases in code (#2040),
 * because a case built in code is reviewable and cannot drift. This corpus is
 * the deliberate exception: the load is the thing under test, so a case that
 * skips the file tests nothing. These bytes came off disk from the version
 * named in each fixture and must never be rewritten - not to tidy a path, not
 * to shrink a file. See tests/corpus/legacy/README.md.
 */
namespace magda::test::legacy_corpus {

inline juce::File corpusDir() {
    return juce::File(MAGDA_LEGACY_CORPUS_DIR);
}

inline juce::File projectsDir() {
    return corpusDir().getChildFile("projects");
}

inline juce::File presetsDir() {
    return corpusDir().getChildFile("presets");
}

/**
 * One project in the corpus, with the contents its file declares.
 *
 * The counts are read out of the saved JSON by hand (see the README) rather
 * than recorded from a load, so a regression that silently drops tracks, clips,
 * devices or automation shows up as a mismatch instead of being re-recorded as
 * the new expectation.
 */
struct ProjectFixture {
    const char* file;
    /// `magdaVersion` exactly as the saving build wrote it.
    const char* savedBy;
    int tracks;
    bool masterTrack;
    int clips;
    int automationLanes;
    int automationClips;
    /// Devices anywhere in the project: track chains, post-FX and rack chains,
    /// plus the master track's own chain.
    int devices;
    int racks;
    /// Devices whose `pluginState` is still pre-v2 engine XML (#1887) once the
    /// project is loaded. A retired device's state is consumed by the alias
    /// migration, so it is NOT counted here even though the file has it.
    int legacyDeviceStates;
    /// Device ids the retired-device aliases must have produced by the end of
    /// the load, comma separated. Empty when the file names no retired device.
    const char* migratedTo;
    /// What this file is in the corpus for.
    const char* covers;
};

/**
 * The corpus, oldest first.
 *
 * "1.0.0" is not a release: it is the placeholder version string builds carried
 * before the version was wired to the tag, so those three files are the oldest
 * format still openable. Every other entry is the version string of the build
 * that saved the file, which for a dev build is the release it follows.
 */
inline const std::vector<ProjectFixture>& projectFixtures() {
    static const std::vector<ProjectFixture> fixtures = {
        {"1.0.0-dupes.mgd", "1.0.0", 3, false, 3, 0, 0, 3, 0, 3, "",
         "oldest format still openable: three 4osc instances whose parameters were all saved "
         "with paramIndex -1, session clips"},
        {"1.0.0-drumgrid-rack.mgd", "1.0.0", 1, false, 1, 0, 0, 2, 1, 1, "magda_compressor",
         "a retired Tracktion compressor inside a rack chain: alias migration below the top level"},
        {"1.0.0-master-eq.mgd", "1.0.0", 0, true, 0, 0, 0, 1, 0, 0, "magda_eq",
         "a retired Tracktion EQ on the master track, with no tracks at all"},
        {"0.4.8-drumgrid.mgd", "0.4.8-10-g83eb35e0", 3, false, 2, 0, 0, 1, 0, 1, "",
         "Drum Grid with pad chains nested in its own engine state"},
        {"0.5.4-macrolinkmod.mgd", "0.5.4-8-g27ac149d", 1, false, 0, 0, 0, 1, 0, 1, "",
         "macro links and a modifier, both keyed on paramIndex"},
        {"0.6.1-sessiondemo.mgd", "0.6.1-48-gcddb687d", 8, false, 32, 0, 0, 0, 0, 0, "",
         "session/launcher clips across eight tracks: launch quantize and follow actions"},
        {"0.7.0-retired-fx.mgd", "0.7.0-rc0", 1, false, 0, 0, 0, 3, 0, 1,
         "magda_compressor,magda_eq",
         "retired Tracktion compressor and EQ with macro links and mods pointing at them"},
        {"0.7.3-dafunk.mgd", "0.7.3-33-gb9154e23", 2, false, 1, 1, 0, 4, 0, 4, "",
         "an absolute automation lane alongside modifiers"},
        {"0.7.3-rack.mgd", "0.7.3-31-g9c1fbbb3", 1, false, 0, 0, 0, 6, 1, 6, "",
         "a rack with a five-device chain"},
        {"0.8.0-fxshowcase.mgd", "0.8.0-rc0", 2, false, 0, 0, 0, 23, 0, 23, "",
         "the widest device roster in the corpus: 23 devices, every one with saved state"},
        {"0.9.0-analysis.mgd", "0.9.0-rc3-1-gde7a0b7c3", 1, false, 1, 0, 0, 2, 0, 2, "",
         "analysis devices in the post-FX stage"},
        {"0.10.2-envfollower.mgd", "0.10.2-53-g020946273", 5, true, 5, 0, 0, 5, 0, 3, "",
         "follower modifiers, an external VST3 chunk and a master chain"},
        {"0.10.2-groups.mgd", "0.10.2-6-g9cfbb3b32", 26, true, 23, 0, 0, 2, 0, 0, "",
         "group tracks with children, 26 tracks"},
        {"0.11.1-automation.mgd", "0.11.1-119-gbe8377ce0", 2, false, 2, 2, 0, 1, 0, 1, "",
         "two automation lanes, one of them tempo"},
        {"0.12.1-fm0demo.mgd", "0.12.1-31-g5d7dc457b", 1, false, 1, 0, 0, 5, 0, 5, "",
         "compiled Faust devices with macros and mods"},
        {"0.13.0-retrovid.mgd", "0.13.0-rc1", 4, false, 9, 0, 0, 3, 0, 1, "",
         "external VST3 state next to internal devices, nine arrangement clips"},
        {"0.14.0-sidechain.mgd", "0.14.0", 2, false, 2, 0, 0, 5, 0, 5, "",
         "the sidechain device and its source wiring, with post-FX devices"},
        {"0.14.0-automation-clips.mgd", "0.14.0-8-gd8212916", 1, false, 1, 1, 1, 0, 0, 0, "",
         "a clip-based automation lane with an automation clip on it"},
        {"0.15.0-demo.mgd", "0.15.0-9-gf444267f", 8, true, 69, 1, 0, 9, 0, 9, "",
         "the largest project in the corpus: 69 clips, nine devices, a lane and a master chain"},
        {"0.16.0-reverse.mgd", "0.16.0-4-gf300f6aa", 1, true, 1, 0, 0, 0, 0, 0, "",
         "a reversed audio clip"},
        {"0.17.0-faust.mgd", "0.17.0-16-gb9ae990b", 2, true, 1, 0, 0, 3, 0, 3, "",
         "the last release before pluginState v2: a JIT Faust device and a compiled one"},
        {"0.18.0-overlaps.mgd", "0.18.0-6-g3c042289", 1, true, 2, 0, 0, 1, 0, 0, "",
         "overlapping arrangement clips saved before clip layering (#2003), and an external "
         "instrument"},
    };
    return fixtures;
}

/// Device presets and other .mps/.mdgk files saved by released versions.
struct PresetFixture {
    const char* file;
    const char* savedBy;
    /// The envelope's `kind`.
    const char* kind;
    const char* covers;
};

inline const std::vector<PresetFixture>& presetFixtures() {
    static const std::vector<PresetFixture> fixtures = {
        {"0.11.1-curve.mps", "0.11.1-119-gbe8377ce0", "curve",
         "an LFO curve preset: bezier handles and per-point curve types"},
        {"0.12.1-step-sequencer.mps", "0.12.1-185-g130141005", "device",
         "a device preset carrying pre-v2 engine state and a full parameter list"},
    };
    return fixtures;
}

// -----------------------------------------------------------------------------
// Walking a loaded project
// -----------------------------------------------------------------------------

inline void forEachDevice(const std::vector<ChainElement>& elements,
                          const std::function<void(const DeviceInfo&)>& visit);

inline void forEachDevice(const RackInfo& rack,
                          const std::function<void(const DeviceInfo&)>& visit) {
    for (const auto& chain : rack.chains)
        forEachDevice(chain.elements, visit);
}

inline void forEachDevice(const std::vector<ChainElement>& elements,
                          const std::function<void(const DeviceInfo&)>& visit) {
    for (const auto& element : elements) {
        if (isDevice(element))
            visit(getDevice(element));
        else if (isRack(element))
            forEachDevice(getRack(element), visit);
    }
}

inline void forEachRack(const std::vector<ChainElement>& elements,
                        const std::function<void(const RackInfo&)>& visit) {
    for (const auto& element : elements) {
        if (!isRack(element))
            continue;
        const auto& rack = getRack(element);
        visit(rack);
        for (const auto& chain : rack.chains)
            forEachRack(chain.elements, visit);
    }
}

inline void forEachDevice(const TrackInfo& track,
                          const std::function<void(const DeviceInfo&)>& visit) {
    forEachDevice(track.chain.fxChainElements, visit);
    for (const auto& element : track.chain.postFxChainElements)
        visit(element.device);
}

/**
 * The frozen paramIndex schema (tests/device_param_schema.txt, #1887) as a map
 * from device type to the parameter ids in index order.
 *
 * Reading the file rather than instantiating devices is what lets a model-level
 * test check a saved paramIndex against the parameter it addresses today: the
 * file IS the compatibility surface, and it is the union across build
 * configurations, so a device an optional pack contributes is covered too.
 */
inline const std::map<juce::String, juce::StringArray>& frozenParamSchema() {
    static const std::map<juce::String, juce::StringArray> schema = [] {
        std::map<juce::String, juce::StringArray> parsed;
        juce::StringArray lines;
        lines.addLines(juce::File(MAGDA_DEVICE_PARAM_SCHEMA_FILE).loadFileAsString());
        for (const auto& line : lines) {
            if (line.isEmpty())
                continue;

            // "<deviceType> <count>" for a device with no parameters, then
            // "<deviceType> <count> <id,id,...>".
            juce::StringArray fields;
            fields.addTokens(line, " ", "");
            if (fields.size() < 2)
                continue;

            juce::StringArray paramIds;
            if (fields.size() >= 3)
                paramIds.addTokens(fields[2], ",", "");
            jassert(paramIds.size() == fields[1].getIntValue());
            parsed[fields[0]] = paramIds;
        }
        return parsed;
    }();
    return schema;
}

}  // namespace magda::test::legacy_corpus
