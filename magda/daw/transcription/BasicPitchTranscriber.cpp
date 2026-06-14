// See BasicPitchTranscriber.hpp.

#include "BasicPitchTranscriber.hpp"

#include "BasicPitchPostprocess.hpp"

#if defined(MAGDA_HAVE_CLAP) && MAGDA_HAVE_CLAP

    #include <juce_audio_basics/juce_audio_basics.h>
    #include <onnxruntime_cxx_api.h>

    #include <algorithm>
    #include <array>
    #include <cmath>
    #include <cstdint>

namespace magda::transcription {

namespace bp = basicpitch;

namespace {

// Window overlap, in samples, matching basic_pitch run_inference.
constexpr int kOverlapLen = bp::kOverlappingFrames * bp::kFftHop;                 // 7680
constexpr int kHopSize = bp::kAudioNSamples - kOverlapLen;                        // 36164
constexpr int kFrontPad = kOverlapLen / 2;                                        // 3840
constexpr int kFramesPerWindowKept = bp::kAnnotNFrames - bp::kOverlappingFrames;  // 142
constexpr int kHalfOverlapFrames = bp::kOverlappingFrames / 2;                    // 15

// Resample mono PCM to 22050 Hz. Returns the resampled buffer.
std::vector<float> resampleTo22050(const float* mono, int numSamples, int sampleRate) {
    if (sampleRate == bp::kSampleRate) {
        return std::vector<float>(mono, mono + numSamples);
    }
    const double ratio = static_cast<double>(sampleRate) / bp::kSampleRate;
    const int outLen = static_cast<int>(std::ceil(numSamples / ratio));
    std::vector<float> out(static_cast<size_t>(std::max(outLen, 0)), 0.0F);
    if (outLen <= 0) {
        return out;
    }
    juce::LagrangeInterpolator interp;
    interp.process(ratio, mono, out.data(), outLen);
    return out;
}

// Turn the model's per-frame contour bends (~86/s, 1 bin == 1/3 semitone) into
// a sparse MPE polyline. The raw argmax jitters +/-1 bin on steady notes, so:
//   - drop notes whose total pitch travel is below kMinTravelSemitones (steady
//     or merely detuned -> no expression at all), and
//   - Ramer-Douglas-Peucker the rest down to the points that actually shape the
//     glide, instead of one point per frame.
std::vector<BendPoint> simplifyBends(const std::vector<int>& binBends, double lengthSec) {
    constexpr double kMinTravelSemitones = 0.5;  // peak-to-trough gate
    constexpr double kRdpEpsSemitones = 0.2;     // simplification tolerance

    const int n = static_cast<int>(binBends.size());
    if (n < 2 || lengthSec <= 0.0) {
        return {};
    }

    std::vector<BendPoint> pts(static_cast<size_t>(n));
    double lo = 0.0;
    double hi = 0.0;
    for (int i = 0; i < n; ++i) {
        pts[static_cast<size_t>(i)].offsetSec = (static_cast<double>(i) / (n - 1)) * lengthSec;
        const double st =
            static_cast<double>(binBends[static_cast<size_t>(i)]) / bp::kContoursBinsPerSemitone;
        pts[static_cast<size_t>(i)].semitones = st;
        lo = std::min(lo, st);
        hi = std::max(hi, st);
    }
    if (hi - lo < kMinTravelSemitones) {
        return {};
    }

    // Iterative RDP using vertical (semitone) distance; x is monotonic time.
    std::vector<char> keep(static_cast<size_t>(n), 0);
    keep[0] = 1;
    keep[static_cast<size_t>(n - 1)] = 1;
    std::vector<std::pair<int, int>> stack{{0, n - 1}};
    while (!stack.empty()) {
        const auto [a, b] = stack.back();
        stack.pop_back();
        if (b - a < 2) {
            continue;
        }
        const double xa = pts[static_cast<size_t>(a)].offsetSec;
        const double ya = pts[static_cast<size_t>(a)].semitones;
        const double xb = pts[static_cast<size_t>(b)].offsetSec;
        const double yb = pts[static_cast<size_t>(b)].semitones;
        double maxDist = -1.0;
        int split = -1;
        for (int i = a + 1; i < b; ++i) {
            const double t =
                (xb > xa) ? (pts[static_cast<size_t>(i)].offsetSec - xa) / (xb - xa) : 0.0;
            const double yLerp = ya + t * (yb - ya);
            const double dist = std::abs(pts[static_cast<size_t>(i)].semitones - yLerp);
            if (dist > maxDist) {
                maxDist = dist;
                split = i;
            }
        }
        if (maxDist > kRdpEpsSemitones && split > 0) {
            keep[static_cast<size_t>(split)] = 1;
            stack.emplace_back(a, split);
            stack.emplace_back(split, b);
        }
    }

    std::vector<BendPoint> out;
    for (int i = 0; i < n; ++i) {
        if (keep[static_cast<size_t>(i)]) {
            out.push_back(pts[static_cast<size_t>(i)]);
        }
    }
    return out;
}

}  // namespace

struct BasicPitchTranscriber::Impl {
    Ort::Env env;
    Ort::SessionOptions sessionOptions;
    Ort::Session session{nullptr};
    Ort::MemoryInfo memoryInfo;

    explicit Impl(const std::filesystem::path& modelPath)
        : env(ORT_LOGGING_LEVEL_WARNING, "magda-basic-pitch"),
          memoryInfo(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {
        sessionOptions.SetIntraOpNumThreads(1);
        sessionOptions.SetInterOpNumThreads(1);
        session = Ort::Session(env, modelPath.c_str(), sessionOptions);
    }

    // Run one 43844-sample window. Appends 172 frames to each output matrix.
    void runWindow(const float* window, std::vector<float>& note, std::vector<float>& onset,
                   std::vector<float>& contour) {
        std::array<int64_t, 3> shape{1, bp::kAudioNSamples, 1};
        auto tensor = Ort::Value::CreateTensor<float>(memoryInfo, const_cast<float*>(window),
                                                      static_cast<size_t>(bp::kAudioNSamples),
                                                      shape.data(), shape.size());

        const char* inputNames[] = {"serving_default_input_2:0"};
        // Explicit output names (order: note, onset, contour) matching the
        // Python Model.predict mapping.
        const char* outputNames[] = {"StatefulPartitionedCall:1", "StatefulPartitionedCall:2",
                                     "StatefulPartitionedCall:0"};
        Ort::RunOptions runOpts;
        auto outputs = session.Run(runOpts, inputNames, &tensor, 1, outputNames, 3);

        const float* noteData = outputs[0].GetTensorData<float>();
        const float* onsetData = outputs[1].GetTensorData<float>();
        const float* contourData = outputs[2].GetTensorData<float>();

        note.insert(note.end(), noteData,
                    noteData + static_cast<size_t>(bp::kAnnotNFrames) * bp::kNoteFreqBins);
        onset.insert(onset.end(), onsetData,
                     onsetData + static_cast<size_t>(bp::kAnnotNFrames) * bp::kNoteFreqBins);
        contour.insert(contour.end(), contourData,
                       contourData + static_cast<size_t>(bp::kAnnotNFrames) * bp::kContourFreqBins);
    }
};

BasicPitchTranscriber::BasicPitchTranscriber(const std::filesystem::path& modelPath) {
    if (!std::filesystem::exists(modelPath)) {
        return;
    }
    try {
        impl_ = std::make_unique<Impl>(modelPath);
    } catch (const Ort::Exception&) {
        impl_.reset();
    }
}

BasicPitchTranscriber::~BasicPitchTranscriber() = default;

bool BasicPitchTranscriber::isLoaded() const noexcept {
    return impl_ != nullptr;
}

std::vector<TranscribedNote> BasicPitchTranscriber::transcribe(const float* mono, int numSamples,
                                                               int sampleRate) {
    if (!impl_ || mono == nullptr || numSamples <= 0) {
        return {};
    }

    const std::vector<float> resampled = resampleTo22050(mono, numSamples, sampleRate);
    const int originalLength = static_cast<int>(resampled.size());
    if (originalLength <= 0) {
        return {};
    }

    // Front-pad with kFrontPad zeros, then window with kHopSize stride.
    std::vector<float> padded(static_cast<size_t>(kFrontPad) + resampled.size(), 0.0F);
    std::copy(resampled.begin(), resampled.end(), padded.begin() + kFrontPad);

    const int paddedLen = static_cast<int>(padded.size());
    std::vector<float> window(static_cast<size_t>(bp::kAudioNSamples));

    // Per-window outputs, concatenated (172 frames per window).
    std::vector<float> noteAll;
    std::vector<float> onsetAll;
    std::vector<float> contourAll;

    int numWindows = 0;
    for (int start = 0; start < paddedLen; start += kHopSize) {
        std::fill(window.begin(), window.end(), 0.0F);
        const int copyLen = std::min(bp::kAudioNSamples, paddedLen - start);
        std::copy(padded.begin() + start, padded.begin() + start + copyLen, window.begin());
        try {
            impl_->runWindow(window.data(), noteAll, onsetAll, contourAll);
        } catch (const Ort::Exception&) {
            return {};
        }
        ++numWindows;
    }
    if (numWindows == 0) {
        return {};
    }

    // unwrap_output: drop kHalfOverlapFrames from each end of every window,
    // concatenate, then trim to the number of frames the original audio
    // length implies.
    bp::ModelOutput out;
    const auto stitch = [&](const std::vector<float>& all, int nFreq) {
        std::vector<float> kept;
        kept.reserve(static_cast<size_t>(numWindows) * kFramesPerWindowKept * nFreq);
        for (int w = 0; w < numWindows; ++w) {
            const size_t base = static_cast<size_t>(w) * bp::kAnnotNFrames * nFreq;
            const size_t from = base + static_cast<size_t>(kHalfOverlapFrames) * nFreq;
            const size_t to =
                base + static_cast<size_t>(bp::kAnnotNFrames - kHalfOverlapFrames) * nFreq;
            kept.insert(kept.end(), all.begin() + from, all.begin() + to);
        }
        return kept;
    };

    const double nExpectedWindows = static_cast<double>(originalLength) / kHopSize;
    const int nFramesTotal = static_cast<int>(nExpectedWindows * kFramesPerWindowKept);

    auto trim = [&](std::vector<float> kept, int nFreq) {
        const size_t keep = static_cast<size_t>(std::max(nFramesTotal, 0)) * nFreq;
        if (kept.size() > keep) {
            kept.resize(keep);
        }
        return kept;
    };

    out.note = trim(stitch(noteAll, bp::kNoteFreqBins), bp::kNoteFreqBins);
    out.onset = trim(stitch(onsetAll, bp::kNoteFreqBins), bp::kNoteFreqBins);
    out.contour = trim(stitch(contourAll, bp::kContourFreqBins), bp::kContourFreqBins);
    out.nFrames = static_cast<int>(out.note.size() / bp::kNoteFreqBins);
    if (out.nFrames <= 1) {
        return {};
    }

    const bp::PostprocessParams params;
    const std::vector<bp::NoteEvent> events = bp::decodeNotes(out, params);

    std::vector<TranscribedNote> notes;
    notes.reserve(events.size());
    for (const bp::NoteEvent& ev : events) {
        const double startSec = bp::frameToTimeSeconds(ev.startFrame);
        const double endSec = bp::frameToTimeSeconds(ev.endFrame);

        TranscribedNote note;
        note.pitch = ev.pitch;
        note.velocity = std::clamp(ev.amplitude, 0.0F, 1.0F);
        note.startSec = startSec;
        note.lengthSec = std::max(0.0, endSec - startSec);

        // Sparse pitch expression: steady notes get none, glides get a few
        // shaping points (see simplifyBends).
        note.bend = simplifyBends(ev.bends, note.lengthSec);
        notes.push_back(std::move(note));
    }

    std::sort(notes.begin(), notes.end(), [](const TranscribedNote& a, const TranscribedNote& b) {
        return a.startSec < b.startSec;
    });
    return notes;
}

}  // namespace magda::transcription

#else  // MAGDA_HAVE_CLAP

// Stub for builds without ONNX Runtime (currently Intel macOS), mirroring
// ClapAudioEncoder's no-backend branch.
namespace magda::transcription {

struct BasicPitchTranscriber::Impl {};

BasicPitchTranscriber::BasicPitchTranscriber(const std::filesystem::path& /*modelPath*/) {}
BasicPitchTranscriber::~BasicPitchTranscriber() = default;

bool BasicPitchTranscriber::isLoaded() const noexcept {
    return false;
}

std::vector<TranscribedNote> BasicPitchTranscriber::transcribe(const float* /*mono*/,
                                                               int /*numSamples*/,
                                                               int /*sampleRate*/) {
    return {};
}

}  // namespace magda::transcription

#endif  // MAGDA_HAVE_CLAP
