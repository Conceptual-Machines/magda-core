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
    std::vector<float> out(static_cast<size_t>(std::max(outLen, 0)), 0.0F);
    if (outLen <= 0)
        return out;
    juce::LagrangeInterpolator interp;
    interp.process(ratio, in, out.data(), outLen);
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

    // Full complex spectra per channel (for mask application) and the
    // magnitude tensor both models consume.
    std::array<std::vector<float>, 2> spectra;  // numFrames x kNumBins x (re,im)
    std::vector<float> x(static_cast<size_t>(2) * paddedFrames * sp::kKeptBins, 0.0F);

    std::vector<float> frame(static_cast<size_t>(sp::kFftSize) * 2);
    for (int ch = 0; ch < 2; ++ch) {
        spectra[static_cast<size_t>(ch)].assign(static_cast<size_t>(numFrames) * sp::kNumBins * 2,
                                                0.0F);

        for (int t = 0; t < numFrames; ++t) {
            const int start = t * sp::kHop;
            std::fill(frame.begin(), frame.end(), 0.0F);
            for (int i = 0; i < sp::kFftSize; ++i) {
                const int pos = start + i;
                if (pos < total)
                    frame[static_cast<size_t>(i)] =
                        signal[static_cast<size_t>(ch)][static_cast<size_t>(pos)] *
                        window[static_cast<size_t>(i)];
            }
            fft.performRealOnlyForwardTransform(frame.data(), true);

            for (int b = 0; b < sp::kNumBins; ++b) {
                const float re = frame[static_cast<size_t>(2 * b)];
                const float im = frame[static_cast<size_t>(2 * b + 1)];
                const size_t sIdx =
                    (static_cast<size_t>(t) * sp::kNumBins + static_cast<size_t>(b)) * 2;
                spectra[static_cast<size_t>(ch)][sIdx] = re;
                spectra[static_cast<size_t>(ch)][sIdx + 1] = im;
                if (b < sp::kKeptBins) {
                    const size_t xIdx =
                        (static_cast<size_t>(ch) * paddedFrames + static_cast<size_t>(t)) *
                            sp::kKeptBins +
                        static_cast<size_t>(b);
                    x[xIdx] = std::sqrt(re * re + im * im);
                }
            }
        }
        if (!report(0.1F + 0.05F * static_cast<float>(ch)))
            return {};
    }

    // Both UNets consume the identical tensor.
    std::vector<float> vocalsMag;
    std::vector<float> accompMag;
    try {
        vocalsMag = impl_->run(impl_->vocals, x, numSplits);
        if (!report(0.5F))
            return {};
        accompMag = impl_->run(impl_->accompaniment, x, numSplits);
    } catch (const Ort::Exception&) {
        return {};
    }
    if (!report(0.8F))
        return {};

    // Wiener-combined soft masks applied to the full complex STFT; bins the
    // model never saw (>= 1024, incl. Nyquist) are zeroed, as in the
    // reference. Reconstruction is HPSS-style weight-normalized overlap-add.
    std::vector<float> norm(static_cast<size_t>(paddedLen), 0.0F);
    for (int t = 0; t < numFrames; ++t) {
        const int start = t * sp::kHop;
        for (int i = 0; i < sp::kFftSize && start + i < paddedLen; ++i)
            norm[static_cast<size_t>(start + i)] +=
                window[static_cast<size_t>(i)] * window[static_cast<size_t>(i)];
    }

    const auto names = stemNames();
    std::vector<Stem> result(2);
    std::array<std::vector<float>, 2> out;  // current stem, per channel

    for (int s = 0; s < 2; ++s) {
        result[static_cast<size_t>(s)].name = names[static_cast<size_t>(s)];
        result[static_cast<size_t>(s)].audio.setSize(numChannels, numSamples);
        result[static_cast<size_t>(s)].audio.clear();

        for (auto& ch : out)
            ch.assign(static_cast<size_t>(paddedLen), 0.0F);

        for (int ch = 0; ch < 2; ++ch) {
            for (int t = 0; t < numFrames; ++t) {
                std::fill(frame.begin(), frame.end(), 0.0F);
                for (int b = 0; b < sp::kKeptBins; ++b) {
                    const size_t yIdx =
                        (static_cast<size_t>(ch) * paddedFrames + static_cast<size_t>(t)) *
                            sp::kKeptBins +
                        static_cast<size_t>(b);
                    const float v = vocalsMag[yIdx];
                    const float a = accompMag[yIdx];
                    const float sum = v * v + a * a + 1.0e-10F;
                    const float mask = ((s == 0 ? v * v : a * a) + 0.5e-10F) / sum;

                    const size_t sIdx =
                        (static_cast<size_t>(t) * sp::kNumBins + static_cast<size_t>(b)) * 2;
                    frame[static_cast<size_t>(2 * b)] =
                        spectra[static_cast<size_t>(ch)][sIdx] * mask;
                    frame[static_cast<size_t>(2 * b + 1)] =
                        spectra[static_cast<size_t>(ch)][sIdx + 1] * mask;
                }

                fft.performRealOnlyInverseTransform(frame.data());

                const int start = t * sp::kHop;
                for (int i = 0; i < sp::kFftSize && start + i < paddedLen; ++i)
                    out[static_cast<size_t>(ch)][static_cast<size_t>(start + i)] +=
                        frame[static_cast<size_t>(i)] * window[static_cast<size_t>(i)];
            }

            for (int i = 0; i < paddedLen; ++i) {
                const float n = norm[static_cast<size_t>(i)];
                out[static_cast<size_t>(ch)][static_cast<size_t>(i)] *=
                    n > 1.0e-8F ? 1.0F / n : 0.0F;
            }
        }

        for (int ch = 0; ch < numChannels; ++ch) {
            const auto& src = out[static_cast<size_t>(ch % 2)];
            std::vector<float> back =
                resampleChannel(src.data(), total, sp::kSampleRate, sampleRate);
            const int copyLen = std::min(numSamples, static_cast<int>(back.size()));
            result[static_cast<size_t>(s)].audio.copyFrom(ch, 0, back.data(), copyLen);
        }

        if (!report(0.8F + 0.1F * static_cast<float>(s + 1)))
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
