#include <catch2/catch_test_macros.hpp>

#include "LegacyCorpus.hpp"
#include "magda/daw/core/DrumGridPads.hpp"
#include "magda/daw/core/RackInfo.hpp"
#include "magda/daw/core/TrackInfo.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"

// The pad projection against real files (#2192).
//
// The synthetic cases in test_drum_grid_pads.cpp fix the shape; these fix the
// fact that released builds actually wrote that shape. Two projects in the
// legacy corpus carry a Drum Grid, saved four minor versions apart, and one of
// them has it inside a rack.

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
    }
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
