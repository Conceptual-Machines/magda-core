#include <catch2/catch_test_macros.hpp>
#include <set>

#include "LegacyCorpus.hpp"
#include "magda/daw/audio/plugins/InternalPluginRegistry.hpp"
#include "magda/daw/core/DrumGridPads.hpp"
#include "magda/daw/core/RackInfo.hpp"
#include "magda/daw/core/TrackInfo.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"

// The pad projection against real files (#2192). test_drum_grid_pads.cpp fixes
// the shape; these fix that released builds actually wrote it. Two corpus
// projects carry a Drum Grid, four minor versions apart, one of them racked.

namespace {

void collectDrumGrids(const std::vector<magda::ChainElement>& elements,
                      std::vector<const magda::DeviceInfo*>& out) {
    for (const auto& element : elements) {
        if (magda::isDevice(element)) {
            const auto& device = magda::getDevice(element);
            if (magda::isPadRackDevice(device.pluginId))
                out.push_back(&device);
            continue;
        }

        for (const auto& chain : magda::getRack(element).chains)
            collectDrumGrids(chain.elements, out);
    }
}

std::vector<const magda::DeviceInfo*> drumGridsIn(const magda::StagedProjectData& staged) {
    std::vector<const magda::DeviceInfo*> out;
    for (const auto& track : staged.tracks) {
        collectDrumGrids(track.chain.fxChainElements, out);

        // Post-FX is a flat list of devices rather than the element tree, and a
        // Drum Grid is an instrument so it never sits there. Checked anyway, so
        // this counts every Drum Grid a project has rather than most of them.
        for (const auto& element : track.chain.postFxChainElements)
            if (magda::isPadRackDevice(element.device.pluginId))
                out.push_back(&element.device);
    }
    return out;
}

magda::StagedProjectData loadCorpusProject(const char* file) {
    const auto path = magda::test::legacy_corpus::projectsDir().getChildFile(file);
    REQUIRE(path.existsAsFile());

    magda::StagedProjectData staged;
    REQUIRE(magda::ProjectSerializer::loadAndStage(path, staged));
    return staged;
}

void requirePadsAreReal(const magda::DeviceInfo& drumGrid) {
    INFO("drum grid device " << drumGrid.name);
    REQUIRE(static_cast<bool>(drumGrid.padRack));
    REQUIRE_FALSE(drumGrid.padRack->chains.empty());

    for (const auto& pad : drumGrid.padRack->chains) {
        INFO("pad " << pad.name);
        // A pad answers to a real range. The whole point of the projection is
        // that the compiler can key a chain by pitch, so a pad that came back
        // taking every note would be the projection silently not working.
        CHECK_FALSE(pad.answersToEveryNote());
        CHECK(pad.lowNote >= 0);
        CHECK(pad.lowNote <= 127);
        CHECK(pad.highNote >= pad.lowNote);
        CHECK(pad.highNote <= 127);
        CHECK(pad.rootNote >= 0);
        CHECK(pad.rootNote <= 127);

        for (const auto& element : pad.elements) {
            REQUIRE(magda::isDevice(element));
            const auto& device = magda::getDevice(element);
            INFO("pad device " << device.name << " / " << device.pluginId);
            CHECK(device.pluginId.isNotEmpty());

            // Non-empty is not enough, and checking only that is what let the
            // real external plugins in this corpus project through as an effect
            // called "vst". Tracktion saves every external plugin under that one
            // type name, so it identifies nothing and must never survive as
            // either the id or the display name.
            CHECK(device.pluginId != "vst");
            CHECK(device.name != "vst");
            CHECK(device.name.isNotEmpty());

            // Internal and external are the two cases, and each has to come out
            // as itself: an internal device left on PluginFormat's VST3 default
            // claims an editor window it has not got, and an external one needs
            // the file it was loaded from.
            if (magda::daw::audio::findInternalPluginSpec(device.pluginId) != nullptr)
                CHECK(device.format == magda::PluginFormat::Internal);
            else
                CHECK(device.fileOrIdentifier.isNotEmpty());
        }

        // At most one instrument per pad. Position does not decide which, so
        // two would mean the flag is being read off the wrong thing.
        int instruments = 0;
        for (const auto& element : pad.elements)
            if (magda::getDevice(element).isInstrument)
                ++instruments;
        CHECK(instruments <= 1);
    }

    // A DeviceId is not a load-time fact for these files. A Drum Grid allocates
    // one per pad plugin when it restores it, so a project saved before that
    // plugin was ever instantiated carries none, and both corpus projects are
    // that old. What load must guarantee is that the ids it DOES find are
    // distinct: an id read from the file and duplicated across pads would key
    // two ops the same and bind one pad's parameters to another's.
    std::set<magda::DeviceId> ids;
    int withIds = 0;
    for (const auto& pad : drumGrid.padRack->chains) {
        for (const auto& element : pad.elements) {
            const auto id = magda::getDevice(element).id;
            if (id == magda::INVALID_DEVICE_ID)
                continue;
            ++withIds;
            ids.insert(id);
        }
    }
    CHECK(static_cast<int>(ids.size()) == withIds);
}

}  // namespace

TEST_CASE("A 0.4.8 project's Drum Grid pads reach the model", "[drumgrid][pads][corpus]") {
    const auto staged = loadCorpusProject("0.4.8-drumgrid.mgd");
    const auto drumGrids = drumGridsIn(staged);

    REQUIRE_FALSE(drumGrids.empty());
    for (const auto* drumGrid : drumGrids)
        requirePadsAreReal(*drumGrid);
}

TEST_CASE("A 1.0.0 project's racked Drum Grid pads reach the model", "[drumgrid][pads][corpus]") {
    const auto staged = loadCorpusProject("1.0.0-drumgrid-rack.mgd");
    const auto drumGrids = drumGridsIn(staged);

    REQUIRE_FALSE(drumGrids.empty());
    for (const auto* drumGrid : drumGrids)
        requirePadsAreReal(*drumGrid);
}

TEST_CASE("A project with no Drum Grid has no pad racks", "[drumgrid][pads][corpus]") {
    const auto staged = loadCorpusProject("0.16.0-reverse.mgd");
    CHECK(drumGridsIn(staged).empty());
}
