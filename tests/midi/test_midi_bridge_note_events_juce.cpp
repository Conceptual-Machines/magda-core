#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>
#include <ranges>
#include <vector>

#include "SharedTestEngine.hpp"
#include "magda/daw/audio/MidiBridge.hpp"
#include "magda/daw/core/MidiTypes.hpp"

namespace te = tracktion;

// Covers the note-event fan-out shared by the real-MIDI path
// (handleIncomingMidiMessage) and the synthesized QWERTY path
// (broadcastSynthesizedNote). The latter is public, so it exercises the shared
// MidiBridge::notifyNoteEventIfMonitored helper directly: onNoteEvent must fire
// only for tracks that are both routed to the source device and monitored.
class MidiBridgeNoteEventsTest final : public juce::UnitTest {
  public:
    MidiBridgeNoteEventsTest() : juce::UnitTest("MidiBridge Note Events Tests", "magda") {}

    void runTest() override {
        testSynthesizedNoteFiresForMonitoredTrack();
        testSynthesizedNoteSilentWhenNotMonitored();
        testSynthesizedNoteSilentForUnroutedDevice();
        testAllRoutingMatchesSynthesizedNote();
        testForkVirtualInputsAreNotOfferedOnTheNativeEngine();
    }

  private:
    static constexpr magda::TrackId kTrackId = 778801;
    static constexpr const char* kDeviceId = "qwerty-note-events-test-device";

    struct Capture {
        int noteOnCount = 0;
        int noteOffCount = 0;
        magda::TrackId lastTrack = magda::INVALID_TRACK_ID;
        magda::MidiNoteEvent lastEvent{};
    };

    // Installs an onNoteEvent callback that records what it receives.
    static void captureInto(magda::MidiBridge& bridge, Capture& capture) {
        bridge.onNoteEvent = [&capture](magda::TrackId trackId, const magda::MidiNoteEvent& event) {
            capture.lastTrack = trackId;
            capture.lastEvent = event;
            if (event.isNoteOn)
                ++capture.noteOnCount;
            else
                ++capture.noteOffCount;
        };
    }

    /// A sink that records nothing: installing one is what tells MidiBridge the
    /// native engine is the one rendering.
    struct NullSink final : magda::LiveMidiSink {
        void pushMidi(const juce::String&, const juce::MidiMessage&) override {}
        void audition(magda::TrackId, const juce::MidiMessage&) override {}
    };

    void testForkVirtualInputsAreNotOfferedOnTheNativeEngine() {
        beginTest("The fork's virtual MIDI inputs are not offered under the native engine");

        auto& engine = *magda::test::getSharedEngine().getEngine();
        auto& devices = engine.getDeviceManager();

        // The fork's own merge of every input, which is what the selector
        // offered and what nothing feeds here.
        const juce::String merged("All MIDI Ins");
        const auto created = devices.createVirtualMidiDevice(merged);
        expect(created.wasOk(), "The virtual device is created");
        if (!created.wasOk())
            return;

        // By value, so the vector has to outlive the search.
        const auto midiIns = devices.getMidiInDevices();
        const auto byName = [&merged](const auto& dev) { return dev->getName() == merged; };
        const auto found = std::ranges::find_if(midiIns, byName);

        auto virtualDevice = found != midiIns.end() ? *found : nullptr;
        expect(virtualDevice != nullptr, "And enumerated by the engine");
        if (virtualDevice == nullptr)
            return;
        virtualDevice->setEnabled(true);

        magda::MidiBridge bridge(engine);
        bridge.setQwertyEnabled(true);

        const auto names = [](const std::vector<magda::MidiDeviceInfo>& list) {
            return list | std::views::transform([](const auto& d) { return d.name; }) |
                   std::ranges::to<std::vector>();
        };

        expect(std::ranges::contains(names(bridge.getAvailableMidiInputs()), merged),
               "The fork offers it while the fork renders");

        NullSink sink;
        bridge.setLiveSink(&sink);

        const auto listed = bridge.getAvailableMidiInputs();
        bridge.setLiveSink(nullptr);

        expect(!std::ranges::contains(names(listed), merged),
               "The native engine does not, since a route to it is silence");

        const auto isKeyboard = [](const magda::MidiDeviceInfo& device) {
            return device.id == magda::qwertyMidiDeviceId();
        };
        expect(std::ranges::any_of(listed, isKeyboard),
               "The keyboard is still there, under the id it pushes under");

        if (auto* toDelete = dynamic_cast<te::VirtualMidiInputDevice*>(virtualDevice.get()))
            devices.deleteVirtualMidiDevice(*toDelete);
    }

    void testSynthesizedNoteFiresForMonitoredTrack() {
        beginTest("Synthesized note fires onNoteEvent for a monitored, routed track");

        magda::MidiBridge bridge(*magda::test::getSharedEngine().getEngine());
        bridge.setTrackMidiInput(kTrackId, kDeviceId);
        bridge.startMonitoring(kTrackId);

        Capture capture;
        captureInto(bridge, capture);

        bridge.broadcastSynthesizedNote(kDeviceId, 60, 100, /*isNoteOn=*/true);
        expectEquals(capture.noteOnCount, 1, "Note-on should fire exactly once");
        expectEquals(static_cast<int>(capture.lastTrack), static_cast<int>(kTrackId),
                     "Callback should carry the routed track id");
        expectEquals(capture.lastEvent.noteNumber, 60, "Note number should be forwarded");
        expectEquals(capture.lastEvent.velocity, 100, "Velocity should be forwarded");
        expect(capture.lastEvent.isNoteOn, "Event should be flagged as note-on");

        bridge.broadcastSynthesizedNote(kDeviceId, 60, 0, /*isNoteOn=*/false);
        expectEquals(capture.noteOffCount, 1, "Note-off should fire exactly once");
        expect(!capture.lastEvent.isNoteOn, "Final event should be flagged as note-off");
    }

    void testSynthesizedNoteSilentWhenNotMonitored() {
        beginTest("Synthesized note does not fire when the track is not monitored");

        magda::MidiBridge bridge(*magda::test::getSharedEngine().getEngine());
        bridge.setTrackMidiInput(kTrackId, kDeviceId);
        // Deliberately no startMonitoring(): routed but not monitored.

        Capture capture;
        captureInto(bridge, capture);

        bridge.broadcastSynthesizedNote(kDeviceId, 64, 100, /*isNoteOn=*/true);
        expectEquals(capture.noteOnCount, 0, "Unmonitored track must not receive note callbacks");

        // And once monitoring stops mid-session, callbacks stop too.
        bridge.startMonitoring(kTrackId);
        bridge.broadcastSynthesizedNote(kDeviceId, 64, 100, /*isNoteOn=*/true);
        expectEquals(capture.noteOnCount, 1, "Monitoring should re-enable callbacks");
        bridge.stopMonitoring(kTrackId);
        bridge.broadcastSynthesizedNote(kDeviceId, 64, 0, /*isNoteOn=*/false);
        expectEquals(capture.noteOffCount, 0, "Stopping monitoring should silence callbacks");
    }

    void testSynthesizedNoteSilentForUnroutedDevice() {
        beginTest("Synthesized note does not fire when the device is not routed to the track");

        magda::MidiBridge bridge(*magda::test::getSharedEngine().getEngine());
        bridge.setTrackMidiInput(kTrackId, kDeviceId);
        bridge.startMonitoring(kTrackId);

        Capture capture;
        captureInto(bridge, capture);

        bridge.broadcastSynthesizedNote("some-other-device", 67, 100, /*isNoteOn=*/true);
        expectEquals(capture.noteOnCount, 0,
                     "A non-matching source device must not fire callbacks");
    }

    void testAllRoutingMatchesSynthesizedNote() {
        beginTest("\"all\" input routing matches any synthesized source device");

        magda::MidiBridge bridge(*magda::test::getSharedEngine().getEngine());
        bridge.setTrackMidiInput(kTrackId, "all");
        bridge.startMonitoring(kTrackId);

        Capture capture;
        captureInto(bridge, capture);

        bridge.broadcastSynthesizedNote(kDeviceId, 72, 90, /*isNoteOn=*/true);
        expectEquals(capture.noteOnCount, 1, "\"all\" routing should match any source device");
        expectEquals(capture.lastEvent.noteNumber, 72,
                     "Note number should be forwarded under \"all\"");
    }
};

static MidiBridgeNoteEventsTest midiBridgeNoteEventsTest;
