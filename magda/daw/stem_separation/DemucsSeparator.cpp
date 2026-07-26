// See DemucsSeparator.hpp.

#include "DemucsSeparator.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace magda::stems::demucs {

std::vector<float> trapezoidWindow() {
    std::vector<float> w(static_cast<size_t>(kSegmentSamples), 1.0F);
    for (int i = 0; i < kOverlapSamples; ++i) {
        // numpy linspace(0, 1, overlap): endpoint included, w[0] == 0.
        const float v = static_cast<float>(i) / static_cast<float>(kOverlapSamples - 1);
        w[static_cast<size_t>(i)] = v;
        w[static_cast<size_t>(kSegmentSamples - 1 - i)] = v;
    }
    return w;
}

}  // namespace magda::stems::demucs

#if defined(MAGDA_HAVE_CLAP) && MAGDA_HAVE_CLAP

    #include <juce_audio_basics/juce_audio_basics.h>
    #include <onnxruntime_cxx_api.h>

    #include <array>
    #include <filesystem>

namespace magda::stems {

namespace {

// Resample one channel. ratio-style matches BasicPitchTranscriber: the
// LagrangeInterpolator's speedRatio is input samples consumed per output
// sample.
std::vector<float> resampleChannel(const float* in, int numSamples, double srcRate,
                                   double dstRate) {
    if (srcRate == dstRate)
        return {in, in + numSamples};
    const double ratio = srcRate / dstRate;
    const int outLen = static_cast<int>(std::ceil(numSamples / ratio));
    if (outLen <= 0)
        return {};
    // The Lagrange group delay is getBaseLatency() input samples (2), which
    // is latency / ratio output samples. Rounding the fractional case leaves
    // at most half an output sample of residual offset.
    const int latencyOut =
        static_cast<int>(std::lround(juce::LagrangeInterpolator::getBaseLatency() / ratio));
    std::vector<float> out(static_cast<size_t>(outLen + latencyOut), 0.0F);
    juce::LagrangeInterpolator interp;
    // By-value wrapAround count, not an out-param; 0 = zero-pad past the end,
    // covering both the discarded warm-up and the final tail samples. The
    // bounded overload keeps reads within numSamples.
    const int wrapAround = 0;
    interp.process(ratio, in, out.data(), outLen + latencyOut, numSamples, wrapAround);
    // Drop the warm-up so the output is time-aligned with the input.
    out.erase(out.begin(), out.begin() + latencyOut);
    return out;
}

}  // namespace

struct DemucsSeparator::Impl {
    Ort::Env env;
    Ort::SessionOptions sessionOptions;
    Ort::Session session{nullptr};
    Ort::MemoryInfo memoryInfo;

    explicit Impl(const std::filesystem::path& modelPath)
        : env(ORT_LOGGING_LEVEL_WARNING, "magda-demucs"),
          memoryInfo(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {
        // Unlike the CLAP embedder (single-threaded so background indexing
        // never competes with audio), separation is a rare, explicit,
        // minutes-long job the user is waiting on — let ORT use its default
        // worker pool.
        // path::c_str() is ORTCHAR_T-compatible on every platform (wchar_t
        // on Windows), matching BasicPitchTranscriber.
        session = Ort::Session(env, modelPath.c_str(), sessionOptions);
    }

    // Run one zero-padded segment. mix is (2, kSegmentSamples) planar;
    // stemsOut receives (kNumStems, 2, kSegmentSamples) planar.
    void runSegment(const std::vector<float>& mix, std::vector<float>& stemsOut) {
        const std::array<int64_t, 3> inShape = {1, 2, demucs::kSegmentSamples};
        auto inputTensor = Ort::Value::CreateTensor<float>(
            memoryInfo, const_cast<float*>(mix.data()), mix.size(), inShape.data(), inShape.size());

        const char* inputNames[] = {"mix"};
        const char* outputNames[] = {"stems"};
        auto outputs =
            session.Run(Ort::RunOptions{nullptr}, inputNames, &inputTensor, 1, outputNames, 1);

        const size_t count = static_cast<size_t>(demucs::kNumStems) * 2 * demucs::kSegmentSamples;
        const auto info = outputs[0].GetTensorTypeAndShapeInfo();
        const auto shape = info.GetShape();
        const std::array<int64_t, 4> expectedShape = {1, demucs::kNumStems, 2,
                                                      demucs::kSegmentSamples};
        if (shape != std::vector<int64_t>(expectedShape.begin(), expectedShape.end()) ||
            info.GetElementCount() != count)
            throw std::runtime_error("Demucs model returned an unexpected output shape");

        const float* data = outputs[0].GetTensorData<float>();
        stemsOut.assign(data, data + count);
    }
};

DemucsSeparator::DemucsSeparator(const std::filesystem::path& modelPath) {
    try {
        if (std::filesystem::exists(modelPath))
            impl_ = std::make_unique<Impl>(modelPath);
    } catch (const Ort::Exception&) {
        impl_.reset();
    } catch (const std::exception&) {
        impl_.reset();
    }
}

DemucsSeparator::~DemucsSeparator() = default;

bool DemucsSeparator::isLoaded() const noexcept {
    return impl_ != nullptr;
}

std::vector<Stem> DemucsSeparator::separate(const juce::AudioBuffer<float>& input,
                                            double sampleRate, const Progress& progress) {
    const int numChannels = input.getNumChannels();
    const int numSamples = input.getNumSamples();
    if (impl_ == nullptr || numChannels < 1 || numSamples < 1 || sampleRate <= 0.0)
        return {};

    // Stereo working signal at the model rate. Mono duplicates to L/R;
    // extra channels beyond two are ignored (the model is stereo).
    const float* srcL = input.getReadPointer(0);
    const float* srcR = input.getReadPointer(numChannels > 1 ? 1 : 0);
    std::vector<float> left = resampleChannel(srcL, numSamples, sampleRate, demucs::kSampleRate);
    std::vector<float> right = resampleChannel(srcR, numSamples, sampleRate, demucs::kSampleRate);
    const int total = static_cast<int>(left.size());
    if (total < 1)
        return {};

    const std::vector<float> window = demucs::trapezoidWindow();
    const int numChunks = (total + demucs::kStrideSamples - 1) / demucs::kStrideSamples;

    // Accumulators per stem, stereo, at model rate.
    std::vector<std::array<std::vector<float>, 2>> acc(demucs::kNumStems);
    for (auto& stem : acc)
        for (auto& ch : stem)
            ch.assign(static_cast<size_t>(total), 0.0F);
    std::vector<float> weight(static_cast<size_t>(total), 0.0F);

    std::vector<float> mix(static_cast<size_t>(2) * demucs::kSegmentSamples);
    std::vector<float> stemsOut;

    for (int c = 0; c < numChunks; ++c) {
        const int start = c * demucs::kStrideSamples;
        const int clen = std::min(demucs::kSegmentSamples, total - start);
        if (clen <= 0)
            break;

        std::fill(mix.begin(), mix.end(), 0.0F);
        std::copy(left.begin() + start, left.begin() + start + clen, mix.begin());
        std::copy(right.begin() + start, right.begin() + start + clen,
                  mix.begin() + demucs::kSegmentSamples);

        try {
            impl_->runSegment(mix, stemsOut);
        } catch (const Ort::Exception&) {
            return {};
        } catch (const std::exception&) {
            return {};
        }

        for (int s = 0; s < demucs::kNumStems; ++s) {
            for (int ch = 0; ch < 2; ++ch) {
                const float* src =
                    stemsOut.data() + (static_cast<size_t>(s) * 2 + static_cast<size_t>(ch)) *
                                          demucs::kSegmentSamples;
                float* dst = acc[static_cast<size_t>(s)][static_cast<size_t>(ch)].data() + start;
                for (int i = 0; i < clen; ++i)
                    dst[i] += src[i] * window[static_cast<size_t>(i)];
            }
        }
        for (int i = 0; i < clen; ++i)
            weight[static_cast<size_t>(start + i)] += window[static_cast<size_t>(i)];

        if (progress != nullptr &&
            !progress(static_cast<float>(c + 1) / static_cast<float>(numChunks)))
            return {};
    }

    for (auto& stem : acc)
        for (auto& ch : stem)
            for (int i = 0; i < total; ++i)
                ch[static_cast<size_t>(i)] /= std::max(weight[static_cast<size_t>(i)], 1.0e-8F);

    // Back to the caller's rate/length/channel count.
    const auto names = stemNames();
    std::vector<Stem> result(static_cast<size_t>(demucs::kNumStems));
    for (int s = 0; s < demucs::kNumStems; ++s) {
        auto& stem = result[static_cast<size_t>(s)];
        stem.name = names[static_cast<size_t>(s)];
        stem.audio.setSize(numChannels, numSamples);
        stem.audio.clear();

        for (int ch = 0; ch < numChannels; ++ch) {
            // The model is stereo; channels beyond two mirror L/R cyclically.
            const auto& src = acc[static_cast<size_t>(s)][static_cast<size_t>(ch % 2)];
            std::vector<float> back =
                resampleChannel(src.data(), total, demucs::kSampleRate, sampleRate);
            const int copyLen = std::min(numSamples, static_cast<int>(back.size()));
            stem.audio.copyFrom(ch, 0, back.data(), copyLen);
        }
    }
    return result;
}

}  // namespace magda::stems

#else  // MAGDA_HAVE_CLAP

// Stub for builds without ONNX Runtime (currently Intel macOS), mirroring
// BasicPitchTranscriber's no-backend branch.
namespace magda::stems {

struct DemucsSeparator::Impl {};

DemucsSeparator::DemucsSeparator(const std::filesystem::path& /*modelPath*/) {}
DemucsSeparator::~DemucsSeparator() = default;

bool DemucsSeparator::isLoaded() const noexcept {
    return false;
}

std::vector<Stem> DemucsSeparator::separate(const juce::AudioBuffer<float>& /*input*/,
                                            double /*sampleRate*/, const Progress& /*progress*/) {
    return {};
}

}  // namespace magda::stems

#endif  // MAGDA_HAVE_CLAP
