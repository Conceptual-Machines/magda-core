#include "plugins/MagdaConvolutionPlugin.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace magda::daw::audio {

namespace {

// The device's own state property names. `irFileData`, `name`, `normalise` and
// `trimSilence` deliberately match the retired Tracktion device's, which is how
// a migrated project keeps its impulse response (core/LegacyDeviceAliases.cpp).
const juce::Identifier kNameProp("name");
const juce::Identifier kNormaliseProp("normalise");
const juce::Identifier kTrimSilenceProp("trimSilence");
const juce::Identifier kIrFileDataProp("irFileData");

constexpr float kMinFrequency = 10.0f;
constexpr float kMaxFrequency = 20000.0f;
constexpr float kSmoothingSeconds = 0.01f;

/// The formats an IR can arrive in. The retired device read through the
/// engine's format manager; the basic JUCE set (WAV, AIFF, FLAC, OGG, and the
/// OS codecs where present) is the same list for the files an IR actually is,
/// and stays engine-neutral.
juce::AudioFormatManager& irFormats() {
    static juce::AudioFormatManager formats;
    [[maybe_unused]] static const bool registered = [] {
        formats.registerBasicFormats();
        return true;
    }();
    return formats;
}

/// One slot's metadata. The ids, order and normalised curves are pinned to
/// what the retired host-native plugin registered, because projects store
/// parameter values in the model in display units against these ranges and
/// saved automation addresses the normalised positions.
ParameterInfo slotInfo(int index) {
    ParameterInfo info;
    info.paramIndex = index;

    switch (index) {
        case MagdaConvolutionPlugin::kGain:
            info.stableId = "gain";
            info.name = "Gain";
            info.unit = "dB";
            // The retired range was {-12, 6} with setSkewForCentre(0): JUCE
            // applies real = min + span * normalized^(1/skew), and unity at
            // centre needs normalized^k = 2/3 at 0.5, so k = ln(2/3)/ln(1/2).
            info.scale = ParameterScale::Exponential;
            info.skewFactor = 0.5849625f;
            info.minValue = -12.0f;
            info.maxValue = 6.0f;
            info.defaultValue = 0.0f;
            break;

        case MagdaConvolutionPlugin::kLowCut:
            info.stableId = "lowCut";
            info.name = "Low Cut";
            info.unit = "Hz";
            // Affine in log2 of the frequency, exactly the curve the retired
            // device got from storing a MIDI note number over the same span.
            info.scale = ParameterScale::Logarithmic;
            info.minValue = kMinFrequency;
            info.maxValue = kMaxFrequency;
            info.defaultValue = kMinFrequency;
            break;

        case MagdaConvolutionPlugin::kHighCut:
            info.stableId = "highCut";
            info.name = "High Cut";
            info.unit = "Hz";
            info.scale = ParameterScale::Logarithmic;
            info.minValue = kMinFrequency;
            info.maxValue = kMaxFrequency;
            info.defaultValue = kMaxFrequency;
            break;

        case MagdaConvolutionPlugin::kMix:
            info.stableId = "mix";
            info.name = "Mix";
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.defaultValue = 1.0f;
            info.displayFormat = DisplayFormat::Percent;
            break;

        case MagdaConvolutionPlugin::kFilterQ:
            info.stableId = "filterQ";
            info.name = "Filter Q";
            info.minValue = 0.1f;
            info.maxValue = 14.0f;
            info.defaultValue = 1.0f / juce::MathConstants<float>::sqrt2;
            break;

        default:
            break;
    }

    return info;
}

/// The retired device's mix law, kept so a given Mix position sounds the same:
/// an equal-power-ish blend rather than a linear crossfade.
struct WetDryGain {
    float wet, dry;
};

WetDryGain wetDryFor(float mix) {
    const float dry = 1.0f - (mix * mix);
    const float inverse = 1.0f - mix;
    const float wet = 1.0f - (inverse * inverse);
    return {wet, dry};
}

}  // namespace

//==============================================================================
const char* MagdaConvolutionPlugin::xmlTypeName = "magda_convolution";

MagdaConvolutionPlugin::MagdaConvolutionPlugin() {
    for (int index = 0; index < kNumParams; ++index) {
        const auto info = slotInfo(index);
        domains_[static_cast<size_t>(index)] = ParameterUtils::domainOf(info);
        values_[static_cast<size_t>(index)] =
            ParameterUtils::realToNormalized(info.defaultValue, info);
    }
}

MagdaConvolutionPlugin::~MagdaConvolutionPlugin() = default;

ParameterInfo MagdaConvolutionPlugin::parameterInfo(int index) const {
    if (index < 0 || index >= kNumParams)
        return {};
    return slotInfo(index);
}

float MagdaConvolutionPlugin::parameterValue(int index) const {
    if (index < 0 || index >= kNumParams)
        return 0.0f;
    return values_[static_cast<size_t>(index)];
}

void MagdaConvolutionPlugin::setParameterValue(int index, float value) {
    if (index < 0 || index >= kNumParams)
        return;
    values_[static_cast<size_t>(index)] = juce::jlimit(0.0f, 1.0f, value);
}

float MagdaConvolutionPlugin::displayValue(int index) const {
    return ParameterUtils::normalizedToReal(values_[static_cast<size_t>(index)],
                                            domains_[static_cast<size_t>(index)]);
}

//==============================================================================
bool MagdaConvolutionPlugin::loadImpulseResponse(const juce::File& file) {
    juce::MemoryBlock fileData;
    if (!file.loadFileAsData(fileData))
        return false;

    return loadImpulseResponse(fileData.getData(), fileData.getSize());
}

bool MagdaConvolutionPlugin::loadImpulseResponse(const void* sourceData, size_t sourceDataSize) {
    if (sourceData == nullptr || sourceDataSize == 0)
        return false;

    auto stream = std::make_unique<juce::MemoryInputStream>(sourceData, sourceDataSize, false);
    std::unique_ptr<juce::AudioFormatReader> reader(irFormats().createReaderFor(std::move(stream)));
    if (reader == nullptr || reader->numChannels == 0 || reader->lengthInSamples == 0)
        return false;

    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels),
                                    static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);

    // Re-encode to FLAC: lossless, a fraction of the size of raw WAV inside the
    // project, and the format the retired device wrote, so an IR blob means the
    // same thing whichever device stored it.
    juce::MemoryBlock encoded;
    auto out = std::unique_ptr<juce::OutputStream>(
        std::make_unique<juce::MemoryOutputStream>(encoded, false));

    auto writer = juce::FlacAudioFormat().createWriterFor(
        out, juce::AudioFormatWriterOptions()
                 .withSampleRate(reader->sampleRate)
                 .withNumChannels(buffer.getNumChannels())
                 .withBitsPerSample(std::min(24, static_cast<int>(reader->bitsPerSample))));
    if (writer == nullptr)
        return false;

    if (!writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()))
        return false;

    writer.reset();  // flush before the block is moved into the device state

    irData_ = std::move(encoded);
    loadImpulseResponseFromData();
    return true;
}

void MagdaConvolutionPlugin::prepare(const DevicePrepareContext& context) {
    sampleRate_ = context.sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = context.sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(std::max(1, context.maximumBlockSize));
    spec.numChannels = 2;
    chain_.prepare(spec);

    dryBuffer_.setSize(2, std::max(1, context.maximumBlockSize));

    lowCutSmoother_.setTargetValue(displayValue(kLowCut));
    highCutSmoother_.setTargetValue(displayValue(kHighCut));
    gainSmoother_.setTargetValue(displayValue(kGain));
    filterQSmoother_.setTargetValue(displayValue(kFilterQ));

    const auto levels = wetDryFor(displayValue(kMix));
    wetSmoother_.setTargetValue(levels.wet);
    drySmoother_.setTargetValue(levels.dry);

    for (auto* smoother : {&lowCutSmoother_, &highCutSmoother_, &gainSmoother_, &filterQSmoother_,
                           &wetSmoother_, &drySmoother_})
        smoother->reset(context.sampleRate, kSmoothingSeconds);
}

void MagdaConvolutionPlugin::reset() {
    chain_.reset();
}

void MagdaConvolutionPlugin::process(DeviceProcessContext& context) {
    if (context.audio == nullptr || context.numSamples <= 0)
        return;

    auto& buffer = *context.audio;
    const int start = context.startSample;
    const int numSamples = context.numSamples;

    lowCutSmoother_.setTargetValue(displayValue(kLowCut));
    highCutSmoother_.setTargetValue(displayValue(kHighCut));
    gainSmoother_.setTargetValue(displayValue(kGain));
    filterQSmoother_.setTargetValue(displayValue(kFilterQ));

    const auto levels = wetDryFor(displayValue(kMix));
    wetSmoother_.setTargetValue(levels.wet);
    drySmoother_.setTargetValue(levels.dry);

    // The dry copy has to be taken before the chain writes over the buffer.
    // dryBuffer_ was sized in prepare(), so this does not allocate.
    const int numChannels = std::min(buffer.getNumChannels(), dryBuffer_.getNumChannels());
    for (int channel = 0; channel < numChannels; ++channel)
        dryBuffer_.copyFrom(channel, 0, buffer, channel, start, numSamples);

    auto& highPass = chain_.get<kHighPass>().state;
    auto& lowPass = chain_.get<kLowPass>().state;
    auto& gain = chain_.get<kOutputGain>();

    const bool smoothing = gainSmoother_.isSmoothing() || lowCutSmoother_.isSmoothing() ||
                           highCutSmoother_.isSmoothing() || filterQSmoother_.isSmoothing();

    const auto updateCoefficients = [&](int samplesThisTime) {
        const auto q = filterQSmoother_.skip(samplesThisTime);
        *highPass = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass(
            sampleRate_, lowCutSmoother_.skip(samplesThisTime), q);
        *lowPass = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass(
            sampleRate_, highCutSmoother_.skip(samplesThisTime), q);
        gain.setGainLinear(juce::Decibels::decibelsToGain(gainSmoother_.skip(samplesThisTime)));
    };

    if (smoothing) {
        // Re-derive the coefficients every 32 samples so a moving control does
        // not step; the convolution itself is unaffected by the sub-blocking.
        constexpr int kControlBlock = 32;
        for (int done = 0; done < numSamples;) {
            const int numThisTime = std::min(kControlBlock, numSamples - done);
            updateCoefficients(numThisTime);

            auto block = juce::dsp::AudioBlock<float>(buffer).getSubBlock(
                static_cast<size_t>(start + done), static_cast<size_t>(numThisTime));
            juce::dsp::ProcessContextReplacing<float> processContext(block);
            chain_.process(processContext);

            done += numThisTime;
        }
    } else {
        updateCoefficients(numSamples);

        auto block = juce::dsp::AudioBlock<float>(buffer).getSubBlock(
            static_cast<size_t>(start), static_cast<size_t>(numSamples));
        juce::dsp::ProcessContextReplacing<float> processContext(block);
        chain_.process(processContext);
    }

    // Gain has to be applied over the render region, not from sample zero: a
    // SmoothedValue advances once across the whole buffer it is handed, so the
    // views below keep it in step with the samples actually being rendered.
    juce::AudioBuffer<float> wetRegion(buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                                       start, numSamples);
    wetSmoother_.applyGain(wetRegion, numSamples);

    // Fully wet needs no dry pass, and the smoother is parked at zero there.
    if (drySmoother_.getCurrentValue() > 0.0f || drySmoother_.getTargetValue() > 0.0f) {
        juce::AudioBuffer<float> dryRegion(dryBuffer_.getArrayOfWritePointers(), numChannels, 0,
                                           numSamples);
        drySmoother_.applyGain(dryRegion, numSamples);

        for (int channel = 0; channel < numChannels; ++channel)
            buffer.addFrom(channel, start, dryBuffer_, channel, 0, numSamples);
    }
}

//==============================================================================
void MagdaConvolutionPlugin::flushState(juce::ValueTree& state) {
    state.setProperty(kNameProp, irName_, nullptr);
    state.setProperty(kNormaliseProp, normalise_, nullptr);
    state.setProperty(kTrimSilenceProp, trimSilence_, nullptr);

    // Only written when the device holds an IR. A device with none leaves the
    // property alone rather than removing it: the retired device stored the
    // blob directly on this tree, and state written there behind the device's
    // back (tests do; a failed decode could) must survive a flush.
    if (irData_.getSize() > 0)
        state.setProperty(kIrFileDataProp, juce::var(irData_), nullptr);
}

void MagdaConvolutionPlugin::restoreState(const juce::ValueTree& state) {
    if (const auto* name = state.getPropertyPointer(kNameProp))
        irName_ = name->toString();
    if (const auto* normalise = state.getPropertyPointer(kNormaliseProp))
        normalise_ = static_cast<bool>(*normalise);
    if (const auto* trimSilence = state.getPropertyPointer(kTrimSilenceProp))
        trimSilence_ = static_cast<bool>(*trimSilence);

    if (const auto* irFileData = state.getProperty(kIrFileDataProp).getBinaryData()) {
        irData_ = *irFileData;
        loadImpulseResponseFromData();
    }
}

void MagdaConvolutionPlugin::loadImpulseResponseFromData() {
    if (irData_.getSize() == 0) {
        irLengthSeconds_.store(0.0, std::memory_order_relaxed);
        return;
    }

    // Read through a format manager rather than assuming FLAC: the blob is
    // whatever encoded the IR when it was stored.
    auto stream = std::make_unique<juce::MemoryInputStream>(irData_, false);
    std::unique_ptr<juce::AudioFormatReader> reader(irFormats().createReaderFor(std::move(stream)));
    if (reader == nullptr || reader->numChannels == 0 || reader->lengthInSamples == 0)
        return;

    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels),
                                    static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);

    irLengthSeconds_.store(reader->sampleRate > 0.0
                               ? static_cast<double>(reader->lengthInSamples) / reader->sampleRate
                               : 0.0,
                           std::memory_order_relaxed);

    chain_.get<kConvolution>().loadImpulseResponse(
        std::move(buffer), reader->sampleRate,
        reader->numChannels > 1 ? juce::dsp::Convolution::Stereo::yes
                                : juce::dsp::Convolution::Stereo::no,
        trimSilence_ ? juce::dsp::Convolution::Trim::yes : juce::dsp::Convolution::Trim::no,
        normalise_ ? juce::dsp::Convolution::Normalise::yes
                   : juce::dsp::Convolution::Normalise::no);
}

}  // namespace magda::daw::audio
