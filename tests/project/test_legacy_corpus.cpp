#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <set>
#include <vector>

#include "LegacyCorpus.hpp"
#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/ClipTypes.hpp"
#include "magda/daw/core/DeviceState.hpp"
#include "magda/daw/core/LegacyDeviceAliases.hpp"
#include "magda/daw/core/TimeStretchModes.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"

// -----------------------------------------------------------------------------
// The legacy corpus (#2079)
//
// Real .mgd projects and .mps presets saved by released versions, loaded and
// asserted. The requirement from #1896 is that old projects and presets load
// bit-identically or through a documented, tested migration; a corpus whose
// cases are built in code cannot show that, because the file is the input.
//
// What each test here is guarding:
//
//  - the file still opens, and nothing is silently dropped on the way in;
//  - a saved link (automation lane, macro, mod) still addresses the parameter
//    it addressed when it was saved, rather than whatever now sits at that
//    index;
//  - device state written before schema v2 is preserved exactly until an engine
//    is there to convert it (that conversion is asserted in the JUCE suite, in
//    test_legacy_corpus_migration_juce.cpp);
//  - loading and re-saving is stable, so opening an old project and pressing
//    save does not change it a second time.
// -----------------------------------------------------------------------------

namespace magda {
namespace {

namespace corpus = magda::test::legacy_corpus;

struct CorpusFixture {
    CorpusFixture() {
        reset();
    }
    ~CorpusFixture() {
        reset();
    }

    static void reset() {
        TrackManager::getInstance().clearAllTracks();
        ClipManager::getInstance().clearAllClips();
        AutomationManager::getInstance().clearAll();
        UndoManager::getInstance().clearHistory();
    }
};

juce::File projectFile(const corpus::ProjectFixture& fixture) {
    return corpus::projectsDir().getChildFile(fixture.file);
}

struct Counts {
    int tracks = 0;
    bool masterTrack = false;
    int clips = 0;
    int automationLanes = 0;
    int automationClips = 0;
    int devices = 0;
    int racks = 0;
    int legacyDeviceStates = 0;
};

Counts countStaged(const StagedProjectData& staged) {
    Counts counts;
    counts.tracks = static_cast<int>(staged.tracks.size());
    counts.masterTrack = staged.masterTrack != nullptr;
    counts.clips = static_cast<int>(staged.clips.size());
    counts.automationLanes = static_cast<int>(staged.automationLanes.size());
    counts.automationClips = static_cast<int>(staged.automationClips.size());

    const auto countDevice = [&counts](const DeviceInfo& device) {
        ++counts.devices;
        if (device_state::looksLikeLegacyEngineState(device.pluginState))
            ++counts.legacyDeviceStates;
    };
    const auto countRack = [&counts](const RackInfo&) { ++counts.racks; };

    for (const auto& track : staged.tracks) {
        corpus::forEachDevice(track, countDevice);
        corpus::forEachRack(track.chain.fxChainElements, countRack);
    }
    if (staged.masterTrack != nullptr) {
        corpus::forEachDevice(*staged.masterTrack, countDevice);
        corpus::forEachRack(staged.masterTrack->chain.fxChainElements, countRack);
    }
    return counts;
}

/// Devices in the project, paired with the track they live on, so a failure can
/// name both.
struct DeviceRef {
    juce::String trackName;
    const DeviceInfo* device = nullptr;
};

std::vector<DeviceRef> allDevices(const StagedProjectData& staged) {
    std::vector<DeviceRef> devices;
    for (const auto& track : staged.tracks) {
        corpus::forEachDevice(track, [&](const DeviceInfo& device) {
            devices.push_back({track.name, &device});
        });
    }
    if (staged.masterTrack != nullptr) {
        corpus::forEachDevice(*staged.masterTrack, [&](const DeviceInfo& device) {
            devices.push_back({"Master", &device});
        });
    }
    return devices;
}

/// The number of automatable parameters the device type has today, or -1 when
/// this build has no frozen record of the type (an external plugin, or a device
/// an optional pack contributes).
int frozenParameterCount(const juce::String& pluginId) {
    const auto& schema = corpus::frozenParamSchema();
    const auto entry = schema.find(pluginId);
    return entry == schema.end() ? -1 : entry->second.size();
}

bool isInternalDevice(const DeviceInfo& device) {
    return device.format == PluginFormat::Internal;
}

/// Write a serialized project to a temporary .mgd, the same shape the app
/// writes: gzipped UTF-8 JSON, no length prefix.
juce::File writeProjectFile(const juce::var& project) {
    const auto file =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("magda-legacy-corpus-" + juce::Uuid().toDashedString() + ".mgd");
    {
        juce::FileOutputStream out(file);
        juce::GZIPCompressorOutputStream gzip(out, 9);
        gzip.writeText(juce::JSON::toString(project), false, false, nullptr);
        gzip.flush();
        out.flush();
    }
    return file;
}

/// Where two saved documents start to disagree, with enough either side to see
/// which field it is. A whole-document diff would bury the answer.
juce::String firstDifference(const juce::String& a, const juce::String& b) {
    if (a == b)
        return "none";

    const auto* rawA = a.toRawUTF8();
    const auto* rawB = b.toRawUTF8();
    int index = 0;
    while (rawA[index] != 0 && rawB[index] != 0 && rawA[index] == rawB[index])
        ++index;

    const int from = juce::jmax(0, index - 80);
    return "at " + juce::String(index) + "\n  first:  ..." + a.substring(from, index + 80) +
           "\n  second: ..." + b.substring(from, index + 80);
}

}  // namespace

TEST_CASE("Legacy corpus: the fixture table covers the checked-in files",
          "[migration][corpus][serialization]") {
    REQUIRE(corpus::projectsDir().isDirectory());

    std::set<juce::String> onDisk;
    for (const auto& file :
         corpus::projectsDir().findChildFiles(juce::File::findFiles, false, "*.mgd"))
        onDisk.insert(file.getFileName());

    std::set<juce::String> inTable;
    for (const auto& fixture : corpus::projectFixtures())
        inTable.insert(fixture.file);

    for (const auto& name : onDisk) {
        INFO("corpus file with no fixture entry: " << name
                                                   << " - add it to tests/LegacyCorpus.hpp");
        CHECK(inTable.count(name) == 1);
    }
    for (const auto& name : inTable) {
        INFO("fixture entry with no corpus file: " << name);
        CHECK(onDisk.count(name) == 1);
    }

    for (const auto& fixture : corpus::presetFixtures()) {
        INFO("missing preset fixture: " << fixture.file);
        CHECK(corpus::presetsDir().getChildFile(fixture.file).existsAsFile());
    }
}

TEST_CASE("Legacy corpus: every project saved by a released version still loads",
          "[migration][corpus][serialization]") {
    CorpusFixture fixture;

    for (const auto& entry : corpus::projectFixtures()) {
        INFO("fixture: " << entry.file << " (saved by " << entry.savedBy << ") - " << entry.covers);

        const auto file = projectFile(entry);
        REQUIRE(file.existsAsFile());

        StagedProjectData staged;
        INFO("load error: " << ProjectSerializer::getLastError());
        REQUIRE(ProjectSerializer::loadAndStage(file, staged));

        CHECK(staged.info.version == juce::String(entry.savedBy));

        const auto counts = countStaged(staged);
        CHECK(counts.tracks == entry.tracks);
        CHECK(counts.masterTrack == entry.masterTrack);
        CHECK(counts.clips == entry.clips);
        CHECK(counts.automationLanes == entry.automationLanes);
        CHECK(counts.automationClips == entry.automationClips);
        CHECK(counts.devices == entry.devices);
        CHECK(counts.racks == entry.racks);
        CHECK(counts.legacyDeviceStates == entry.legacyDeviceStates);
    }
}

TEST_CASE("Legacy corpus: retired devices are rewritten onto their successors",
          "[migration][corpus][devices]") {
    CorpusFixture fixture;

    for (const auto& entry : corpus::projectFixtures()) {
        INFO("fixture: " << entry.file);

        StagedProjectData staged;
        REQUIRE(ProjectSerializer::loadAndStage(projectFile(entry), staged));

        std::set<juce::String> presentTypes;
        for (const auto& ref : allDevices(staged)) {
            presentTypes.insert(ref.device->pluginId);

            const auto successor = legacy_devices::retiredDeviceSuccessor(ref.device->pluginId);
            INFO("device '" << ref.device->name << "' on '" << ref.trackName << "' still names the "
                            << "retired type '" << ref.device->pluginId << "'; load should have "
                            << "rewritten it to '" << successor << "'");
            CHECK(successor.isEmpty());
        }

        // And the successor is actually there: a migration that dropped the
        // device instead of rewriting it would also pass the check above.
        juce::StringArray expectedSuccessors;
        expectedSuccessors.addTokens(juce::String(entry.migratedTo), ",", "");
        expectedSuccessors.removeEmptyStrings();
        for (const auto& expected : expectedSuccessors) {
            INFO("expected the retired device to load as '" << expected << "'");
            CHECK(presentTypes.count(expected) == 1);
        }
    }
}

TEST_CASE("Legacy corpus: saved parameter indices still address a live parameter",
          "[migration][corpus][devices]") {
    CorpusFixture fixture;

    for (const auto& entry : corpus::projectFixtures()) {
        INFO("fixture: " << entry.file);

        StagedProjectData staged;
        REQUIRE(ProjectSerializer::loadAndStage(projectFile(entry), staged));

        for (const auto& ref : allDevices(staged)) {
            if (!isInternalDevice(*ref.device))
                continue;

            const auto frozenCount = frozenParameterCount(ref.device->pluginId);
            if (frozenCount < 0)
                continue;  // not a device this build knows about

            std::set<int> seen;
            for (const auto& param : ref.device->parameters) {
                INFO("device '" << ref.device->pluginId << "' on '" << ref.trackName
                                << "': saved parameter '" << param.name << "' at index "
                                << param.paramIndex << ", but the device has " << frozenCount
                                << " parameters today. A parameter list that shrank or was "
                                   "reordered needs a paramIndex migration "
                                   "(core/DeviceParamMigrations.hpp).");
                CHECK(param.paramIndex >= 0);
                CHECK(param.paramIndex < frozenCount);

                INFO("duplicate paramIndex " << param.paramIndex << " on '" << ref.device->pluginId
                                             << "'");
                CHECK(seen.insert(param.paramIndex).second);
            }
        }
    }
}

TEST_CASE("Legacy corpus: saved links still resolve to a device in the project",
          "[migration][corpus][automation]") {
    CorpusFixture fixture;

    for (const auto& entry : corpus::projectFixtures()) {
        INFO("fixture: " << entry.file);

        StagedProjectData staged;
        REQUIRE(ProjectSerializer::loadAndStage(projectFile(entry), staged));
        ProjectSerializer::commitStaged(staged);

        auto& trackManager = TrackManager::getInstance();

        const auto checkTarget = [&](const ControlTarget& target, const juce::String& owner) {
            if (target.isEditScoped() || !target.isValid())
                return;
            if (target.kind != ControlTarget::Kind::PluginParam &&
                target.kind != ControlTarget::Kind::DeviceMacro)
                return;

            const auto resolved = trackManager.resolvePath(target.devicePath);
            INFO(owner << " points at a device path that no longer resolves");
            CHECK(resolved.valid);
            if (!resolved.valid || resolved.device == nullptr)
                return;

            if (target.kind == ControlTarget::Kind::DeviceMacro) {
                INFO(owner << " addresses macro " << target.paramIndex << " on '"
                           << resolved.device->pluginId << "', which has "
                           << static_cast<int>(resolved.device->macros.size()));
                CHECK(target.paramIndex < static_cast<int>(resolved.device->macros.size()));
                return;
            }

            if (!isInternalDevice(*resolved.device))
                return;
            const auto frozenCount = frozenParameterCount(resolved.device->pluginId);
            if (frozenCount < 0)
                return;

            INFO(owner << " addresses parameter " << target.paramIndex << " on '"
                       << resolved.device->pluginId << "', which has " << frozenCount
                       << " parameters today");
            CHECK(target.paramIndex < frozenCount);
        };

        for (const auto& lane : AutomationManager::getInstance().getLanes())
            checkTarget(lane.target, "automation lane '" + lane.name + "'");

        for (const auto& track : trackManager.getTracks()) {
            for (const auto& macro : track.macros)
                for (const auto& link : macro.links)
                    checkTarget(link.target, "track macro '" + macro.name + "'");
            for (const auto& mod : track.mods)
                for (const auto& link : mod.links)
                    checkTarget(link.target, "track mod '" + mod.name + "'");

            corpus::forEachDevice(track, [&](const DeviceInfo& device) {
                for (const auto& macro : device.macros)
                    for (const auto& link : macro.links)
                        checkTarget(link.target,
                                    "macro '" + macro.name + "' on '" + device.name + "'");
                for (const auto& mod : device.mods)
                    for (const auto& link : mod.links)
                        checkTarget(link.target, "mod '" + mod.name + "' on '" + device.name + "'");
            });
        }

        CorpusFixture::reset();
    }
}

TEST_CASE("Legacy corpus: persisted enum values stay inside their pinned ranges",
          "[migration][corpus][enums]") {
    CorpusFixture fixture;

    const auto inRange = [](int value, int lowest, int highest) {
        return value >= lowest && value <= highest;
    };

    for (const auto& entry : corpus::projectFixtures()) {
        INFO("fixture: " << entry.file);

        StagedProjectData staged;
        REQUIRE(ProjectSerializer::loadAndStage(projectFile(entry), staged));

        for (const auto& clip : staged.clips) {
            INFO("clip '" << clip.name << "'");
            CHECK(inRange(static_cast<int>(clip.view), static_cast<int>(ClipView::Arrangement),
                          static_cast<int>(ClipView::Session)));
            CHECK(inRange(static_cast<int>(clip.launchMode), static_cast<int>(LaunchMode::Trigger),
                          static_cast<int>(LaunchMode::Toggle)));
            CHECK(inRange(static_cast<int>(clip.launchQuantize),
                          static_cast<int>(LaunchQuantize::None),
                          static_cast<int>(LaunchQuantize::SixteenthBar)));
            CHECK(inRange(static_cast<int>(clip.followAction), static_cast<int>(FollowAction::None),
                          static_cast<int>(FollowAction::PlayAgain)));
            if (!clip.isAudio())
                continue;  // fades and stretch modes live on audio events

            for (const auto& event : clip.events()) {
                INFO("audio event on clip '" << clip.name << "'");
                CHECK(inRange(event.fadeInType, static_cast<int>(FadeCurve::Linear),
                              static_cast<int>(FadeCurve::SCurve)));
                CHECK(inRange(event.fadeOutType, static_cast<int>(FadeCurve::Linear),
                              static_cast<int>(FadeCurve::SCurve)));

                // Sparse on purpose: the gaps are engine stretcher modes MAGDA
                // does not offer, so "in range" is not the check - membership is.
                const bool knownStretchMode =
                    event.timeStretchMode == time_stretch_mode::kDisabled ||
                    event.timeStretchMode == time_stretch_mode::kSoundTouchNormal ||
                    event.timeStretchMode == time_stretch_mode::kSoundTouchBetter ||
                    event.timeStretchMode == time_stretch_mode::kSignalsmith;
                INFO("time stretch mode " << event.timeStretchMode << " is not a pinned mode");
                CHECK(knownStretchMode);
            }
        }

        for (const auto& track : staged.tracks) {
            INFO("track '" << track.name << "'");
            CHECK(inRange(static_cast<int>(track.inputMonitor),
                          static_cast<int>(InputMonitorMode::Off),
                          static_cast<int>(InputMonitorMode::Auto)));
            CHECK(inRange(static_cast<int>(track.playbackMode),
                          static_cast<int>(TrackPlaybackMode::Arrangement),
                          static_cast<int>(TrackPlaybackMode::Session)));

            for (const auto& mod : track.mods) {
                INFO("mod '" << mod.name << "'");
                CHECK(inRange(static_cast<int>(mod.type), static_cast<int>(ModType::LFO),
                              static_cast<int>(ModType::Follower)));
                CHECK(inRange(static_cast<int>(mod.waveform), static_cast<int>(LFOWaveform::Sine),
                              static_cast<int>(LFOWaveform::Custom)));
                CHECK(inRange(static_cast<int>(mod.triggerMode),
                              static_cast<int>(LFOTriggerMode::Free),
                              static_cast<int>(LFOTriggerMode::Audio)));
                CHECK(inRange(static_cast<int>(mod.curvePreset),
                              static_cast<int>(CurvePreset::Triangle),
                              static_cast<int>(CurvePreset::Custom)));
            }
        }
    }
}

TEST_CASE("Legacy corpus: pre-v2 device state is preserved until an engine converts it",
          "[migration][corpus][devices]") {
    CorpusFixture fixture;

    for (const auto& entry : corpus::projectFixtures()) {
        if (entry.legacyDeviceStates == 0)
            continue;

        INFO("fixture: " << entry.file);

        StagedProjectData staged;
        REQUIRE(ProjectSerializer::loadAndStage(projectFile(entry), staged));

        for (const auto& ref : allDevices(staged)) {
            const auto& state = ref.device->pluginState;
            if (!device_state::looksLikeLegacyEngineState(state))
                continue;

            // A model round trip must not touch it. The conversion to v2 needs a
            // live plugin to read the values off, so anything that rewrote the
            // string here would be dropping state rather than migrating it.
            DeviceInfo restored;
            REQUIRE(ProjectSerializer::deserializeDeviceInfo(
                ProjectSerializer::serializeDeviceInfo(*ref.device), restored));

            INFO("device '" << ref.device->pluginId << "' on '" << ref.trackName << "'");
            CHECK(restored.pluginState == state);
            CHECK(device_state::looksLikeLegacyEngineState(restored.pluginState));
            CHECK(!device_state::isDeviceStateV2(restored.pluginState));
            CHECK(restored.parameters.size() == ref.device->parameters.size());
        }
    }
}

TEST_CASE("Legacy corpus: loading and re-saving a legacy project is stable",
          "[migration][corpus][serialization]") {
    CorpusFixture fixture;

    for (const auto& entry : corpus::projectFixtures()) {
        INFO("fixture: " << entry.file);

        StagedProjectData staged;
        REQUIRE(ProjectSerializer::loadAndStage(projectFile(entry), staged));
        const auto loaded = countStaged(staged);

        auto info = staged.info;
        ProjectSerializer::commitStaged(staged);

        // Save the way the app does: straight off the managers.
        const auto firstSave = ProjectSerializer::serializeProject(info);
        REQUIRE(firstSave.isObject());

        // Round trip through a real file, so this is the same reader the app
        // uses rather than a second, test-only path.
        const auto resaved = writeProjectFile(firstSave);
        CorpusFixture::reset();

        StagedProjectData reloaded;
        INFO("reload error: " << ProjectSerializer::getLastError());
        REQUIRE(ProjectSerializer::loadAndStage(resaved, reloaded));
        auto reloadedInfo = reloaded.info;
        ProjectSerializer::commitStaged(reloaded);
        resaved.deleteFile();

        const auto secondSave = ProjectSerializer::serializeProject(reloadedInfo);
        REQUIRE(secondSave.isObject());

        // Byte-for-byte: the second save is what a user gets by opening an old
        // project, saving, and saving again. A difference here is a migration
        // that never settles.
        const auto firstText = juce::JSON::toString(firstSave);
        const auto secondText = juce::JSON::toString(secondSave);
        INFO("first difference: " << firstDifference(firstText, secondText));
        CHECK(firstText == secondText);

        // And nothing was lost on the way through the managers.
        StagedProjectData afterReload;
        afterReload.tracks = TrackManager::getInstance().getTracks();
        afterReload.clips = ClipManager::getInstance().getClips();
        afterReload.automationLanes = AutomationManager::getInstance().getLanes();
        afterReload.automationClips = AutomationManager::getInstance().getClips();
        if (auto* master = TrackManager::getInstance().getTrack(MASTER_TRACK_ID))
            afterReload.masterTrack = std::make_unique<TrackInfo>(*master);

        const auto after = countStaged(afterReload);
        CHECK(after.clips == loaded.clips);
        CHECK(after.automationLanes == loaded.automationLanes);
        CHECK(after.automationClips == loaded.automationClips);
        CHECK(after.devices == loaded.devices);
        CHECK(after.racks == loaded.racks);
        CHECK(after.legacyDeviceStates == loaded.legacyDeviceStates);

        CorpusFixture::reset();
    }
}

TEST_CASE("Legacy corpus: presets saved by released versions still load",
          "[migration][corpus][presets]") {
    for (const auto& entry : corpus::presetFixtures()) {
        INFO("preset: " << entry.file << " (saved by " << entry.savedBy << ") - " << entry.covers);

        const auto file = corpus::presetsDir().getChildFile(entry.file);
        REQUIRE(file.existsAsFile());

        const auto json = juce::JSON::parse(file);
        REQUIRE(json.isObject());
        auto* envelope = json.getDynamicObject();
        REQUIRE(envelope != nullptr);

        CHECK(envelope->getProperty("magdaVersion").toString() == juce::String(entry.savedBy));
        CHECK(envelope->getProperty("kind").toString() == juce::String(entry.kind));

        const auto payload = envelope->getProperty("payload");
        REQUIRE(payload.isObject());

        if (juce::String(entry.kind) != "device")
            continue;

        DeviceInfo device;
        REQUIRE(ProjectSerializer::deserializeDeviceInfo(payload, device));
        CHECK(device.pluginId.isNotEmpty());
        CHECK(!device.parameters.empty());

        // A preset written before schema v2 carries the engine's own plugin XML,
        // and the same rule as a project applies: keep it verbatim until a live
        // plugin can convert it.
        if (device_state::looksLikeLegacyEngineState(device.pluginState)) {
            DeviceInfo restored;
            REQUIRE(ProjectSerializer::deserializeDeviceInfo(
                ProjectSerializer::serializeDeviceInfo(device), restored));
            CHECK(restored.pluginState == device.pluginState);
        }

        const auto frozenCount = frozenParameterCount(device.pluginId);
        if (frozenCount >= 0) {
            for (const auto& param : device.parameters) {
                INFO("preset parameter '" << param.name << "' at index " << param.paramIndex
                                          << ", device has " << frozenCount << " today");
                CHECK(param.paramIndex >= 0);
                CHECK(param.paramIndex < frozenCount);
            }
        }
    }
}

}  // namespace magda
