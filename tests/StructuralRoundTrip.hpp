#pragma once

#include <juce_core/juce_core.h>

#include <string>

#include "TextDifference.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"

// The oracle every structural edit is measured against (#2221, #2229).
//
// "The device is back" cannot tell a restored model from a plausible one: an id
// allocated on the way out, a link retargeted and not put back, an element
// reinserted at the wrong index are all invisible to it. Comparing the
// serialized track is what catches them, which is why both the hand-written
// cases and the generated matrix compare this and nothing else.

namespace magda::structural_test {

/// Every track, serialized, in order. The comparison subject.
inline juce::String snapshot() {
    juce::Array<juce::var> tracks;
    for (const auto& track : TrackManager::getInstance().getTracks())
        tracks.add(ProjectSerializer::serializeTrackInfo(track));
    return juce::JSON::toString(juce::var(tracks), false);
}

inline void resetState() {
    ClipManager::getInstance().clearAllClips();
    TrackManager::getInstance().clearAllTracks();
    SelectionManager::getInstance().clearSelection();
    UndoManager::getInstance().clearHistory();
}

/// The first line on which two snapshots disagree, or an empty string.
///
/// A failure has to name the field that moved, not print two projects at each
/// other: a snapshot of a handful of tracks runs to thousands of lines, and a
/// sweep that reports one on every cell reports nothing at all.
inline std::string modelDifference(const juce::String& expected, const juce::String& actual) {
    return firstDifference(expected.toStdString(), actual.toStdString());
}

/// Counts what a structural edit announced while it was alive.
///
/// A refused operation has to leave the model alone and say nothing. A
/// notification with no matching mutation sends every listener to rebuild from
/// a model that did not change, which is how a half-applied edit hides.
class NotificationCounter final : public TrackManagerListener {
  public:
    NotificationCounter() {
        TrackManager::getInstance().addListener(this);
    }

    ~NotificationCounter() override {
        TrackManager::getInstance().removeListener(this);
    }

    NotificationCounter(const NotificationCounter&) = delete;
    NotificationCounter& operator=(const NotificationCounter&) = delete;

    void tracksChanged() override {
        ++count_;
    }
    void trackPropertyChanged(int) override {
        ++count_;
    }
    void trackDevicesChanged(TrackId) override {
        ++count_;
    }

    int count() const {
        return count_;
    }

  private:
    int count_ = 0;
};

}  // namespace magda::structural_test
