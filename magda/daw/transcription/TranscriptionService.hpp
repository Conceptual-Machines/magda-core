// Audio-to-MIDI orchestration (issue #1168).
//
// The entry point the UI calls. Owns the (lazily created) Basic Pitch backend
// and a single-worker thread pool. Given an audio clip, it decodes the source
// PCM, runs transcription off the message thread, then hops back to the
// message thread to create a MIDI clip and insert the notes through the
// command system (so the whole thing is one undo step).
//
// This is the ONLY place the seconds domain (transcriber output) meets the
// beats domain (clip model): notes are converted with the project tempo here.

#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <mutex>

#include "ClipTypes.hpp"

namespace magda::transcription {

class BasicPitchTranscriber;

class TranscriptionService {
  public:
    static TranscriptionService& getInstance();

    // True when an ONNX backend is compiled in and the bundled model is found
    // on disk. UI uses this to enable/disable the "Transcribe to MIDI" action.
    [[nodiscard]] bool isAvailable();

    // Result: newClipId is INVALID_CLIP_ID on failure / no notes; error holds
    // a user-facing message in that case. Always fires on the message thread.
    using Completion = std::function<void(ClipId newClipId, juce::String error)>;

    // Transcribe the source of an existing audio clip and create a MIDI clip
    // on the same track over the same beat range. No-op (error completion) if
    // the clip isn't audio or the backend is unavailable.
    void transcribeAudioClip(ClipId sourceClipId, Completion onComplete);

    TranscriptionService(const TranscriptionService&) = delete;
    TranscriptionService& operator=(const TranscriptionService&) = delete;

  private:
    TranscriptionService();
    ~TranscriptionService();

    BasicPitchTranscriber* backend();  // lazily constructs; nullptr if no model

    std::unique_ptr<juce::ThreadPool> pool_;
    std::unique_ptr<BasicPitchTranscriber> backend_;
    std::once_flag backendOnce_;
    std::mutex backendMutex_;
};

}  // namespace magda::transcription
