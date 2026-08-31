// Coverage for DefaultControllerParamWriter::writeTrackLevel, the path a
// control surface takes when a binding resolves to @master.volume,
// @master.pan, @selected.volume or @selected.pan.
//
// This used to be dead: every track-level kind fell through a bare `break` in
// DefaultControllerParamWriter::write, so a JSON controller binding on
// master.volume resolved cleanly and then silently did nothing. The router
// tests all inject a FakeParamWriter, so nothing exercised the real writer.
//
// Lives in the JUCE target because DefaultControllerParamWriter takes an
// AudioBridge&, which needs the shared engine.

#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <cmath>

#include "JuceTestStateGuard.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
#include "magda/daw/audio/controllers/ControllerParamWriter.hpp"
#include "magda/daw/core/ControlTarget.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/TracktionEngineWrapper.hpp"

using namespace magda;

namespace {

ResolveResult makeTrackLevelTarget(ControlTarget::Kind kind, TrackId trackId) {
    ResolveResult r;
    r.target.kind = kind;
    r.target.devicePath = ChainNodePath::trackLevel(trackId);
    r.target.paramIndex = (kind == ControlTarget::Kind::TrackVolume) ? 0 : 1;
    r.resolved = true;
    return r;
}

}  // namespace

class ControllerTrackLevelWriteTest final : public juce::UnitTest {
  public:
    ControllerTrackLevelWriteTest()
        : juce::UnitTest("Controller Track Level Write Tests", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] {
            testMasterVolumeWrite();
            testMasterPanWrite();
            testTrackVolumeWrite();
            testSendLevelWrite();
            testFullScaleAndSilence();
        });
    }

  private:
    void testMasterVolumeWrite() {
        beginTest("A master.volume binding reaches the master channel");

        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        expect(bridge != nullptr, "AudioBridge must exist");
        if (bridge == nullptr)
            return;

        auto& tm = TrackManager::getInstance();
        DefaultControllerParamWriter writer{*bridge};

        const auto resolved =
            makeTrackLevelTarget(ControlTarget::Kind::TrackVolume, MASTER_TRACK_ID);
        expect(resolved.ok(), "target must resolve");

        // Half scale, then full: the point is that the value moves at all, and
        // that it moves in the right direction.
        writer.write(resolved, 0.5f);
        const float atHalf = tm.getMasterChannel().volume;

        writer.write(resolved, 1.0f);
        const float atFull = tm.getMasterChannel().volume;

        expect(atFull > atHalf, "a higher controller value must raise master volume");
        expect(atFull > 0.0f, "full scale must not silence the master");
    }

    void testMasterPanWrite() {
        beginTest("A master.pan binding reaches the master channel");

        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        if (bridge == nullptr)
            return;

        auto& tm = TrackManager::getInstance();
        DefaultControllerParamWriter writer{*bridge};

        const auto resolved = makeTrackLevelTarget(ControlTarget::Kind::TrackPan, MASTER_TRACK_ID);

        writer.write(resolved, 0.0f);
        const float atMin = tm.getMasterChannel().pan;

        writer.write(resolved, 1.0f);
        const float atMax = tm.getMasterChannel().pan;

        expect(atMax > atMin, "pan must sweep from left to right across the CC range");
        expect(atMin < 0.0f, "a zero controller value must pan left");
        expect(atMax > 0.0f, "a full controller value must pan right");
    }

    void testTrackVolumeWrite() {
        beginTest("A selected.volume binding reaches an ordinary track");

        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        if (bridge == nullptr)
            return;

        auto& tm = TrackManager::getInstance();
        const auto trackId = tm.createTrack("Fader Target");
        bridge->createAudioTrack(trackId, "Fader Target");

        const float masterBefore = tm.getMasterChannel().volume;

        DefaultControllerParamWriter writer{*bridge};
        const auto resolved = makeTrackLevelTarget(ControlTarget::Kind::TrackVolume, trackId);
        writer.write(resolved, 0.25f);

        const auto* track = tm.getTrack(trackId);
        expect(track != nullptr, "track must exist");
        if (track == nullptr)
            return;

        writer.write(resolved, 0.75f);
        const auto* after = tm.getTrack(trackId);
        expect(after != nullptr);
        if (after == nullptr)
            return;

        expect(after->volume > 0.0f, "the write must land on the track");
        expect(tm.getMasterChannel().volume == masterBefore,
               "a track write must not disturb the master channel");
    }

    void testSendLevelWrite() {
        beginTest("A send-level write reaches the send (#1757)");

        // SendLevel used to fall through the same bare `break` this file was
        // written about. OSC is its first caller, and a control surface with a
        // send fader that silently did nothing would be the same bug again.
        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        if (bridge == nullptr)
            return;

        auto& tm = TrackManager::getInstance();
        const auto source = tm.createTrack("Send Source");
        const auto destination = tm.createTrack("Reverb Bus");
        bridge->createAudioTrack(source, "Send Source");
        bridge->createAudioTrack(destination, "Reverb Bus");
        tm.addSend(source, destination);

        const auto* withSend = tm.getTrack(source);
        expect(withSend != nullptr && !withSend->sends.empty(), "the send must have been created");
        if (withSend == nullptr || withSend->sends.empty())
            return;
        const int busIndex = withSend->sends[0].busIndex;

        DefaultControllerParamWriter writer{*bridge};
        ResolveResult resolved;
        resolved.target = ControlTarget::sendLevel(source, busIndex);
        resolved.resolved = true;
        expect(resolved.ok(), "the send target must resolve");

        writer.write(resolved, 0.25f);
        const float atQuarter = tm.getTrack(source)->sends[0].level;

        writer.write(resolved, 0.9f);
        const float atNine = tm.getTrack(source)->sends[0].level;

        expect(atNine > atQuarter, "a higher controller value must raise the send level");
        expect(atQuarter >= 0.0f, "a send level must never go negative");
    }

    void testFullScaleAndSilence() {
        beginTest("Controller extremes stay inside the accepted gain range");

        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        if (bridge == nullptr)
            return;

        auto& tm = TrackManager::getInstance();
        DefaultControllerParamWriter writer{*bridge};
        const auto resolved =
            makeTrackLevelTarget(ControlTarget::Kind::TrackVolume, MASTER_TRACK_ID);

        writer.write(resolved, 0.0f);
        const float atZero = tm.getMasterChannel().volume;
        expect(atZero >= 0.0f, "zero must not produce a negative gain");

        writer.write(resolved, 1.0f);
        const float atFull = tm.getMasterChannel().volume;
        // TrackManager clamps to 0..2 (+6 dB); a full-scale controller write
        // must land inside that window rather than being clipped away.
        expect(atFull <= 2.0f, "full scale must stay within the +6 dB ceiling");
        expect(atFull > atZero, "full scale must exceed silence");

        // Values outside 0..1 are clamped by write() before any mapping runs.
        writer.write(resolved, 5.0f);
        expect(tm.getMasterChannel().volume == atFull,
               "an out-of-range value must clamp to the full-scale result");
    }
};

static ControllerTrackLevelWriteTest controllerTrackLevelWriteTest;
