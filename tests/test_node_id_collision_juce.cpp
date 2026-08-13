#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_graph/tracktion_graph.h>

#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "SharedTestEngine.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h"
#include "third_party/tracktion_engine/modules/tracktion_engine/playback/graph/tracktion_TracktionEngineNode.h"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"
#include "third_party/tracktion_engine/modules/tracktion_graph/tracktion_graph/tracktion_TestUtilities.h"

/**
 * Node identity across many tracks (#2085).
 *
 * Every Node in a playback graph is expected to carry an id that no other Node
 * in the same graph carries: `node_player_utils::prepareToPlay` asserts on it,
 * and `ArrangerLauncherSwitchingNode::prepareToPlay` uses it to find its own
 * previous self when a graph is swapped, so that fader ramps and the active
 * note list survive the swap. Two Nodes sharing an id means one track can be
 * handed the other's carried state.
 *
 * The graph is built here rather than rendered: what is under test is the id,
 * and a render would only reach it through everything else that can fail.
 */

namespace te = tracktion;

namespace {

/// A graph's node ids, by the type that produced them.
struct NodeIdentity {
    std::size_t id = 0;
    std::string type;
};

std::vector<NodeIdentity> collectNodeIdentities(te::graph::Node& root) {
    std::vector<NodeIdentity> identities;

    for (auto* node : te::graph::getNodes(root, te::graph::VertexOrdering::postordering))
        identities.push_back({node->getNodeProperties().nodeID, typeid(*node).name()});

    return identities;
}

/// The ids that more than one Node in the graph claims.
///
/// Zero is excluded the way the engine's own check excludes it: a Node that
/// does not participate in identity reports zero, and any number of those is
/// not a collision.
std::map<std::size_t, std::vector<std::string>> duplicates(
    const std::vector<NodeIdentity>& identities) {
    std::map<std::size_t, std::vector<std::string>> byId;

    for (const auto& identity : identities)
        if (identity.id != 0)
            byId[identity.id].push_back(identity.type);

    for (auto iter = byId.begin(); iter != byId.end();)
        iter = iter->second.size() > 1 ? std::next(iter) : byId.erase(iter);

    return byId;
}

/// An Edit with `numTracks` audio tracks, each playing one file of its own.
///
/// Different files on purpose: a node's identity is partly a hash of what it
/// plays, so two tracks playing the same file at the same position collide for
/// a reason that has nothing to do with what is under test here.
struct Project {
    std::unique_ptr<te::Edit> edit;
    std::vector<std::unique_ptr<juce::TemporaryFile>> sources;
};

Project makeProject(te::Engine& engine, int numTracks) {
    Project project;
    project.edit = te::engine::test_utilities::createTestEdit(engine, numTracks);

    if (project.edit == nullptr)
        return project;

    auto tracks = te::getAudioTracks(*project.edit);

    for (int index = 0; index < numTracks && index < tracks.size(); ++index) {
        auto source = te::graph::test_utilities::getSinFile<juce::WavAudioFormat>(
            44100.0, 2.0, 1, 220.0f + 10.0f * static_cast<float>(index));

        auto* track = tracks[index];
        track->insertWaveClip(
            "clip" + juce::String(index), source->getFile(),
            {{te::TimePosition(), te::TimePosition::fromSeconds(2.0)}, te::TimeDuration()}, false);

        project.sources.push_back(std::move(source));
    }

    return project;
}

std::unique_ptr<te::graph::Node> buildGraph(te::Edit& edit, te::ProcessState& processState) {
    te::CreateNodeParams params{processState};
    params.sampleRate = 44100.0;
    params.blockSize = 256;
    params.forRendering = true;

    return te::createNodeForEdit(edit, params);
}

class NodeIdCollisionTests final : public juce::UnitTest {
  public:
    NodeIdCollisionTests() : juce::UnitTest("Node ID Collision Tests", "Engine") {}

    void runTest() override {
        auto& wrapper = magda::test::getSharedEngine();
        auto* engine = wrapper.getEngine();

        if (engine == nullptr) {
            beginTest("engine");
            expect(false, "no Tracktion engine");
            return;
        }

        // Up to eight, because the report is about four and a fix that only
        // moves the first collision along by one track is not a fix.
        for (int numTracks = 1; numTracks <= 8; ++numTracks) {
            beginTest(juce::String(numTracks) + " audio tracks have unique node ids");

            auto project = makeProject(*engine, numTracks);
            expect(project.edit != nullptr, "no Edit");

            if (project.edit == nullptr)
                continue;

            te::graph::PlayHead playHead;
            te::graph::PlayHeadState playHeadState{playHead};
            te::ProcessState processState{playHeadState, project.edit->tempoSequence};

            auto graph = buildGraph(*project.edit, processState);
            expect(graph != nullptr, "no graph");

            if (graph == nullptr)
                continue;

            const auto identities = collectNodeIdentities(*graph);
            const auto collisions = duplicates(identities);

            for (const auto& [id, types] : collisions) {
                std::ostringstream report;
                report << "id " << id << " is claimed by " << types.size() << " nodes:";
                for (const auto& type : types)
                    report << " " << type;
                logMessage(report.str());
            }

            expect(collisions.empty(),
                   "node ids collide with " + juce::String(numTracks) + " tracks");
        }
    }
};

NodeIdCollisionTests nodeIdCollisionTests;

}  // namespace
