// See SpleeterSeparator.hpp.

#include "SpleeterSeparator.hpp"

#if defined(MAGDA_HAVE_CLAP) && MAGDA_HAVE_CLAP

    #include <juce_audio_basics/juce_audio_basics.h>
    #include <juce_dsp/juce_dsp.h>
    #include <onnxruntime_cxx_api.h>

    #include <algorithm>
    #include <array>
    #include <cmath>
    #include <filesystem>
    #include <stdexcept>

namespace magda::stems {

namespace {

// Same ratio convention as the other backends (input samples consumed per
// output sample).
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

// Periodic Hann, matching torch.hann_window(4096, periodic=True) used when
// the sherpa-onnx conversion was validated.
std::vector<float> periodicHann() {
    std::vector<float> w(static_cast<size_t>(spleeter::kFftSize));
    for (int n = 0; n < spleeter::kFftSize; ++n)
        w[static_cast<size_t>(n)] =
            0.5F - 0.5F * static_cast<float>(std::cos(2.0 * juce::MathConstants<double>::pi * n /
                                                      spleeter::kFftSize));
    return w;
}

}  // namespace

struct SpleeterSeparator::Impl {
    Ort::Env env;
    Ort::SessionOptions sessionOptions;
    Ort::Session vocals{nullptr};
    Ort::Session accompaniment{nullptr};
    Ort::MemoryInfo memoryInfo;

    Impl(const std::filesystem::path& vocalsPath, const std::filesystem::path& accompPath)
        : env(ORT_LOGGING_LEVEL_WARNING, "magda-spleeter"),
          memoryInfo(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {
        // Explicit long job the user waits on: ORT's default worker pool,
        // like DemucsSeparator.
        vocals = Ort::Session(env, vocalsPath.c_str(), sessionOptions);
        accompaniment = Ort::Session(env, accompPath.c_str(), sessionOptions);
    }

    // Run one UNet over the shared input x: (2, numSplits, 512, 1024).
    std::vector<float> run(Ort::Session& session, const std::vector<float>& x, int numSplits) {
        const std::array<int64_t, 4> shape = {2, numSplits, spleeter::kFramesPerSplit,
                                              spleeter::kKeptBins};
        auto inputTensor = Ort::Value::CreateTensor<float>(memoryInfo, const_cast<float*>(x.data()),
                                                           x.size(), shape.data(), shape.size());

        const char* inputNames[] = {"x"};
        const char* outputNames[] = {"y"};
        auto outputs =
            session.Run(Ort::RunOptions{nullptr}, inputNames, &inputTensor, 1, outputNames, 1);

        const auto info = outputs[0].GetTensorTypeAndShapeInfo();
        if (info.GetShape() != std::vector<int64_t>(shape.begin(), shape.end()) ||
            info.GetElementCount() != x.size())
            throw std::runtime_error("Spleeter model returned an unexpected output shape");

        const float* data = outputs[0].GetTensorData<float>();
        return {data, data + x.size()};
    }
};

SpleeterSeparator::SpleeterSeparator(const std::filesystem::path& vocalsModelPath,
                                     const std::filesystem::path& accompanimentModelPath) {
    try {
        if (std::filesystem::exists(vocalsModelPath) &&
            std::filesystem::exists(accompanimentModelPath))
            impl_ = std::make_unique<Impl>(vocalsModelPath, accompanimentModelPath);
    } catch (const Ort::Exception&) {
        impl_.reset();
    } catch (const std::exception&) {
        impl_.reset();
    }
}

SpleeterSeparator::~SpleeterSeparator() = default;

bool SpleeterSeparator::isLoaded() const noexcept {
    return impl_ != nullptr;
}

std::vector<Stem> SpleeterSeparator::separate(const juce::AudioBuffer<float>& input,
                                              double sampleRate, const Progress& progress) {
    namespace sp = spleeter;

    const int numChannels = input.getNumChannels();
    const int numSamples = input.getNumSamples();
    if (impl_ == nullptr || numChannels < 1 || numSamples < 1 || sampleRate <= 0.0)
        return {};

    auto report = [&progress](float frac) { return progress == nullptr || progress(frac); };

    // Stereo at the model rate; mono duplicates, extra channels ignored.
    const float* srcL = input.getReadPointer(0);
    const float* srcR = input.getReadPointer(numChannels > 1 ? 1 : 0);
    std::array<std::vector<float>, 2> signal = {
        resampleChannel(srcL, numSamples, sampleRate, sp::kSampleRate),
        resampleChannel(srcR, numSamples, sampleRate, sp::kSampleRate)};
    const int total = static_cast<int>(signal[0].size());
    if (total < 1)
        return {};

    // Trailing zero-pad (one FFT frame, like the Spleeter reference) so the
    // tail is covered despite center=false framing.
    const int paddedLen = total + sp::kFftSize;
    const int numFrames = 1 + (paddedLen - sp::kFftSize) / sp::kHop;
    const int paddedFrames = sp::paddedFrameCount(numFrames);
    const int numSplits = paddedFrames / sp::kFramesPerSplit;

    const std::vector<float> window = periodicHann();
    juce::dsp::FFT fft(12);  // 4096

    // Process one 512-frame model split at a time. Keeping the STFT, input
    // tensor and both model outputs chunk-local bounds peak memory for long
    // files; only the input signal and final model-rate stem audio scale with
    // duration.
    std::vector<float> norm(static_cast<size_t>(paddedLen), 0.0F);
    std::array<std::array<std::vector<float>, 2>, 2> rendered;
    for (auto& stem : rendered)
        for (auto& channel : stem)
            channel.assign(static_cast<size_t>(paddedLen), 0.0F);

    std::vector<float> frame(static_cast<size_t>(sp::kFftSize) * 2);
    for (int split = 0; split < numSplits; ++split) {
        const int firstFrame = split * sp::kFramesPerSplit;
        const int framesThisSplit = std::min(sp::kFramesPerSplit, numFrames - firstFrame);
        if (framesThisSplit <= 0)
            break;

        std::array<std::vector<float>, 2> spectra;
        for (auto& channel : spectra)
            channel.assign(static_cast<size_t>(framesThisSplit) * sp::kNumBins * 2, 0.0F);
        std::vector<float> x(static_cast<size_t>(2) * sp::kFramesPerSplit * sp::kKeptBins, 0.0F);

        for (int ch = 0; ch < 2; ++ch) {
            for (int localFrame = 0; localFrame < framesThisSplit; ++localFrame) {
                const int globalFrame = firstFrame + localFrame;
                const int start = globalFrame * sp::kHop;
                std::fill(frame.begin(), frame.end(), 0.0F);
                for (int i = 0; i < sp::kFftSize; ++i) {
                    const int pos = start + i;
                    if (pos < total)
                        frame[static_cast<size_t>(i)] =
                            signal[static_cast<size_t>(ch)][static_cast<size_t>(pos)] *
                            window[static_cast<size_t>(i)];
                    if (ch == 0 && start + i < paddedLen)
                        norm[static_cast<size_t>(start + i)] +=
                            window[static_cast<size_t>(i)] * window[static_cast<size_t>(i)];
                }
                fft.performRealOnlyForwardTransform(frame.data(), true);

                for (int b = 0; b < sp::kKeptBins; ++b) {
                    const float re = frame[static_cast<size_t>(2 * b)];
                    const float im = frame[static_cast<size_t>(2 * b + 1)];
                    const size_t xIdx = (static_cast<size_t>(ch) * sp::kFramesPerSplit +
                                         static_cast<size_t>(localFrame)) *
                                            sp::kKeptBins +
                                        static_cast<size_t>(b);
                    const size_t sIdx =
                        (static_cast<size_t>(localFrame) * sp::kNumBins + static_cast<size_t>(b)) *
                        2;
                    spectra[static_cast<size_t>(ch)][sIdx] = re;
                    spectra[static_cast<size_t>(ch)][sIdx + 1] = im;
                    x[xIdx] = std::sqrt(re * re + im * im);
                }
                for (int b = sp::kKeptBins; b < sp::kNumBins; ++b) {
                    const size_t sIdx =
                        (static_cast<size_t>(localFrame) * sp::kNumBins + static_cast<size_t>(b)) *
                        2;
                    spectra[static_cast<size_t>(ch)][sIdx] = frame[static_cast<size_t>(2 * b)];
                    spectra[static_cast<size_t>(ch)][sIdx + 1] =
                        frame[static_cast<size_t>(2 * b + 1)];
                }
            }
        }

        std::vector<float> vocalsMag;
        std::vector<float> accompMag;
        try {
            vocalsMag = impl_->run(impl_->vocals, x, 1);
            accompMag = impl_->run(impl_->accompaniment, x, 1);
        } catch (const Ort::Exception&) {
            return {};
        } catch (const std::exception&) {
            return {};
        }

        for (int stem = 0; stem < 2; ++stem) {
            for (int ch = 0; ch < 2; ++ch) {
                for (int localFrame = 0; localFrame < framesThisSplit; ++localFrame) {
                    std::fill(frame.begin(), frame.end(), 0.0F);
                    for (int b = 0; b < sp::kKeptBins; ++b) {
                        const size_t yIdx = (static_cast<size_t>(ch) * sp::kFramesPerSplit +
                                             static_cast<size_t>(localFrame)) *
                                                sp::kKeptBins +
                                            static_cast<size_t>(b);
                        const float v = vocalsMag[yIdx];
                        const float a = accompMag[yIdx];
                        const float sum = v * v + a * a + 1.0e-10F;
                        const float mask = ((stem == 0 ? v * v : a * a) + 0.5e-10F) / sum;
                        const size_t sIdx = (static_cast<size_t>(localFrame) * sp::kNumBins +
                                             static_cast<size_t>(b)) *
                                            2;
                        frame[static_cast<size_t>(2 * b)] =
                            spectra[static_cast<size_t>(ch)][sIdx] * mask;
                        frame[static_cast<size_t>(2 * b + 1)] =
                            spectra[static_cast<size_t>(ch)][sIdx + 1] * mask;
                    }

                    fft.performRealOnlyInverseTransform(frame.data());
                    const int start = (firstFrame + localFrame) * sp::kHop;
                    auto& output = rendered[static_cast<size_t>(stem)][static_cast<size_t>(ch)];
                    for (int i = 0; i < sp::kFftSize && start + i < paddedLen; ++i)
                        output[static_cast<size_t>(start + i)] +=
                            frame[static_cast<size_t>(i)] * window[static_cast<size_t>(i)];
                }
            }
        }

        if (!report(0.9F * static_cast<float>(split + 1) / static_cast<float>(numSplits)))
            return {};
    }

    for (auto& stem : rendered)
        for (auto& channel : stem)
            for (int i = 0; i < paddedLen; ++i) {
                const float weight = norm[static_cast<size_t>(i)];
                channel[static_cast<size_t>(i)] *= weight > 1.0e-8F ? 1.0F / weight : 0.0F;
            }

    const auto names = stemNames();
    std::vector<Stem> result(2);
    for (int stem = 0; stem < 2; ++stem) {
        result[static_cast<size_t>(stem)].name = names[static_cast<size_t>(stem)];
        result[static_cast<size_t>(stem)].audio.setSize(numChannels, numSamples);
        result[static_cast<size_t>(stem)].audio.clear();

        for (int ch = 0; ch < numChannels; ++ch) {
            const auto& src = rendered[static_cast<size_t>(stem)][static_cast<size_t>(ch % 2)];
            std::vector<float> back =
                resampleChannel(src.data(), total, sp::kSampleRate, sampleRate);
            const int copyLen = std::min(numSamples, static_cast<int>(back.size()));
            result[static_cast<size_t>(stem)].audio.copyFrom(ch, 0, back.data(), copyLen);
        }

        if (!report(0.9F + 0.05F * static_cast<float>(stem + 1)))
            return {};
    }

    return result;
}

}  // namespace magda::stems

#else  // MAGDA_HAVE_CLAP

// Stub for builds without ONNX Runtime (currently Intel macOS).
namespace magda::stems {

struct SpleeterSeparator::Impl {};

SpleeterSeparator::SpleeterSeparator(const std::filesystem::path& /*vocalsModelPath*/,
                                     const std::filesystem::path& /*accompanimentModelPath*/) {}
SpleeterSeparator::~SpleeterSeparator() = default;

bool SpleeterSeparator::isLoaded() const noexcept {
    return false;
}

std::vector<Stem> SpleeterSeparator::separate(const juce::AudioBuffer<float>& /*input*/,
                                              double /*sampleRate*/, const Progress& /*progress*/) {
    return {};
}

}  // namespace magda::stems

#endif  // MAGDA_HAVE_CLAP
