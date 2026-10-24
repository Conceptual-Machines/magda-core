#include "devices/spectral/SpectralFx.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace magda::devices::spectral {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr int kOverlap = 4;
constexpr int kChunk = 256;

sdk::ParameterDescriptor slotInfo(int index) {
    sdk::ParameterDescriptor info;
    info.index = index;

    switch (index) {
        case SpectralFx::kGateParamIndex:
            info.stableId = "gate";
            info.name = "Gate";
            info.unit = "dB";
            info.minValue = SpectralFx::kGateOffDb;
            info.maxValue = 0.0f;
            info.defaultValue = SpectralFx::kGateOffDb;
            info.tooltip = "Mutes the bins quieter than this";
            break;

        case SpectralFx::kContrastParamIndex:
            info.stableId = "contrast";
            info.name = "Contrast";
            info.unit = "%";
            info.minValue = -100.0f;
            info.maxValue = 100.0f;
            info.defaultValue = 0.0f;
            info.tooltip = "Below zero flattens the spectrum, above zero exaggerates its peaks";
            break;

        case SpectralFx::kShiftParamIndex:
            info.stableId = "shift";
            info.name = "Shift";
            info.unit = "Hz";
            info.minValue = -2000.0f;
            info.maxValue = 2000.0f;
            info.defaultValue = 0.0f;
            break;

        case SpectralFx::kScrambleParamIndex:
            info.stableId = "scramble";
            info.name = "Scramble";
            info.unit = "%";
            info.minValue = 0.0f;
            info.maxValue = 100.0f;
            info.defaultValue = 0.0f;
            info.tooltip = "Randomises each bin's phase, smearing the sound";
            break;

        case SpectralFx::kMixParamIndex:
            info.stableId = "mix";
            info.name = "Mix";
            info.unit = "%";
            info.minValue = 0.0f;
            info.maxValue = 100.0f;
            info.defaultValue = 100.0f;
            break;

        case SpectralFx::kOutputParamIndex:
            info.stableId = "output";
            info.name = "Output";
            info.unit = "dB";
            info.minValue = -24.0f;
            info.maxValue = 24.0f;
            info.defaultValue = 0.0f;
            break;

        default:
            break;
    }

    return info;
}

}  // namespace

SpectralFx::SpectralFx() {
    for (int index = 0; index < kParamCount; ++index) {
        const auto info = slotInfo(index);
        domains_[static_cast<size_t>(index)] = sdk::domainOf(info);
        values_[static_cast<size_t>(index)] =
            sdk::realToNormalized(info.defaultValue, sdk::domainOf(info));
    }
}

sdk::ParameterDescriptor SpectralFx::parameterDescriptor(int index) const {
    if (index < 0 || index >= kParamCount)
        return {};
    return slotInfo(index);
}

float SpectralFx::parameterValue(int index) const {
    if (index < 0 || index >= kParamCount)
        return 0.0f;
    return values_[static_cast<size_t>(index)];
}

void SpectralFx::setParameterValue(int index, float value) {
    if (index < 0 || index >= kParamCount)
        return;
    values_[static_cast<size_t>(index)] = std::clamp(value, 0.0f, 1.0f);
}

float SpectralFx::displayValue(int index) const {
    return sdk::normalizedToReal(values_[static_cast<size_t>(index)],
                                 domains_[static_cast<size_t>(index)]);
}

void SpectralFx::prepare(const sdk::PrepareContext& context) {
    sampleRate_ = context.sampleRate > 0.0 ? context.sampleRate : 44100.0;
    // Keeps the frame near 46 ms, so the smear and the shift resolution feel the same at any rate.
    const int fftSize = sampleRate_ > 50000.0 ? 4096 : 2048;
    stft_.prepare(fftSize, kOverlap, kMaxChannels);

    const auto bins = static_cast<size_t>(stft_.bins());
    magnitude_.assign(bins, 0.0f);
    phase_.assign(bins, 0.0f);
    shiftedMagnitude_.assign(bins, 0.0f);
    shiftedPhase_.assign(bins, 0.0f);

    const double binHz = sampleRate_ / fftSize;
    const double ratio = static_cast<double>(kMaxHz) / kMinHz;
    for (int band = 0; band < kColumnBands; ++band) {
        const double lowHz = kMinHz * std::pow(ratio, static_cast<double>(band) / kColumnBands);
        const double highHz =
            kMinHz * std::pow(ratio, static_cast<double>(band + 1) / kColumnBands);
        const int low =
            std::clamp(static_cast<int>(std::lround(lowHz / binHz)), 0, stft_.bins() - 1);
        const auto b = static_cast<size_t>(band);
        bandLow_[b] = low;
        bandHigh_[b] =
            std::clamp(static_cast<int>(std::lround(highHz / binHz)), low + 1, stft_.bins());
    }
    pendingColumns_.assign(static_cast<size_t>(kMaxPendingColumns * kColumnBands), 0.0f);
    reset();
}

void SpectralFx::reset() {
    stft_.reset();
    shiftPhase_.fill(0.0);
    const float mix = displayValue(kMixParamIndex) * 0.01f;
    const float gain = std::pow(10.0f, displayValue(kOutputParamIndex) / 20.0f);
    wetGain_ = mix * gain;
    dryGain_ = (1.0f - mix) * gain;
}

SpectralFx::FrameSettings SpectralFx::frameSettings() const {
    FrameSettings settings;

    const float gateDb = displayValue(kGateParamIndex);
    if (gateDb > kGateOffDb)
        settings.gateThreshold = std::pow(10.0f, gateDb / 20.0f);

    // -100..100 % maps to an exponent of 1/4..4 on the bin magnitudes.
    settings.contrastExponent = std::exp2(2.0f * displayValue(kContrastParamIndex) * 0.01f);

    const double shiftHz = displayValue(kShiftParamIndex);
    const double binHz = sampleRate_ / stft_.fftSize();
    settings.shiftBins = static_cast<int>(std::lround(shiftHz / binHz));
    settings.shiftPhasePerFrame = kTwoPi * shiftHz * stft_.hopSize() / sampleRate_;

    settings.scramble = displayValue(kScrambleParamIndex) * 0.01f;
    return settings;
}

void SpectralFx::process(sdk::ProcessContext& context) {
    const int numSamples = context.numSamples();
    const int numChannels = std::min(context.audio.numChannels(), kMaxChannels);
    if (numSamples <= 0 || stft_.fftSize() == 0)
        return;

    const auto settings = frameSettings();
    blockFrames_.fill(0);
    const auto onFrame = [this, &settings](int channel, float* spectrum) {
        processFrame(channel, spectrum, settings);
        collectColumn(channel, spectrum);
    };

    const float mix = displayValue(kMixParamIndex) * 0.01f;
    const float gain = std::pow(10.0f, displayValue(kOutputParamIndex) / 20.0f);
    const float wetTarget = mix * gain;
    const float dryTarget = (1.0f - mix) * gain;
    const float wetStep = (wetTarget - wetGain_) / static_cast<float>(numSamples);
    const float dryStep = (dryTarget - dryGain_) / static_cast<float>(numSamples);

    float dry[kChunk];
    for (int ch = 0; ch < numChannels; ++ch) {
        float* samples = context.audio.channel(ch);
        for (int done = 0; done < numSamples; done += kChunk) {
            const int chunk = std::min(kChunk, numSamples - done);
            float* block = samples + done;
            stft_.process(ch, block, block, dry, chunk, onFrame);

            for (int i = 0; i < chunk; ++i) {
                const auto t = static_cast<float>(done + i);
                block[i] = block[i] * (wetGain_ + wetStep * t) + dry[i] * (dryGain_ + dryStep * t);
            }
        }
    }
    wetGain_ = wetTarget;
    dryGain_ = dryTarget;
    publishColumns(numChannels);
}

void SpectralFx::collectColumn(int channel, const float* spectrum) {
    const int frame = blockFrames_[static_cast<size_t>(channel)]++;
    if (frame >= kMaxPendingColumns)
        return;

    float* column = pendingColumns_.data() + static_cast<size_t>(frame) * kColumnBands;
    for (size_t band = 0; band < kColumnBands; ++band) {
        float peak = 0.0f;
        for (int b = bandLow_[band]; b < bandHigh_[band]; ++b) {
            const float re = spectrum[2 * static_cast<size_t>(b)];
            const float im = spectrum[2 * static_cast<size_t>(b) + 1];
            peak = std::max(peak, re * re + im * im);
        }
        column[band] += std::sqrt(peak);
    }
}

void SpectralFx::publishColumns(int numChannels) {
    const int columns = std::min(kMaxPendingColumns, blockFrames_[0]);
    const float scale = stft_.magnitudeScale() / static_cast<float>(std::max(1, numChannels));
    for (int c = 0; c < columns; ++c) {
        float* column = pendingColumns_.data() + static_cast<size_t>(c) * kColumnBands;
        for (size_t band = 0; band < kColumnBands; ++band) {
            const float level = column[band] * scale;
            column[band] = level > 0.0f ? std::max(kFloorDb, 20.0f * std::log10(level)) : kFloorDb;
        }
        spectrogram_.write(column, kColumnBands);
        std::fill_n(column, kColumnBands, 0.0f);
    }
}

void SpectralFx::processFrame(int channel, float* spectrum, const FrameSettings& settings) {
    const int bins = stft_.bins();
    auto& framePhase = shiftPhase_[static_cast<size_t>(channel)];
    framePhase = std::fmod(framePhase + settings.shiftPhasePerFrame, kTwoPi);

    const bool contrast = std::abs(settings.contrastExponent - 1.0f) > 1.0e-4f;
    const bool shift = settings.shiftPhasePerFrame != 0.0;
    if (settings.gateThreshold <= 0.0f && !contrast && !shift && settings.scramble <= 0.0f)
        return;

    float peak = 0.0f;
    for (size_t b = 0; b < magnitude_.size(); ++b) {
        const float re = spectrum[2 * b];
        const float im = spectrum[2 * b + 1];
        magnitude_[b] = std::sqrt(re * re + im * im);
        phase_[b] = std::atan2(im, re);
    }

    if (settings.gateThreshold > 0.0f) {
        const float floor = settings.gateThreshold / stft_.magnitudeScale();
        for (auto& m : magnitude_)
            if (m < floor)
                m = 0.0f;
    }

    if (contrast) {
        double energyBefore = 0.0;
        for (const float m : magnitude_) {
            peak = std::max(peak, m);
            energyBefore += static_cast<double>(m) * m;
        }
        // Shaped against the peak so the power never overflows; the frame keeps its energy.
        if (peak > 1.0e-9f) {
            double energyAfter = 0.0;
            for (auto& m : magnitude_) {
                m = peak * std::pow(m / peak, settings.contrastExponent);
                energyAfter += static_cast<double>(m) * m;
            }
            if (energyAfter > 1.0e-18) {
                const auto scale = static_cast<float>(std::sqrt(energyBefore / energyAfter));
                for (auto& m : magnitude_)
                    m *= scale;
            }
        }
    }

    const float* outMagnitude = magnitude_.data();
    const float* outPhase = phase_.data();
    if (shift) {
        // Bins move by whole steps; the per-frame phase advance carries the exact offset in Hz.
        std::ranges::fill(shiftedMagnitude_, 0.0f);
        const int from = std::max(0, -settings.shiftBins);
        const int to = std::min(bins, bins - settings.shiftBins);
        const auto rotation = static_cast<float>(framePhase);
        for (int b = from; b < to; ++b) {
            const auto target = static_cast<size_t>(b + settings.shiftBins);
            shiftedMagnitude_[target] = magnitude_[static_cast<size_t>(b)];
            shiftedPhase_[target] = phase_[static_cast<size_t>(b)] + rotation;
        }
        outMagnitude = shiftedMagnitude_.data();
        outPhase = shiftedPhase_.data();
    }

    const auto pi = std::numbers::pi_v<float>;
    // Jitter of +-a*pi keeps sinc(a) of the signal coherent; the rest adds at incoherentGain().
    float scrambleGain = 1.0f;
    if (settings.scramble > 0.0f) {
        const float spread = settings.scramble * pi;
        const float coherent = std::sin(spread) / spread;
        const float k = stft_.incoherentGain();
        scrambleGain = 1.0f / std::sqrt(coherent * coherent + (1.0f - coherent * coherent) * k * k);
    }
    for (size_t b = 0; b < magnitude_.size(); ++b) {
        float ph = outPhase[b];
        if (settings.scramble > 0.0f)
            ph += settings.scramble * pi * (2.0f * random_.nextFloat() - 1.0f);
        const float m = outMagnitude[b] * scrambleGain;
        spectrum[2 * b] = m * std::cos(ph);
        spectrum[2 * b + 1] = m * std::sin(ph);
    }
    // DC and Nyquist are real.
    spectrum[1] = 0.0f;
    spectrum[2 * magnitude_.size() - 1] = 0.0f;
}

}  // namespace magda::devices::spectral
