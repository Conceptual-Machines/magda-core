#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/engine/host/EngineInsertCapture.hpp"

/// @file The native pass that records hardware inserts for a render (#2279).

namespace {

using magda::InsertRenderCapture;
using magda::daw::engine_host::EngineInsertCapture;
using magda::daw::engine_host::EngineRuntimeFactory;
using magda::daw::engine_host::InsertCaptureHost;
namespace engine = magda::engine;

constexpr double kRate = 1000.0;
constexpr int kBlock = 100;
constexpr float kReturned = 0.5f;
const engine::DeviceKey kKey{magda::ChainSegment::Fx, 7};

/// What the outboard answers with, whatever it was sent.
class Outboard final : public engine::EngineInsert {
  public:
    void send(const engine::BlockInfo&, juce::dsp::AudioBlock<const float>,
              const juce::MidiBuffer&) override {}
    void receive(const engine::BlockInfo&, juce::dsp::AudioBlock<float> audio,
                 juce::MidiBuffer&) override {
        audio.fill(kReturned);
    }
};

/// The live store and transport, with the blocks played by hand.
class Host final : public InsertCaptureHost {
  public:
    std::vector<engine::DeviceKey> capturableInserts() const override {
        return inserts;
    }

    bool rewrapInserts(EngineRuntimeFactory::InsertWrapper wrapper) override {
        held.reset();
        if (wrapper)
            held = wrapper(kKey, std::make_unique<Outboard>());
        return true;
    }

    double liveSampleRate() const override {
        return rate;
    }
    double livePlanLatencySeconds() const override {
        return 0.0;
    }
    bool transportPlaying() const override {
        ++playingAsked;
        return playing;
    }
    double transportSeconds() const override {
        return seconds;
    }
    bool transportLooping() const override {
        return looping;
    }
    void playFrom(double from) override {
        playing = true;
        looping = false;
        seconds = from;
    }
    void stopAt(double at, bool loop) override {
        playing = false;
        seconds = at;
        looping = loop;
    }

    /// One block of the live session through whatever stands where the insert is.
    void play() {
        if (held == nullptr || !playing)
            return;

        engine::BlockInfo block;
        block.numSamples = kBlock;
        block.playing = true;
        block.continuous = true;
        block.seconds = {seconds, seconds + kBlock / kRate};

        juce::AudioBuffer<float> audio(2, kBlock);
        juce::MidiBuffer midi;
        held->receive(block, juce::dsp::AudioBlock<float>(audio), midi);
        seconds = block.seconds.end;
    }

    std::vector<engine::DeviceKey> inserts{kKey};
    std::unique_ptr<engine::EngineInsert> held;
    double rate = kRate;
    bool playing = false;
    bool looping = true;
    double seconds = 5.0;
    mutable int playingAsked = 0;
};

/// The message loop, until @p done says so.
template <typename Done> bool pumpUntil(Done&& done) {
    for (auto tick = 0; tick < 200 && !done(); ++tick)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    return done();
}

}  // namespace

class EngineInsertCaptureTest final : public juce::UnitTest {
  public:
    EngineInsertCaptureTest() : juce::UnitTest("Engine Insert Capture", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { recordsAndPlaysBack(); });
        magda::test::runWithCleanJuceState([this] { stoppingCancels(); });
        magda::test::runWithCleanJuceState([this] { nothingToCaptureIsNotAnError(); });
    }

  private:
    void recordsAndPlaysBack() {
        beginTest("A pass records the return, and a render plays it where the insert was");

        Host host;
        EngineInsertCapture capture(host);
        expect(capture.exportNeedsCapturePass(), "A live insert with a return needs the pass");

        auto finished = false;
        auto succeeded = false;
        expect(capture.startCapturePass(1.0, 2.0, 48000.0,
                                        [&](bool success) {
                                            finished = true;
                                            succeeded = success;
                                        }),
               "The pass starts");
        expect(host.held != nullptr, "and stands in front of the insert");
        expect(host.playing && host.seconds < 1.0, "playing from ahead of the window");

        while (host.seconds < 2.5)
            host.play();

        expect(pumpUntil([&] { return finished; }) && succeeded,
               "The pass finishes once the window is written");
        expect(host.held == nullptr, "and takes itself out of the live session");
        expect(!host.playing && host.seconds == 5.0 && host.looping,
               "and puts the transport back where it was");

        const engine::RenderContext context{
            .sampleRate = 48000.0, .maxBlockSize = 64, .numChannels = 2};
        auto playback =
            capture.playbackFor(kKey, {.startSeconds = 1.0, .endSeconds = 2.0}, context);
        expect(playback != nullptr, "A render gets the recording in the insert's place");
        if (playback == nullptr)
            return;

        engine::BlockInfo block;
        block.numSamples = 64;
        block.playing = true;
        block.seconds = {1.5, 1.5 + 64.0 / 48000.0};
        juce::AudioBuffer<float> audio(2, 64);
        juce::MidiBuffer midi;
        playback->receive(block, juce::dsp::AudioBlock<float>(audio), midi);
        expectWithinAbsoluteError(audio.getSample(0, 32), kReturned, 1.0e-3f,
                                  "at the rate the render asks for");

        expect(capture.playbackFor(kKey, {.startSeconds = 0.0, .endSeconds = 2.0}, context) ==
                   nullptr,
               "but not past what the pass covered");

        capture.cleanupAfterRender();
        expect(capture.playbackFor(kKey, {.startSeconds = 1.0, .endSeconds = 2.0}, context) ==
                   nullptr,
               "and nothing once the render is done");
    }

    void stoppingCancels() {
        beginTest("Stopping the transport mid-pass cancels it");

        Host host;
        EngineInsertCapture capture(host);

        auto finished = false;
        auto succeeded = true;
        capture.startCapturePass(1.0, 2.0, 48000.0, [&](bool success) {
            finished = true;
            succeeded = success;
        });

        // Seen playing first, so what follows is a stop rather than a start not yet made.
        host.play();
        pumpUntil([&] { return host.playingAsked > 0; });
        host.playing = false;

        expect(pumpUntil([&] { return finished; }) && !succeeded,
               "The pass ends without a recording");
        expect(capture.getLastPassError() == InsertRenderCapture::PassError::None,
               "and a cancel is not an error");
    }

    void nothingToCaptureIsNotAnError() {
        beginTest("No insert to capture, or no device to capture on");

        Host host;
        host.inserts.clear();
        EngineInsertCapture capture(host);
        expect(!capture.exportNeedsCapturePass(), "Nothing to capture");
        expect(!capture.startCapturePass(1.0, 2.0, 48000.0, {}), "The pass does not start");
        expect(capture.getLastPassError() == InsertRenderCapture::PassError::None,
               "and that is not an error");

        host.inserts = {kKey};
        host.rate = 0.0;
        expect(!capture.startCapturePass(1.0, 2.0, 48000.0, {}), "No device running");
        expect(capture.getLastPassError() == InsertRenderCapture::PassError::SetupFailed,
               "is a setup failure");
    }
};

static EngineInsertCaptureTest engineInsertCaptureTest;
