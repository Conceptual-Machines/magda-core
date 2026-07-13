#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>

#include "SharedTestEngine.hpp"
#include "third_party/tracktion_engine/modules/tracktion_engine/playback/graph/tracktion_ArrangerClipControlNode.h"
#include "third_party/tracktion_engine/modules/tracktion_engine/playback/graph/tracktion_ArrangerLauncherSwitchingNode.h"
#include "third_party/tracktion_engine/modules/tracktion_engine/playback/graph/tracktion_TracktionEngineNode.h"
#include "third_party/tracktion_engine/modules/tracktion_engine/utilities/tracktion_TestUtilities.h"
#include "third_party/tracktion_engine/modules/tracktion_graph/tracktion_graph.h"

namespace te = tracktion;

namespace {

class CountingAudioNode final : public te::graph::Node {
  public:
    CountingAudioNode() {
        setOptimisations({te::graph::ClearBuffers::yes, te::graph::AllocateAudioBuffer::yes});
    }

    te::graph::NodeProperties getNodeProperties() override {
        return {.hasAudio = true,
                .hasMidi = false,
                .numberOfChannels = 1,
                .nodeID = 0x617272616e676572ULL};
    }

    bool isReadyToProcess() override {
        return true;
    }

    void process(ProcessContext& pc) override {
        ++processCount;
        for (choc::buffer::ChannelCount channel = 0; channel < pc.buffers.audio.getNumChannels();
             ++channel) {
            auto* samples = pc.buffers.audio.getIterator(channel).sample;
            std::fill(samples, samples + pc.buffers.audio.getNumFrames(), 0.5f);
        }
    }

    int processCount = 0;
};

}  // namespace

class ArrangerLauncherSwitchingTest final : public juce::UnitTest {
  public:
    ArrangerLauncherSwitchingTest()
        : juce::UnitTest("Arranger Launcher Conditional Processing Tests", "magda") {}

    void runTest() override {
        beginTest("Queued launcher stop does not reclaim playback from arrangement");

        te::LaunchHandle launchHandle;
        launchHandle.play(std::nullopt);

        te::SyncRange syncRange;
        auto nextSyncPoint = syncRange.end;
        const auto halfBeat = te::BeatDuration::fromBeats(0.5);
        nextSyncPoint.monotonicBeat.v = nextSyncPoint.monotonicBeat.v + halfBeat;
        nextSyncPoint.beat = nextSyncPoint.beat + halfBeat;
        syncRange = {syncRange.end, nextSyncPoint};
        launchHandle.advance(syncRange);

        expect(te::ArrangerLauncherSwitchingNode::shouldActivateSlotPlayback(launchHandle),
               "A playing launcher should claim the track");

        launchHandle.stop(std::nullopt);
        expect(launchHandle.getPlayingStatus() == te::LaunchHandle::PlayState::playing,
               "The handle remains playing until the audio thread consumes its stop");
        expect(launchHandle.getQueuedStatus() == te::LaunchHandle::QueueState::stopQueued,
               "Back to Arrangement queues an immediate launcher stop");
        expect(!te::ArrangerLauncherSwitchingNode::shouldActivateSlotPlayback(launchHandle),
               "A queued stop must not overwrite playSlotClips=false");

        beginTest("Arrangement reader only processes while arrangement owns the track");

        auto* engine = magda::test::getSharedEngine().getEngine();
        expect(engine != nullptr, "Shared engine should be available");
        if (engine == nullptr)
            return;

        auto edit = te::test_utilities::createTestEdit(*engine, 1);
        auto tracks = te::getAudioTracks(*edit);
        expect(!tracks.isEmpty(), "Test edit should contain an audio track");
        if (tracks.isEmpty())
            return;

        auto* track = tracks.getFirst();
        auto input = std::make_unique<CountingAudioNode>();
        auto* inputCounter = input.get();
        auto controlNode = std::make_unique<te::ArrangerClipControlNode>(*track, std::move(input));
        auto graph = te::graph::createNodeGraph(std::move(controlNode), false);

        constexpr double sampleRate = 44100.0;
        constexpr int blockSize = 256;
        const te::graph::PlaybackInitialisationInfo info{sampleRate, blockSize, *graph, nullptr,
                                                         {},         {},        false};
        for (auto* node : graph->orderedNodes)
            node->initialise(info);

        auto processBlock = [&](int64_t startSample) {
            for (auto* node : graph->orderedNodes)
                node->prepareForNextBlock({startSample, startSample + blockSize});

            for (auto* node : graph->orderedNodes) {
                expect(node->isReadyToProcess(), "Conditional graph should be ready");
                node->process(blockSize, {startSample, startSample + blockSize});
            }

            return graph->rootNode->getProcessedOutput().audio;
        };

        track->playSlotClips = true;
        auto sessionOutput = processBlock(0);
        expectEquals(inputCounter->processCount, 0,
                     "Session ownership must not process the inaudible arrangement reader");
        expectWithinAbsoluteError(sessionOutput.getSample(0, 0), 0.0f, 0.0001f,
                                  "Session-owned arrangement reader should output silence");

        track->playSlotClips = false;
        auto arrangementOutput = processBlock(blockSize);
        expectEquals(inputCounter->processCount, 1,
                     "Returning to arrangement should resume its reader on the next block");
        expectWithinAbsoluteError(arrangementOutput.getSample(0, 0), 0.5f, 0.0001f,
                                  "Arrangement-owned reader should output arrangement audio");

        track->playSlotClips = true;
        auto resumedSessionOutput = processBlock(2 * blockSize);
        expectEquals(inputCounter->processCount, 1,
                     "Re-entering Session must suspend the arrangement reader again");
        expectWithinAbsoluteError(resumedSessionOutput.getSample(0, 0), 0.0f, 0.0001f,
                                  "Suspended arrangement reader should contribute silence");
    }
};

static ArrangerLauncherSwitchingTest arrangerLauncherSwitchingTest;
