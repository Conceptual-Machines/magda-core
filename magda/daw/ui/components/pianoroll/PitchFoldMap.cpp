#include "PitchFoldMap.hpp"

#include <algorithm>

namespace magda {

namespace {
int clampInt(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
}  // namespace

void PitchFoldMap::rebuild(const std::vector<int>& usedPitches) {
    rowsDescending_.clear();

    for (int p : usedPitches) {
        if (p >= kMinNote && p <= kMaxNote)
            rowsDescending_.push_back(p);
    }
    std::sort(rowsDescending_.begin(), rowsDescending_.end(), std::greater<>());
    rowsDescending_.erase(std::unique(rowsDescending_.begin(), rowsDescending_.end()),
                          rowsDescending_.end());

    if (rowsDescending_.empty())
        return;

    // Precompute the nearest used row for every MIDI note so folded-out pitches
    // (e.g. a note dragged toward an empty pitch) snap to a real row. On a tie,
    // prefer the higher pitch (the lower row index).
    for (int note = kMinNote; note <= kMaxNote; ++note) {
        int bestRow = 0;
        int bestDist = std::abs(rowsDescending_[0] - note);
        for (int row = 1; row < static_cast<int>(rowsDescending_.size()); ++row) {
            int dist = std::abs(rowsDescending_[static_cast<size_t>(row)] - note);
            if (dist < bestDist) {
                bestDist = dist;
                bestRow = row;
            }
        }
        noteToRow_[static_cast<size_t>(note)] = bestRow;
    }
}

int PitchFoldMap::rowForNote(int note) const {
    if (!isActive())
        return kMaxNote - clampInt(note, kMinNote, kMaxNote);
    return noteToRow_[static_cast<size_t>(clampInt(note, kMinNote, kMaxNote))];
}

int PitchFoldMap::noteForRow(int row) const {
    if (!isActive())
        return kMaxNote - clampInt(row, 0, kFullRowCount - 1);
    return rowsDescending_[static_cast<size_t>(
        clampInt(row, 0, static_cast<int>(rowsDescending_.size()) - 1))];
}

}  // namespace magda
