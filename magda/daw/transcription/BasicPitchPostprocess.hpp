// Basic Pitch posteriorgram -> note decoding (issue #1168).
//
// A dependency-free C++ port of the note-creation postprocessing from
// Spotify's Basic Pitch (basic_pitch/note_creation.py, Apache-2.0):
//   output_to_notes_polyphonic + get_pitch_bends + the inference-time frame
//   timing (model_frames_to_time). Pure: it takes the model's three
//   posteriorgram matrices and returns note events. No ONNX, no JUCE, only
//   std - so it is unit-testable in isolation and could be lifted into a
//   standalone "basic-pitch-cpp" library unchanged.
//
// Original work Copyright 2022-2024 Spotify AB, licensed under Apache-2.0.
// This C++ translation is distributed under MAGDA's GPL-3.0 (Apache-2.0 is
// one-way compatible with GPLv3). See third-party license notices.

#pragma once

#include <vector>

namespace magda::transcription::basicpitch {

// Model geometry (basic_pitch/constants.py). The ICASSP-2022 model emits
// these posteriorgrams per 2-second window; callers stitch windows into the
// full-length matrices below before decoding.
inline constexpr int kSampleRate = 22050;
inline constexpr int kFftHop = 256;
inline constexpr int kAnnotationsFps = kSampleRate / kFftHop;       // 86
inline constexpr int kAnnotNFrames = kAnnotationsFps * 2;           // 172
inline constexpr int kAudioNSamples = (kSampleRate * 2) - kFftHop;  // 43844
inline constexpr int kNoteFreqBins = 88;                            // note/onset
inline constexpr int kContourFreqBins = 88 * 3;                     // 264
inline constexpr int kContoursBinsPerSemitone = 3;
inline constexpr int kMidiOffset = 21;  // bin 0 == MIDI 21
inline constexpr int kMaxFreqIdx = 87;
inline constexpr double kAnnotationsBaseFrequency = 27.5;
inline constexpr int kOverlappingFrames = 30;  // window overlap

// Stitched, full-length model output. Matrices are row-major
// [frame * nFreqBins + bin]. note and onset have kNoteFreqBins columns;
// contour has kContourFreqBins columns. All three share nFrames rows.
struct ModelOutput {
    int nFrames = 0;
    std::vector<float> note;
    std::vector<float> onset;
    std::vector<float> contour;
};

struct PostprocessParams {
    float onsetThresh = 0.5F;     // DEFAULT_ONSET_THRESHOLD
    float frameThresh = 0.3F;     // DEFAULT_FRAME_THRESHOLD
    double minNoteLenMs = 127.7;  // DEFAULT_MINIMUM_NOTE_LENGTH_MS
    bool inferOnsets = true;
    bool melodiaTrick = true;
    int energyTol = 11;  // ENERGY_TOLERANCE
    bool includePitchBends = true;
    int nBinsTolerance = 25;  // get_pitch_bends default
    double minFreqHz = -1.0;  // < 0 = unconstrained
    double maxFreqHz = -1.0;
};

// A decoded note in the FRAME domain (start/end are frame indices).
struct NoteEvent {
    int startFrame = 0;
    int endFrame = 0;
    int pitch = 60;          // MIDI note number
    float amplitude = 0.0F;  // mean frame activation, 0..1
    // Per-frame pitch bend over [startFrame, endFrame), in contour bins
    // (1 bin == 1/kContoursBinsPerSemitone semitones). Empty if disabled.
    std::vector<int> bends;
};

// Port of output_to_notes_polyphonic (+ get_pitch_bends when enabled).
std::vector<NoteEvent> decodeNotes(const ModelOutput& output, const PostprocessParams& params);

// Port of model_frames_to_time: frame index -> seconds, correcting the
// per-window alignment offset introduced by the overlapping windows.
double frameToTimeSeconds(int frame);

}  // namespace magda::transcription::basicpitch
