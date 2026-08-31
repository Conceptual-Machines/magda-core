// Regression tests for #1690: live recording-preview events for track-routed
// MIDI input ("track:N").
//
// Track-sourced MIDI deliberately bypasses MidiBridge's juce::MidiInputCallback
// layer (it flows engine-side: source track output -> TrackMidiInputDeviceNode
// -> te::MidiInputDevice::sendMessageToInstances), so the RecPreview queue used
// to stay empty during a record pass. MidiInputRouter now registers a
// te::InputDeviceInstance::Consumer on the source track's MIDI input instance
// and pushes RecordingNoteEvents for armed destination tracks into a dedicated
// lock-free queue.
//
// These tests drive the real registration path (MidiInputRouter::
// syncTrackMidiPreviewConsumers via route/arm changes + the AudioBridge tick)
// and inject MIDI at the te::MidiInputDevice level — the exact entry point the
// audio-graph node uses — so consumer fan-out is exercised without needing a
// real audio device.

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <tracktion_engine/tracktion_engine.h>

#include "JuceTestStateGuard.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
#include "magda/daw/audio/midi/RecordingNoteQueue.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/TracktionEngineWrapper.hpp"

using namespace magda;
namespace te = tracktion;

namespace {

void pumpMessageLoop(int ms) {
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
        mm->runDispatchLoopUntil(ms);
}

int drainQueue(RecordingNoteQueue& queue, std::vector<RecordingNoteEvent>& out) {
    RecordingNoteEvent evt;
    int count = 0;
    while (queue.pop(evt)) {
        out.push_back(evt);
        ++count;
    }
    return count;
}

}  // namespace

class TrackMidiRecordingPreviewTest final : public juce::UnitTest {
  public:
    TrackMidiRecordingPreviewTest()
        : juce::UnitTest("Track MIDI Recording Preview Tests", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { testConsumerLifecycle(); });
    }

  private:
    void testConsumerLifecycle() {
        beginTest("Track-routed MIDI feeds the recording preview queue for armed targets");

        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        auto* edit = wrapper.getEdit();
        expect(bridge != nullptr, "AudioBridge must exist");
        expect(edit != nullptr, "Tracktion edit must exist");
        if (bridge == nullptr || edit == nullptr)
            return;

        auto& tm = TrackManager::getInstance();
        const auto srcId = tm.createTrack("Midi Source");
        const auto dstId = tm.createTrack("Midi Dest");
        bridge->createAudioTrack(srcId, "Midi Source");
        bridge->createAudioTrack(dstId, "Midi Dest");

        edit->getTransport().ensureContextAllocated();
        pumpMessageLoop(60);

        // Redirect the router's preview queue to a local one we can inspect.
        RecordingNoteQueue queue;
        std::atomic<double> transportSeconds{2.5};
        bridge->setTrackMidiRecordingQueue(&queue, &transportSeconds);

        // Arm the destination, then route its MIDI input from the source track.
        tm.setTrackRecordArmed(dstId, true);
        const auto inputSpec = "track:" + juce::String(srcId);
        tm.setTrackMidiInput(dstId, inputSpec);
        bridge->setTrackMidiInput(dstId, inputSpec);
        pumpMessageLoop(120);

        auto* srcTrack = bridge->getAudioTrack(srcId);
        expect(srcTrack != nullptr, "Source TE track must exist");
        if (srcTrack == nullptr)
            return;
        auto& sourceDevice = srcTrack->getMidiInputDevice();

        // Inject note on/off at the device level — the same entry point
        // TrackMidiInputDeviceNode uses on the audio thread.
        auto sendNotePair = [&sourceDevice]() {
            sourceDevice.handleIncomingMidiMessage(
                juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)),
                sourceDevice.getMPESourceID());
            sourceDevice.handleIncomingMidiMessage(juce::MidiMessage::noteOff(1, 60),
                                                   sourceDevice.getMPESourceID());
        };

        std::vector<RecordingNoteEvent> events;
        sendNotePair();
        drainQueue(queue, events);

        expectEquals(static_cast<int>(events.size()), 2,
                     "Armed track-routed destination must receive note on + off");
        if (events.size() == 2) {
            expectEquals(events[0].trackId, static_cast<int>(dstId),
                         "Event must carry the destination track id");
            expectEquals(events[0].noteNumber, 60, "Note number must match");
            expect(events[0].isNoteOn, "First event must be the note-on");
            expectEquals(events[0].velocity, 100, "Velocity must match");
            expectWithinAbsoluteError(events[0].transportSeconds, 2.5, 1e-9);
            expect(!events[1].isNoteOn, "Second event must be the note-off");
        }

        // Instance recreation: a monitor-mode change restarts the transports,
        // which tears down and recreates every InputDeviceInstance. The tick
        // must re-register the consumer on the fresh instance.
        tm.setTrackInputMonitor(dstId, InputMonitorMode::In);
        pumpMessageLoop(150);

        events.clear();
        sendNotePair();
        drainQueue(queue, events);
        expectEquals(static_cast<int>(events.size()), 2,
                     "Consumer must survive input-instance recreation (monitor change)");

        // Disarming the destination must stop preview pushes.
        tm.setTrackRecordArmed(dstId, false);
        pumpMessageLoop(120);

        events.clear();
        sendNotePair();
        drainQueue(queue, events);
        expectEquals(static_cast<int>(events.size()), 0,
                     "Disarmed destination must not receive preview events");

        // Re-arm, then clear the route entirely: no more events either.
        tm.setTrackRecordArmed(dstId, true);
        pumpMessageLoop(120);
        events.clear();
        sendNotePair();
        drainQueue(queue, events);
        expectEquals(static_cast<int>(events.size()), 2,
                     "Re-armed destination must receive preview events again");

        tm.setTrackMidiInput(dstId, {});
        bridge->setTrackMidiInput(dstId, {});
        pumpMessageLoop(120);

        events.clear();
        sendNotePair();
        drainQueue(queue, events);
        expectEquals(static_cast<int>(events.size()), 0,
                     "Cleared route must not receive preview events");

        // Detach the local queue before it goes out of scope (the router holds
        // raw pointers; later ticks must not touch freed stack memory).
        tm.setTrackRecordArmed(dstId, false);
        bridge->setTrackMidiRecordingQueue(nullptr, nullptr);
        pumpMessageLoop(60);
    }
};

static TrackMidiRecordingPreviewTest trackMidiRecordingPreviewTest;
