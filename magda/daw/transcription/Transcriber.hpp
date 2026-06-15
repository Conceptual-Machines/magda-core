// Audio-to-MIDI transcription (issue #1168).
//
// Backend-agnostic interface. A Transcriber turns a mono PCM buffer into a
// list of notes in the SECONDS domain. It is pure DSP/ML: it knows only
// samples, Hz, and seconds, and deliberately does NOT depend on the clip
// model or tempo. The seconds->beats conversion (which needs the clip tempo)
// happens one layer up in TranscriptionService, the only place the two
// domains meet. Keeping the transcriber tempo-agnostic lets monophonic
// (aubio / CREPE) backends drop in later without touching callers.

#pragma once

#include <memory>
#include <vector>

namespace magda::transcription {

// A single pitch-bend sample, as an offset from the note's start.
struct BendPoint {
    double offsetSec = 0.0;  // Seconds from note start (0..note length).
    double semitones = 0.0;  // Pitch offset in semitones from the note pitch.
};

// A detected note in the seconds domain.
struct TranscribedNote {
    int pitch = 60;               // MIDI note number (0-127).
    float velocity = 0.8F;        // Normalized 0..1 (mapped to 0-127 downstream).
    double startSec = 0.0;        // Note onset, seconds from buffer start.
    double lengthSec = 0.0;       // Note duration in seconds.
    std::vector<BendPoint> bend;  // Empty = no pitch glide. Sorted by offsetSec.
};

class Transcriber {
  public:
    virtual ~Transcriber() = default;

    // Transcribe mono float PCM. sampleRate is the buffer's rate; backends
    // resample internally to whatever the model expects. Returns notes sorted
    // by startSec. Returns empty on failure (never throws to the caller).
    virtual std::vector<TranscribedNote> transcribe(const float* mono, int numSamples,
                                                    int sampleRate) = 0;

    // Stable identifier for the backing model, suitable for
    // media_transcription.model_id (e.g. "basic_pitch").
    [[nodiscard]] virtual const char* modelId() const noexcept = 0;
};

}  // namespace magda::transcription
