// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/ui/components/mixer/RoutingSyncHelper.hpp"

/**
 * A track's MIDI Out selector shows where that track sends, and nothing else (#2209).
 *
 * It used to show the first track taking this one as its MIDI input, which hid every other
 * track doing the same.
 */

using namespace magda;

namespace {

class MidiOutSelectorTest final : public juce::UnitTest {
  public:
    MidiOutSelectorTest() : juce::UnitTest("MIDI Out Selector Tests", "magda") {}

    void runTest() override {
        test::runWithCleanJuceState([this] { testShowsOnlyItsOwnRoute(); });
    }

  private:
    /// The track the selector shows, or INVALID_TRACK_ID for None or a port.
    static TrackId shownTrack(TrackId trackId) {
        RoutingSelector midiOut(RoutingSelector::Type::MidiOut);
        std::map<int, TrackId> outputs;
        std::map<int, TrackId> midiOutputs;
        const auto* track = TrackManager::getInstance().getTrack(trackId);
        RoutingSyncHelper::syncSelectorsFromTrack(*track, nullptr, nullptr, nullptr, &midiOut,
                                                  nullptr, trackId, outputs, midiOutputs);
        const auto shown = midiOutputs.find(midiOut.getSelectedId());
        return shown == midiOutputs.end() ? INVALID_TRACK_ID : shown->second;
    }

    void testShowsOnlyItsOwnRoute() {
        beginTest("Tracks listening to a track are not its MIDI Out");

        auto& tracks = TrackManager::getInstance();
        const auto source = tracks.createTrack("Source", TrackType::Media);
        const auto first = tracks.createTrack("First", TrackType::Media);
        const auto second = tracks.createTrack("Second", TrackType::Media);
        const auto destination = tracks.createTrack("Destination", TrackType::Media);

        const auto sourceRoute = "track:" + juce::String(source);
        tracks.setTrackMidiInput(first, sourceRoute);
        tracks.setTrackMidiInput(second, sourceRoute);
        expectEquals(shownTrack(source), INVALID_TRACK_ID, "listeners are not shown as its route");

        tracks.routeMidiOutputToTrack(source, destination);
        expectEquals(shownTrack(source), destination, "its own route is");
        expect(tracks.getTrack(first)->midiInputDevice == sourceRoute, "first still listens");
        expect(tracks.getTrack(second)->midiInputDevice == sourceRoute, "second still listens");
    }
};

MidiOutSelectorTest midiOutSelectorTest;

}  // namespace
