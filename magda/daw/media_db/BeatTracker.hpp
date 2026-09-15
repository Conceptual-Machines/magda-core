// Beats and downbeats from a learned model (issue #2674).
//
// The autocorrelation tier this replaces measures periodicity in an onset
// envelope, which works on a steady drum loop and not much else: measured over
// a real library it agreed with the tempo in the filename on 36% of what it
// answered. A beat tracker answers where the beats are, and the tempo is what
// their spacing says -- on material the signal-processing route cannot read at
// all, like an arpeggio, a vocal chop or a solo instrument.
//
// The model is Beat This! (CPJKU, MIT), a transformer over log-mel frames,
// exported to ONNX. It takes the whole file at once and returns a beat and a
// downbeat logit per 20 ms frame.

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace magda::media {

struct BeatTrack {
    /// Beat positions in seconds from the start of the file.
    std::vector<double> beats;
    /// The subset of `beats` the model also called a downbeat.
    std::vector<double> downbeats;
    /// Beats per minute from the spacing of the beats. Zero when there are too
    /// few to say.
    double bpm = 0.0;
    /// Beats per bar, from the spacing of the downbeats. Zero when the model
    /// found none.
    int beatsPerBar = 0;
    /// How regular the beat spacing is, in [0, 1]. One means every interval is
    /// the same; a rubato performance or a misread file is far below it.
    double steadiness = 0.0;
};

class BeatTracker {
  public:
    /// Throws if the model is missing or will not load.
    explicit BeatTracker(const std::filesystem::path& modelPath);
    ~BeatTracker();
    BeatTracker(BeatTracker&&) noexcept;
    BeatTracker& operator=(BeatTracker&&) noexcept;

    /// Track @p mono, which is resampled to the rate the model wants.
    /// nullopt when the audio is too short to hold a beat.
    std::optional<BeatTrack> track(const float* mono, int numSamples, double sampleRate) const;

    /// Where the model lives when it is not passed explicitly: the media DB's
    /// models directory, beside the CLAP pair.
    static std::filesystem::path defaultModelPath();
    static bool isAvailable();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace magda::media
