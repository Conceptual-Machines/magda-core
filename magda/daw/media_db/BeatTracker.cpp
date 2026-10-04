#include "BeatTracker.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "MediaDbContext.hpp"

#if defined(MAGDA_HAVE_CLAP) && MAGDA_HAVE_CLAP

    #include <juce_audio_basics/juce_audio_basics.h>
    #include <onnxruntime_cxx_api.h>

    #include "magda/sdk/analysis/LogMel.hpp"

namespace magda::media {

namespace {

// What the exported model was trained on. Every one of these has to match the
// Python side or the frames mean nothing to it.
constexpr int kModelRate = 22050;
constexpr int kFftOrder = 10;
constexpr int kFftSize = 1 << kFftOrder;  // 1024
constexpr int kHop = 441;                 // 50 frames a second
constexpr int kMels = 128;
constexpr double kFMin = 30.0;
constexpr double kFMax = 11000.0;
constexpr double kLogMultiplier = 1000.0;
constexpr double kAmplitudeFloor = 1e-10;

constexpr double kFrameSeconds = static_cast<double>(kHop) / kModelRate;

// A frame is a beat where the model is more sure than not and surer than its
// neighbours. The tracker's own DBN post-processing is a stronger decoder, but
// tempo comes from the spacing of the beats rather than their exact placement.
constexpr float kBeatThreshold = 0.5F;
constexpr int kPeakRadius = 2;

// Fewer beats than this is not a tempo. Three peaks scattered over a long file
// give a confident-looking 23 bpm, which is a vocal phrase with two consonants
// in it rather than music at a tempo.
constexpr std::size_t kMinBeats = 8;

// What a tempo can be. The spacing of the beats can say anything; a tempo
// outside this is a metrical reading of one inside it, so it is folded rather
// than reported.
constexpr double kMinBpm = 60.0;
constexpr double kMaxBpm = 200.0;

// How much audio the model reads, whatever it was handed. Short files are
// repeated up to it, because the model is trained on songs and a two-bar loop
// gives it two seconds to work from -- and a loop repeats by definition, so the
// material already implies the context. Long ones are cropped to it, because
// the model reads what it is given in one pass and the cost grows with the
// length: 24 s needs 0.8 GB and half a second, 60 s needs 3.3 GB, 120 s needs
// 6.6 GB and 24 s of CPU, and past ~200 s the partial-attention blocks throw.
// A tempo does not need more -- it is one number for the whole file, and 24 s
// of it measures the same as 30 (#2674).
constexpr double kContextSeconds = 24.0;

// A file's length is taken as a whole number of beats when it is within this of
// one, which is what turns a 179.1 into the 175 the loop actually is.
constexpr double kWholeBeatTolerance = 0.04;

std::vector<float> resampleToModelRate(const float* mono, int numSamples, double sampleRate) {
    if (std::abs(sampleRate - kModelRate) < 1.0) {
        return {mono, mono + numSamples};
    }
    const double ratio = sampleRate / kModelRate;
    const auto outSamples = static_cast<int>(std::floor(numSamples / ratio));
    std::vector<float> out(static_cast<std::size_t>(std::max(outSamples, 0)), 0.0F);
    if (out.empty()) {
        return out;
    }
    juce::LagrangeInterpolator interpolator;
    interpolator.process(ratio, mono, out.data(), outSamples);
    return out;
}

/// Log-mel frames, [frames][mels] flattened row-major, matching the tensor the
/// model takes.
/// The model's front end: Slaney mels of the magnitude spectrum over reflect-padded frames,
/// compressed as log1p(1000 x) (magda-sdk docs/measurement.md). Frame-major.
std::vector<float> logMelFrames(const std::vector<float>& audio, int& framesOut) {
    static const sdk::LogMelConfig config = [] {
        sdk::LogMelConfig c;
        c.sampleRate = kModelRate;
        c.fftSize = kFftSize;
        c.hopSize = kHop;
        c.numMels = kMels;
        c.fMin = kFMin;
        c.fMax = kFMax;
        c.scale = sdk::MelScale::Slaney;
        c.spectrum = sdk::MelSpectrum::Magnitude;
        c.binScale = 1.0 / std::sqrt(static_cast<double>(kFftSize));
        c.normaliseWindow = false;
        c.padding = sdk::MelPadding::Reflect;
        c.compression = sdk::MelCompression::Log1p;
        c.logOffset = kAmplitudeFloor;
        c.log1pScale = kLogMultiplier;
        return c;
    }();
    sdk::LogMelFrontEnd frontEnd;
    frontEnd.prepare(config);
    framesOut = frontEnd.numFrames(static_cast<int>(audio.size()));
    return frontEnd.compute(audio.data(), static_cast<int>(audio.size()));
}

std::vector<double> pickPeaks(const float* logits, int frames) {
    std::vector<double> times;
    for (int f = 0; f < frames; ++f) {
        const float probability = 1.0F / (1.0F + std::exp(-logits[f]));
        if (probability < kBeatThreshold) {
            continue;
        }
        bool isPeak = true;
        for (int d = -kPeakRadius; d <= kPeakRadius && isPeak; ++d) {
            const int neighbour = f + d;
            if (d == 0 || neighbour < 0 || neighbour >= frames) {
                continue;
            }
            isPeak = logits[f] >= logits[neighbour];
        }
        if (isPeak) {
            times.push_back(f * kFrameSeconds);
        }
    }
    return times;
}

double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

}  // namespace

struct BeatTracker::Impl {
    Ort::Env env;
    Ort::SessionOptions options;
    Ort::Session session{nullptr};
    Ort::MemoryInfo memory;
    std::string inputName;
    std::vector<std::string> outputNames;

    explicit Impl(const std::filesystem::path& modelPath)
        : env(ORT_LOGGING_LEVEL_WARNING, "magda-beats"),
          memory(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {
        options.SetIntraOpNumThreads(1);
        options.SetInterOpNumThreads(1);
        session = Ort::Session(env, modelPath.c_str(), options);

        Ort::AllocatorWithDefaultOptions allocator;
        inputName = session.GetInputNameAllocated(0, allocator).get();
        for (std::size_t i = 0; i < session.GetOutputCount(); ++i) {
            outputNames.emplace_back(session.GetOutputNameAllocated(i, allocator).get());
        }
    }
};

BeatTracker::BeatTracker(const std::filesystem::path& modelPath)
    : impl_(std::make_unique<Impl>(modelPath)) {}
BeatTracker::~BeatTracker() = default;
BeatTracker::BeatTracker(BeatTracker&&) noexcept = default;
BeatTracker& BeatTracker::operator=(BeatTracker&&) noexcept = default;

std::optional<BeatTrack> BeatTracker::track(const float* mono, int numSamples,
                                            double sampleRate) const {
    if (impl_ == nullptr || mono == nullptr || numSamples <= 0 || sampleRate <= 0.0) {
        return std::nullopt;
    }

    const double sourceSeconds = numSamples / sampleRate;

    // Repeat a short loop so the model has something to read, and remember how
    // long the file itself was: the tempo has to make *that* a whole number of
    // beats, not the tiled copy.
    std::vector<float> context;
    const float* audioIn = mono;
    int audioSamples = numSamples;
    if (sourceSeconds < kContextSeconds) {
        const auto wanted = static_cast<std::size_t>(kContextSeconds * sampleRate);
        context.reserve(wanted);
        while (context.size() < wanted) {
            const auto chunk =
                std::min(static_cast<std::size_t>(numSamples), wanted - context.size());
            context.insert(context.end(), mono, mono + chunk);
        }
        audioIn = context.data();
        audioSamples = static_cast<int>(context.size());
    } else if (sourceSeconds > kContextSeconds) {
        // From the middle, where a song is playing: the head of a long file is
        // an intro or a count-in as often as it is the material.
        audioSamples = static_cast<int>(kContextSeconds * sampleRate);
        audioIn = mono + (numSamples - audioSamples) / 2;
    }
    const bool readWhole = sourceSeconds <= kContextSeconds;

    const auto audio = resampleToModelRate(audioIn, audioSamples, sampleRate);
    int frames = 0;
    auto mel = logMelFrames(audio, frames);
    if (frames <= 0) {
        return std::nullopt;
    }

    const std::array<std::int64_t, 3> shape{1, frames, kMels};
    auto input = Ort::Value::CreateTensor<float>(impl_->memory, mel.data(), mel.size(),
                                                 shape.data(), shape.size());

    const char* inputNames[] = {impl_->inputName.c_str()};
    std::vector<const char*> outputNames;
    outputNames.reserve(impl_->outputNames.size());
    for (const auto& name : impl_->outputNames) {
        outputNames.push_back(name.c_str());
    }

    std::vector<Ort::Value> outputs;
    try {
        outputs = impl_->session.Run(Ort::RunOptions{nullptr}, inputNames, &input, 1,
                                     outputNames.data(), outputNames.size());
    } catch (const Ort::Exception&) {
        return std::nullopt;
    }
    if (outputs.empty()) {
        return std::nullopt;
    }

    BeatTrack track;
    track.beats = pickPeaks(outputs[0].GetTensorData<float>(), frames);
    if (outputs.size() > 1) {
        track.downbeats = pickPeaks(outputs[1].GetTensorData<float>(), frames);
    }
    if (track.beats.size() < kMinBeats) {
        return track;
    }

    std::vector<double> intervals;
    intervals.reserve(track.beats.size() - 1);
    for (std::size_t i = 1; i < track.beats.size(); ++i) {
        intervals.push_back(track.beats[i] - track.beats[i - 1]);
    }
    double beatSeconds = median(intervals);
    if (beatSeconds > 0.0) {
        // Beat times land on 20 ms frames, so the median interval can only be a
        // whole number of them: at 172 bpm the period is 17.4 frames and the
        // median says 17, which is 176 bpm. Fitting a line through every beat
        // averages the quantisation away. Each beat is numbered by how many
        // periods it sits from the last one, so a beat the peak picker missed
        // widens a gap rather than halving the tempo.
        std::vector<double> indices;
        indices.reserve(track.beats.size());
        double index = 0.0;
        indices.push_back(index);
        for (std::size_t i = 1; i < track.beats.size(); ++i) {
            const double steps =
                std::max(1.0, std::round((track.beats[i] - track.beats[i - 1]) / beatSeconds));
            index += steps;
            indices.push_back(index);
        }

        const auto count = static_cast<double>(indices.size());
        const double meanIndex = std::accumulate(indices.begin(), indices.end(), 0.0) / count;
        const double meanTime =
            std::accumulate(track.beats.begin(), track.beats.end(), 0.0) / count;
        double covariance = 0.0;
        double variance = 0.0;
        for (std::size_t i = 0; i < indices.size(); ++i) {
            const double di = indices[i] - meanIndex;
            covariance += di * (track.beats[i] - meanTime);
            variance += di * di;
        }
        if (variance > 0.0 && covariance > 0.0) {
            beatSeconds = covariance / variance;
        }

        track.bpm = 60.0 / beatSeconds;

        // A metrical reading outside what a tempo can be is folded back into
        // it: the beats are right, the unit they were counted in is not.
        while (track.bpm > 0.0 && track.bpm < kMinBpm) {
            track.bpm *= 2.0;
        }
        while (track.bpm > kMaxBpm) {
            track.bpm /= 2.0;
        }

        // A loop is a whole number of beats long. Snapping to that is exact
        // where the beat spacing is only close -- the model reads to a 20 ms
        // frame, and a seam in the repeated audio pulls the fit a little.
        // Only for a file read end to end: a track long enough to be sampled
        // from the middle is not a loop, and its length says nothing.
        if (sourceSeconds > 0.0 && readWhole) {
            const double beats = sourceSeconds * track.bpm / 60.0;
            const int perBar = track.beatsPerBar >= 2 ? track.beatsPerBar : 4;

            // A bar first, then a beat. A loop is a whole number of bars far
            // more often than it is 33 beats long, and a model reading 3% fast
            // over a 32 beat file lands nearer 33 than 32 without this.
            for (double candidate : {std::round(beats / perBar) * perBar, std::round(beats)}) {
                if (candidate < 2.0) {
                    continue;
                }
                if (std::abs(beats - candidate) / candidate > kWholeBeatTolerance) {
                    continue;
                }
                const double snapped = candidate * 60.0 / sourceSeconds;
                if (snapped >= kMinBpm && snapped <= kMaxBpm) {
                    track.bpm = snapped;
                    break;
                }
            }
        }

        // How far a typical interval is from the median one. A steady loop
        // sits within a frame or two of it; a rubato take does not.
        std::vector<double> deviations;
        deviations.reserve(intervals.size());
        for (double interval : intervals) {
            deviations.push_back(std::abs(interval - beatSeconds) / beatSeconds);
        }
        track.steadiness = std::clamp(1.0 - median(deviations) * 4.0, 0.0, 1.0);
    }

    if (track.downbeats.size() >= 2 && beatSeconds > 0.0) {
        std::vector<double> barLengths;
        barLengths.reserve(track.downbeats.size() - 1);
        for (std::size_t i = 1; i < track.downbeats.size(); ++i) {
            barLengths.push_back((track.downbeats[i] - track.downbeats[i - 1]) / beatSeconds);
        }
        track.beatsPerBar = static_cast<int>(std::lround(median(barLengths)));
    }

    return track;
}

std::filesystem::path BeatTracker::defaultModelPath() {
    return MediaDbContext::modelsDir() / "beat_this.onnx";
}

bool BeatTracker::isAvailable() {
    std::error_code error;
    return std::filesystem::exists(defaultModelPath(), error);
}

}  // namespace magda::media

#else  // no ONNX runtime in this build

namespace magda::media {

struct BeatTracker::Impl {};

BeatTracker::BeatTracker(const std::filesystem::path&) {}
BeatTracker::~BeatTracker() = default;
BeatTracker::BeatTracker(BeatTracker&&) noexcept = default;
BeatTracker& BeatTracker::operator=(BeatTracker&&) noexcept = default;

std::optional<BeatTrack> BeatTracker::track(const float*, int, double) const {
    return std::nullopt;
}

std::filesystem::path BeatTracker::defaultModelPath() {
    return MediaDbContext::modelsDir() / "beat_this.onnx";
}

bool BeatTracker::isAvailable() {
    return false;
}

}  // namespace magda::media

#endif
