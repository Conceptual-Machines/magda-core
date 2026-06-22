#include "MidiChordMarkers.hpp"

namespace magda::daw {

std::vector<ChordMarker> readChordMarkers(const juce::MidiFile& midi) {
    std::vector<ChordMarker> markers;

    const int ticksPerQN = midi.getTimeFormat();
    if (ticksPerQN <= 0) {
        // SMPTE timing (negative) — the writer never emits it, so bail rather
        // than misinterpret timestamps as ticks-per-quarter-note.
        return markers;
    }

    for (int t = 0; t < midi.getNumTracks(); ++t) {
        const auto* track = midi.getTrack(t);
        if (track == nullptr) {
            continue;
        }
        for (int e = 0; e < track->getNumEvents(); ++e) {
            const auto& msg = track->getEventPointer(e)->message;
            if (!msg.isTextMetaEvent() || msg.getMetaEventType() != 6) {
                continue;
            }
            const auto text = msg.getTextFromTextMetaEvent();
            if (!text.startsWith("CHORD:")) {
                continue;
            }
            auto parts = juce::StringArray::fromTokens(text.substring(6), ":", "");
            if (parts.size() < 2) {
                continue;
            }
            ChordMarker marker;
            marker.beatPosition = msg.getTimeStamp() / ticksPerQN;
            // Last token is the length; everything before it is the name
            // (chord names can contain colons, e.g. "C:maj7").
            marker.lengthBeats = parts[parts.size() - 1].getDoubleValue();
            parts.remove(parts.size() - 1);
            marker.chordName = parts.joinIntoString(":");
            if (marker.lengthBeats <= 0.0) {
                marker.lengthBeats = 4.0;
            }
            markers.push_back(marker);
        }
    }

    return markers;
}

std::vector<ChordMarker> readChordMarkers(const juce::File& midiFile) {
    juce::FileInputStream stream(midiFile);
    if (!stream.openedOk()) {
        return {};
    }
    juce::MidiFile midi;
    if (!midi.readFrom(stream)) {
        return {};
    }
    return readChordMarkers(midi);
}

}  // namespace magda::daw
