#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "../core/AutomationManager.hpp"

namespace magda {

/**
 * @brief Keeps the edit-scoped Tempo automation lane and te::Edit::tempoSequence
 *        in sync, in both directions, so tempoSequence stays the single source
 *        of truth.
 *
 * - Lane edited (point added/moved/removed) -> reconcile into tempoSequence.
 * - tempoSequence changed (transport slider, undo, anything) -> rebuild the
 *   lane's points from it.
 *
 * A single `applying_` guard breaks the echo loop: every write we make fires
 * the opposite listener synchronously, which the guard ignores.
 *
 * Attaches lazily: if no Tempo lane exists yet it stays inert, then engages the
 * moment one appears (automationLanesChanged). Lives on the message thread.
 */
class TempoLaneSync : public AutomationManagerListener, private juce::ValueTree::Listener {
  public:
    explicit TempoLaneSync(tracktion::Edit& edit);
    ~TempoLaneSync() override;

    // AutomationManagerListener
    void automationLanesChanged() override;
    void automationPointsChanged(AutomationLaneId laneId) override;

  private:
    void resolveTempoLane();
    void syncLaneToSequence();  // lane points -> tempoSequence
    void syncSequenceToLane();  // tempoSequence -> lane points

    // Called when the tempo lane first becomes available. Picks the sync
    // direction: a lane restored from a saved project carries the real tempo
    // curve and is authoritative (lane -> sequence); a lane freshly created in
    // the UI has a single default point and should be seeded from the current
    // sequence (sequence -> lane).
    void reconcileAppearingTempoLane();

    // juce::ValueTree::Listener (tempoSequence state subtree)
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged(juce::ValueTree&) override {}

    tracktion::Edit& edit_;
    // Non-const handle onto tempoSequence's shared state (getState() is const).
    // ValueTree is a ref-counted handle, so this observes the same tree.
    juce::ValueTree tempoState_;
    AutomationLaneId tempoLaneId_ = INVALID_AUTOMATION_LANE_ID;
    bool applying_ = false;
};

}  // namespace magda
