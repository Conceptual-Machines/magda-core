#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_graph/tracktion_graph.h>

#include "SharedTestEngine.hpp"
// These Tracktion internal headers are order-dependent: the base node types
// must be defined before LoopingMidiNode and SlotControlNode.
// clang-format off
#include "third_party/tracktion_engine/modules/tracktion_engine/playback/graph/tracktion_TracktionEngineNode.h"
#include "third_party/tracktion_engine/modules/tracktion_engine/playback/graph/tracktion_LoopingMidiNode.h"
#include "third_party/tracktion_engine/modules/tracktion_engine/playback/graph/tracktion_SlotControlNode.h"
// clang-format on
#include "third_party/tracktion_engine/modules/tracktion_engine/playback/graph/tracktion_TracktionNodePlayer.h"
#include "third_party/tracktion_engine/modules/tracktion_engine/playback/graph/tracktion_WaveNode.h"
#include "third_party/tracktion_engine/modules/tracktion_graph/tracktion_graph/tracktion_TestUtilities.h"

namespace te = tracktion::engine;
namespace tg = tracktion::graph;
using namespace tracktion::literals;

class SignalsmithLoopTest final : public juce::UnitTest {
  public:
    SignalsmithLoopTest() : juce::UnitTest("Signalsmith Loop Tests", "magda") {}

    void runTest() override {
        runLoopTest(false);
        runLoopTest(true);
    }

  private:
    static constexpr double sampleRate = 44100.0;
    static constexpr int blockSize = 512;
    static constexpr double sourceBpm = 75.0;
    static constexpr auto loopBeats = 1.25_bd;
    static constexpr int numCycles = 8;
    static constexpr int outputCycleSamples = 44160;
    static constexpr double outputCycleSeconds = outputCycleSamples / sampleRate;
    static constexpr double editBpm = 60.0 * 1.25 / outputCycleSeconds;

    void runLoopTest(bool useSessionSlot) {
        beginTest(useSessionSlot
                      ? "Session loop-head transients remain stable across transport wraps"
                      : "Arrangement loop-head transients remain stable across transport wraps");

        auto* engine = magda::test::getSharedEngine().getEngine();
        expect(engine != nullptr, "Shared Tracktion engine should exist");
        if (engine == nullptr)
            return;

        juce::AudioBuffer<float> transientBuffer(1, juce::roundToInt(sampleRate));
        transientBuffer.clear();
        for (int sample = 0; sample < juce::roundToInt(sampleRate * 0.10); ++sample) {
            const auto envelope = std::exp(-static_cast<double>(sample) / (sampleRate * 0.015));
            const auto body =
                std::cos(juce::MathConstants<double>::twoPi * 95.0 * sample / sampleRate);
            transientBuffer.setSample(0, sample, static_cast<float>(envelope * body));
        }
        auto transientFile = tg::test_utilities::writeToTemporaryFile<juce::WavAudioFormat>(
            tg::toBufferView(transientBuffer), sampleRate);
        te::AudioFile audioFile(*engine, transientFile->getFile());
        expect(audioFile.isValid(), "Transient fixture should be readable");
        if (!audioFile.isValid())
            return;

        tracktion::tempo::Sequence editTempoSequence(
            {{0_bp, editBpm, 0.0f}}, {{0_bp, 4, 4, false}},
            tracktion::tempo::LengthOfOneBeat::dependsOnTimeSignature);
        tracktion::tempo::Sequence fileTempoSequence(
            {{0_bp, sourceBpm, 0.0f}}, {{0_bp, 4, 4, false}},
            tracktion::tempo::LengthOfOneBeat::dependsOnTimeSignature);
        te::WarpMap warpMap{{0_tp, 0_tp}, {0.45_tp, 0.50_tp}, {1_tp, 1_tp}};

        tg::PlayHead playHead;
        tg::PlayHeadState playHeadState(playHead);
        te::ProcessState processState(playHeadState, editTempoSequence);
        playHead.play({0, outputCycleSamples * 2}, true);

        auto node = std::make_unique<te::WaveNodeRealTime>(
            audioFile, te::TimeStretcher::signalsmith, te::TimeStretcher::ElastiqueProOptions(),
            useSessionSlot ? tracktion::BeatRange(0_bp, tracktion::BeatPosition::fromBeats(
                                                            std::numeric_limits<double>::max()))
                           : tracktion::BeatRange(0_bp, loopBeats * numCycles),
            0_bd, tracktion::BeatRange(0_bp, loopBeats), te::LiveClipLevel(),
            juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono(), processState,
            te::EditItemID(), true, te::ResamplingQuality::lagrange, te::SpeedFadeDescription(),
            std::nullopt, warpMap, fileTempoSequence, te::WaveNodeRealTime::SyncTempo::yes,
            te::WaveNodeRealTime::SyncPitch::no, std::nullopt);
        auto* waveNode = node.get();

        std::unique_ptr<tg::Node> rootNode = std::move(node);
        if (useSessionSlot) {
            auto launchHandle = std::make_shared<te::LaunchHandle>();
            launchHandle->play(std::nullopt);
            rootNode = std::make_unique<te::SlotControlNode>(
                processState, launchHandle, std::nullopt, std::function<void(te::MonotonicBeat)>(),
                te::EditItemID(), std::move(rootNode), 256);
        }

        tg::test_utilities::TestSetup setup{sampleRate, blockSize, false, getRandom()};
        auto player = std::make_unique<te::TracktionNodePlayer>(
            std::move(rootNode), processState, sampleRate, blockSize,
            tg::getPoolCreatorFunction(tg::ThreadPoolStrategy::realTime));
        expectEquals(waveNode->getNumTimeStretchStages(), 1,
                     "A non-identity warp and tempo sync should share one stretch processor");
        tg::test_utilities::TestProcess<te::TracktionNodePlayer> process(
            std::move(player), setup, 1, outputCycleSeconds * numCycles, true);
        const auto renderStartMs = juce::Time::getMillisecondCounterHiRes();
        double maxWrapBlockMs = 0.0;
        double maxRegularBlockMs = 0.0;
        bool hasMoreBlocks = true;
        int blockIndex = 0;

        while (hasMoreBlocks) {
            const auto blockStartMs = juce::Time::getMillisecondCounterHiRes();
            hasMoreBlocks = process.process(blockSize);
            const auto blockDurationMs = juce::Time::getMillisecondCounterHiRes() - blockStartMs;
            const auto firstSample = blockIndex * blockSize;
            const auto lastSample =
                std::min(firstSample + blockSize, outputCycleSamples * numCycles) - 1;
            const bool crossesClipWrap =
                firstSample / outputCycleSamples != lastSample / outputCycleSamples;

            if (blockIndex > 0) {
                auto& maximum = crossesClipWrap ? maxWrapBlockMs : maxRegularBlockMs;
                maximum = std::max(maximum, blockDurationMs);
            }
            ++blockIndex;
        }

        auto context = process.getTestResult();
        const auto renderDurationMs = juce::Time::getMillisecondCounterHiRes() - renderStartMs;

        expect(context != nullptr, "Graph render should return a context");
        if (context == nullptr)
            return;

        std::array<float, numCycles> attackPeaks{};
        const auto attackWindow = juce::roundToInt(sampleRate * 0.20);

        for (int cycle = 0; cycle < numCycles; ++cycle) {
            const auto cycleStart = juce::roundToInt(cycle * outputCycleSeconds * sampleRate);
            attackPeaks[static_cast<size_t>(cycle)] =
                context->buffer.getMagnitude(0, cycleStart, attackWindow);
        }

        juce::String metrics(useSessionSlot ? "session attack peaks: "
                                            : "arrangement attack peaks: ");
        for (const auto peak : attackPeaks)
            metrics << juce::String(peak, 5) << " ";
        metrics << "renderMs=" << juce::String(renderDurationMs, 1)
                << " maxWrapMs=" << juce::String(maxWrapBlockMs, 2)
                << " maxRegularMs=" << juce::String(maxRegularBlockMs, 2);
        logMessage(metrics);

        const auto [minAttack, maxAttack] =
            std::minmax_element(attackPeaks.begin() + 1, attackPeaks.end());
        expect(*maxAttack > 0.01f, "Loop-head transient should remain audible");
        expect(*minAttack >= *maxAttack * 0.98f,
               "Consecutive loop-head attacks should not alternate in strength");
    }
};

static SignalsmithLoopTest signalsmithLoopTest;
