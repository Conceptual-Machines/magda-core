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
    }

  private:
    void testAuxIsTheOnlyTypeWithoutARow() {
        beginTest("Aux is the only track type without an arrangement row");

        expect(!occupiesArrangementRow(TrackType::Aux), "aux belongs in its own strip");

        for (const auto type : {TrackType::Audio, TrackType::Group, TrackType::Chord,
                                TrackType::Master, TrackType::MultiOut}) {
            expect(occupiesArrangementRow(type),
                   "every non-aux type keeps its row: " + juce::String(static_cast<int>(type)));
        }
    }

    void testContentColumnGivesNoLaneToAux() {
        beginTest("The content column gives an aux return no lane");

        test::resetJuceProjectState();
        auto& trackManager = TrackManager::getInstance();
        trackManager.createTrack("Audio", TrackType::Audio);
        trackManager.createTrack("Send", TrackType::Aux);
        trackManager.createTrack("Below", TrackType::Audio);

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
        trackManager.createTrack("First", TrackType::Audio);
        trackManager.createTrack("Second", TrackType::Audio);

        TrackContentPanel withoutAux;
        withoutAux.setSize(2000, 400);
        const int secondRowY = withoutAux.getTrackYPosition(1);
        expect(secondRowY > 0, "the second lane should sit below the first");

        // The same two audio tracks, with an aux return created between them.
        // Its row must not exist, so the second audio track keeps its position —
        // which is what keeps it level with its header.
        test::resetJuceProjectState();
        trackManager.createTrack("First", TrackType::Audio);
        trackManager.createTrack("Send", TrackType::Aux);
        trackManager.createTrack("Second", TrackType::Audio);

        TrackContentPanel withAux;
        withAux.setSize(2000, 400);
        expectEquals(withAux.getNumTracks(), 2, "aux should not occupy a lane");
        // The last lane, not index 1: with the bug present index 1 *is* the aux
        // row and sits exactly where the second audio track should, so an
        // assertion there passes either way. What moves is the track after it.
        expectEquals(withAux.getTrackYPosition(withAux.getNumTracks() - 1), secondRowY,
                     "an aux return between two tracks must not move the lower one");
    }
};

ArrangementRowAlignmentTest arrangementRowAlignmentTest;

}  // namespace
