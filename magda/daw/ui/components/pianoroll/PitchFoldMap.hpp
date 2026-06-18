#pragma once

#include <array>
#include <vector>

namespace magda {

/**
 * Shared discrete vertical-axis map for the piano roll.
 *
 * Unfolded, it is the identity over [kMinNote, kMaxNote]: row = kMaxNote - note,
 * i.e. the highest pitch at row 0, exactly matching the linear 0..127 axis the
 * grid/keyboard/octave-strip used before fold existed.
 *
 * Folded, it collapses the axis to only the supplied pitches (one row each,
 * highest pitch at row 0), so dense parts read cleanly and empty octaves
 * disappear. The grid, keyboard, and octave-label strip all route their
 * pitch<->row math through one shared instance so their axes stay aligned.
 *
 * Built from an arbitrary pitch set so MIDI take lanes / comping can later feed
 * it the union of every take's pitches.
 */
class PitchFoldMap {
  public:
    static constexpr int kMinNote = 0;
    static constexpr int kMaxNote = 127;
    static constexpr int kFullRowCount = kMaxNote - kMinNote + 1;  // 128

    /** Replace the used-pitch set (any order; deduped + sorted internally). */
    void rebuild(const std::vector<int>& usedPitches);

    void setEnabled(bool enabled) {
        enabled_ = enabled;
    }
    bool isEnabled() const {
        return enabled_;
    }

    /**
     * Folding kicks in with >= 1 distinct used pitch (a single note collapses to
     * one row). With 0 used pitches there's nothing to collapse, so the map
     * behaves as the plain 0..127 axis and the Fold toggle is a no-op.
     */
    bool isActive() const {
        return enabled_ && !rowsDescending_.empty();
    }

    int rowCount() const {
        return isActive() ? static_cast<int>(rowsDescending_.size()) : kFullRowCount;
    }

    /** Pitch -> row (0 = top). Folded-out pitches snap to the nearest used row. */
    int rowForNote(int note) const;

    /** Row -> pitch (inverse of rowForNote on valid rows). */
    int noteForRow(int row) const;

  private:
    bool enabled_ = false;
    std::vector<int> rowsDescending_;             // used pitches, high -> low (row order)
    std::array<int, kFullRowCount> noteToRow_{};  // nearest-row lookup, valid when active
};

}  // namespace magda
