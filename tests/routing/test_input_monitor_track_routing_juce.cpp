// Regression tests for #1690: cycling a destination track's input Monitor
// (Off/In/Auto) while it takes another track's output as its audio input
// ("track:N" routing).
//
// Each Monitor edge flips te::InputDeviceInstance::isLivePlayEnabled() for the
// destination track, which drives two engine-side paths that used to break:
//   1. AudioBridge::refreshInputMeterClients() dropped its LevelMeasurer
//      client without unregistering it from the (still-alive) source track's
//      WaveInputDevice::levelMeasurer, leaving a dangling Client* that the
//      audio graph wrote through on every block (heap corruption / livelock).
//   2. Each monitor change applied te::InputDevice::setMonitorMode()
//      synchronously from inside trackPropertyChanged, so a burst of queued
//      monitor commands stacked TransportControl::restartAllTransports()
//      calls against the running graph.
//
// These tests drive the full path (TrackManager -> AudioBridge ->
// MidiInputRouter resync -> te::InputDevice::setMonitorMode) with the
// transport playing and the message loop pumping (30 Hz metering timer +
// coalesced async resync both run on it).

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <tracktion_engine/tracktion_engine.h>

#include "JuceTestStateGuard.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/TracktionEngineWrapper.hpp"

using namespace magda;
namespace te = tracktion;

namespace {

void pumpMessageLoop(int ms) {
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
        mm->runDispatchLoopUntil(ms);
}

}  // namespace

class InputMonitorTrackRoutingTest final : public juce::UnitTest {
  public:
    InputMonitorTrackRoutingTest() : juce::UnitTest("Input Monitor Track Routing Tests", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { testMonitorCyclingOnTrackInput(); });
    }

  private:
    void testMonitorCyclingOnTrackInput() {
        beginTest("Monitor cycling on a track-as-input applies device modes and survives bursts");

        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        auto* edit = wrapper.getEdit();
        expect(bridge != nullptr, "AudioBridge must exist");
        expect(edit != nullptr, "Tracktion edit must exist");
        if (bridge == nullptr || edit == nullptr)
            return;

        auto& tm = TrackManager::getInstance();
        const auto srcId = tm.createTrack("Route Source");
        const auto dstId = tm.createTrack("Route Dest");
        bridge->createAudioTrack(srcId, "Route Source");
        bridge->createAudioTrack(dstId, "Route Dest");

        auto& transport = edit->getTransport();
        transport.ensureContextAllocated();
        // Best-effort: in the headless test environment the transport does not
        // reliably report isPlaying(), but play() still allocates and runs the
        // playback context so monitor changes restart a live graph.
        transport.play(false);
        pumpMessageLoop(60);

        // Route dest's audio input from the source track.
        const auto inputSpec = "track:" + juce::String(srcId);
        tm.setTrackAudioInput(dstId, inputSpec);
        bridge->setTrackAudioInput(dstId, inputSpec);
        pumpMessageLoop(60);

        auto* srcTrack = bridge->getAudioTrack(srcId);
        expect(srcTrack != nullptr, "Source TE track must exist");
        if (srcTrack == nullptr)
            return;
        auto& sourceDevice = srcTrack->getWaveInputDevice();

        // Several full cycles with message-loop pumps in between. Each In->Off
        // edge exercises the input-meter client add/remove transition in
        // AudioBridge::refreshInputMeterClients() (the former use-after-free),
        // and each real mode change restarts the transports mid-playback.
        for (int i = 0; i < 3; ++i) {
            tm.setTrackInputMonitor(dstId, InputMonitorMode::In);
            pumpMessageLoop(80);
            expect(sourceDevice.getMonitorMode() == te::InputDevice::MonitorMode::on,
                   "Monitor In must map to device mode 'on'");

            tm.setTrackInputMonitor(dstId, InputMonitorMode::Auto);
            pumpMessageLoop(80);
            expect(sourceDevice.getMonitorMode() == te::InputDevice::MonitorMode::automatic,
                   "Monitor Auto must map to device mode 'automatic'");

            tm.setTrackInputMonitor(dstId, InputMonitorMode::Off);
            pumpMessageLoop(80);
            expect(sourceDevice.getMonitorMode() == te::InputDevice::MonitorMode::off,
                   "Monitor Off must map to device mode 'off'");
        }

        // Burst: rapid changes queued in a single message-loop drain (as when
        // clicks pile up behind a graph rebuild) must coalesce to one
        // application of the final state.
        tm.setTrackInputMonitor(dstId, InputMonitorMode::In);
        tm.setTrackInputMonitor(dstId, InputMonitorMode::Off);
        tm.setTrackInputMonitor(dstId, InputMonitorMode::In);
        tm.setTrackInputMonitor(dstId, InputMonitorMode::Off);
        tm.setTrackInputMonitor(dstId, InputMonitorMode::Auto);
        pumpMessageLoop(120);
        expect(sourceDevice.getMonitorMode() == te::InputDevice::MonitorMode::automatic,
               "A burst of monitor changes must settle on the final mode");

        // Drop the routing while the source device's measurer is still alive;
        // the meter client for dst must be unregistered cleanly on the next
        // metering ticks (no dangling client left behind).
        tm.setTrackAudioInput(dstId, {});
        bridge->setTrackAudioInput(dstId, {});
        pumpMessageLoop(120);

        transport.stop(false, false);
    }
};

static InputMonitorTrackRoutingTest inputMonitorTrackRoutingTest;
