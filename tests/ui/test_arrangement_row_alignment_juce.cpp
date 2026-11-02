// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_gui_basics/juce_gui_basics.h>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/ui/components/tracks/TrackContentPanel.hpp"
#include "magda/daw/ui/components/tracks/TrackControlsPolicy.hpp"

/**
 * The arrangement's two columns agree on which tracks have rows.
 *
 * The header column and the content column are separate viewports driven to one
 * scroll offset, so they are only ever aligned because they lay out the same
 * tracks at the same heights. A track present in one and absent from the other
 * shifts everything below it by that track's height, and the columns drift
 * further apart the more you scroll.
 *
 * That is what aux returns did. They gained their own fixed strip below the
 * arrangement, the header column learned to skip them, and the content column
 * kept giving them a lane — so it rendered every aux track twice and pushed the
 * rest of its lanes down. It survived six months because it needs an aux track
 * to exist with something below it, and nothing in the UI creates one by
 * default.
 *
 * `TrackHeadersPanel` is not in this target, so the assertions here are on the
 * shared predicate both columns now consult and on the content column's own
 * row count. Pinning the predicate is what matters: the bug was one condition
 * written in one panel and not the other, and it can only come back by someone
 * reintroducing that split.
 */

using namespace magda;

namespace {

class ArrangementRowAlignmentTest final : public juce::UnitTest {
  public:
    ArrangementRowAlignmentTest() : juce::UnitTest("Arrangement Row Alignment Tests", "magda") {}

    void runTest() override {
        testAuxIsTheOnlyTypeWithoutARow();
        testContentColumnGivesNoLaneToAux();
        testAuxDoesNotDisplaceTheTracksBelowIt();
        testChordTrackLeadsTheRows();
        testSeparatorHasAThinRow();
        testWhatEachTypeExposes();
    }

  private:
    void testAuxIsTheOnlyTypeWithoutARow() {
        beginTest("Aux is the only track type without an arrangement row");

        expect(!occupiesArrangementRow(TrackType::Aux), "aux belongs in its own strip");

        for (const auto type : {TrackType::Media, TrackType::Group, TrackType::Chord,
                                TrackType::Master, TrackType::MultiOut, TrackType::Separator}) {
            expect(occupiesArrangementRow(type),
                   "every non-aux type keeps its row: " + juce::String(static_cast<int>(type)));
        }
    }

    void testContentColumnGivesNoLaneToAux() {
        beginTest("The content column gives an aux return no lane");

        test::resetJuceProjectState();
        auto& trackManager = TrackManager::getInstance();
        trackManager.createTrack("Audio", TrackType::Media);
        trackManager.createTrack("Send", TrackType::Aux);
        trackManager.createTrack("Below", TrackType::Media);

        TrackContentPanel panel;
        panel.setSize(2000, 400);

        // Three tracks exist; two of them have lanes here. The aux return is
        // rendered by MainView's aux strip instead.
        expectEquals(panel.getNumTracks(), 2, "aux should not occupy a lane");
    }

    void testAuxDoesNotDisplaceTheTracksBelowIt() {
        beginTest("An aux return does not push the lanes below it down");

        test::resetJuceProjectState();
        auto& trackManager = TrackManager::getInstance();
        trackManager.createTrack("First", TrackType::Media);
        trackManager.createTrack("Second", TrackType::Media);

        TrackContentPanel withoutAux;
        withoutAux.setSize(2000, 400);
        const int secondRowY = withoutAux.getTrackYPosition(1);
        expect(secondRowY > 0, "the second lane should sit below the first");

        // The same two audio tracks, with an aux return created between them.
        // Its row must not exist, so the second audio track keeps its position —
        // which is what keeps it level with its header.
        test::resetJuceProjectState();
        trackManager.createTrack("First", TrackType::Media);
        trackManager.createTrack("Send", TrackType::Aux);
        trackManager.createTrack("Second", TrackType::Media);

        TrackContentPanel withAux;
        withAux.setSize(2000, 400);
        expectEquals(withAux.getNumTracks(), 2, "aux should not occupy a lane");
        // The last lane, not index 1: with the bug present index 1 *is* the aux
        // row and sits exactly where the second audio track should, so an
        // assertion there passes either way. What moves is the track after it.
        expectEquals(withAux.getTrackYPosition(withAux.getNumTracks() - 1), secondRowY,
                     "an aux return between two tracks must not move the lower one");
    }

    void testWhatEachTypeExposes() {
        beginTest("Each track type exposes its own controls and no others");

        // #3228: volume and the audition control, nothing else.
        const auto chord = TrackControlsPolicy::forType(TrackType::Chord);
        expect(chord.gain && chord.mute, "volume and the audition control");
        expect(chord.muteStyle == TrackControlsPolicy::MuteStyle::ChordAudition);
        expect(!chord.pan && !chord.solo && !chord.record && !chord.monitor && !chord.automation);
        expect(!chord.anyRouting() && !chord.sends && !chord.meter);

        for (const auto type : {TrackType::Aux, TrackType::Group}) {
            const auto bus = TrackControlsPolicy::forType(type);
            expect(!bus.record && !bus.monitor, "a bus has no input to record or monitor");
            expect(bus.gain && bus.pan && bus.mute && bus.solo);
        }

        const auto aux = TrackControlsPolicy::forType(TrackType::Aux);
        expect(!aux.anyRouting() && !aux.sends, "an aux return has no routing or sends");

        const auto media = TrackControlsPolicy::forType(TrackType::Media);
        expect(media.record && media.monitor, "the regular track keeps them");
        expect(media.anyRouting() && media.sends);

        const auto separator = TrackControlsPolicy::forType(TrackType::Separator);
        expect(!separator.gain && !separator.mute && !separator.meter, "a separator has none");
    }

    void testChordTrackLeadsTheRows() {
        beginTest("The chord track has the top row wherever it sits in the track list");

        test::resetJuceProjectState();
        auto& trackManager = TrackManager::getInstance();
        const auto first = trackManager.createTrack("First", TrackType::Media);
        const auto chord = trackManager.createTrack("", TrackType::Chord);
        const auto second = trackManager.createTrack("Second", TrackType::Media);

        TrackContentPanel panel;
        panel.setSize(2000, 400);

        const auto& rows = panel.getVisibleTrackIds();
        expect(rows == std::vector<TrackId>{chord, first, second}, "chord should lead the rows");
        expect(trackManager.getTracks().front().id == first, "the track list keeps its order");
    }

    void testSeparatorHasAThinRow() {
        beginTest("A separator's row is thin and only a little resizable");

        test::resetJuceProjectState();
        auto& trackManager = TrackManager::getInstance();
        trackManager.createTrack("First", TrackType::Media);
        const auto separator = trackManager.createTrack("", TrackType::Separator);

        TrackContentPanel panel;
        panel.setSize(2000, 400);

        expectEquals(panel.getNumTracks(), 2, "a separator has a row");
        expectEquals(panel.getTrackHeight(1), kSeparatorRowHeight, "thin to start with");

        // Row heights above its range are refused, in the model and in the lane.
        trackManager.setTrackHeight(separator, ViewMode::Arrange, 200);
        panel.setTrackHeight(1, 200);
        const auto* track = trackManager.getTrack(separator);
        expectEquals(track->viewSettings.getHeight(ViewMode::Arrange), kMaxSeparatorRowHeight);
        expectEquals(panel.getTrackHeight(1), kMaxSeparatorRowHeight);

        // Below a regular track's minimum is its own range.
        trackManager.setTrackHeight(separator, ViewMode::Arrange, kSeparatorRowHeight);
        panel.setTrackHeight(1, kSeparatorRowHeight);
        expectEquals(panel.getTrackHeight(1), kSeparatorRowHeight);
    }
};

ArrangementRowAlignmentTest arrangementRowAlignmentTest;

}  // namespace
