#include "plugins/MagdaConvolutionPlugin.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace magda::daw::audio {

const juce::Identifier MagdaConvolutionPlugin::StateIDs::name("name");
const juce::Identifier MagdaConvolutionPlugin::StateIDs::normalise("normalise");
const juce::Identifier MagdaConvolutionPlugin::StateIDs::trimSilence("trimSilence");
const juce::Identifier MagdaConvolutionPlugin::StateIDs::irFileData("irFileData");

namespace {

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

bool MagdaConvolutionPlugin::encodeImpulseResponse(const void* sourceData, size_t sourceDataSize,
                                                   juce::MemoryBlock& outEncoded) {
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

    outEncoded = std::move(encoded);
    return true;
}

bool MagdaConvolutionPlugin::loadImpulseResponse(const void* sourceData, size_t sourceDataSize) {
    juce::MemoryBlock encoded;
    if (!encodeImpulseResponse(sourceData, sourceDataSize, encoded))
        return false;

    irData_ = std::move(encoded);
    loadImpulseResponseFromData();
    return true;
}

void MagdaConvolutionPlugin::prepare(const DevicePrepareContext& context) {
    sampleRate_ = context.sampleRate;

    // restoreState() runs before the first prepare() in both hosts, so a
    // device restored with no IR queued its pass-through dirac stamped at
    // the 44100 default (#2360). juce::dsp::Convolution loads impulse
    // responses through a background queue, but chain_.prepare() below pops
    // it synchronously before rebuilding the convolution engine for the
    // now-real sampleRate_ - reinstalling here, before that pop, replaces
    // the stale stamp in time for that same rebuild, rather than racing a
    // background thread to replace it after. Skipped once a real IR is
    // loaded: its source rate came from the file, not this default.
    if (irData_.getSize() == 0)
        loadImpulseResponseFromData();

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
    state.setProperty(StateIDs::name, irName_, nullptr);
    state.setProperty(StateIDs::normalise, normalise_, nullptr);
    state.setProperty(StateIDs::trimSilence, trimSilence_, nullptr);

    // The property tracks the device exactly: written when it holds an IR,
    // REMOVED when it does not. Leaving a stale blob on the engine tree would
    // let a capture write an un-loaded IR back into the model after an undo
    // (#2317 review). restoreState() now owns the absent-means-none contract,
    // so nothing legitimate parks state here behind the device's back.
    if (irData_.getSize() > 0)
        state.setProperty(StateIDs::irFileData, juce::var(irData_), nullptr);
    else
        state.removeProperty(StateIDs::irFileData, nullptr);
}

void MagdaConvolutionPlugin::restoreState(const juce::ValueTree& state) {
    if (const auto* name = state.getPropertyPointer(StateIDs::name))
        irName_ = name->toString();
    if (const auto* normalise = state.getPropertyPointer(StateIDs::normalise))
        normalise_ = static_cast<bool>(*normalise);
    if (const auto* trimSilence = state.getPropertyPointer(StateIDs::trimSilence))
        trimSilence_ = static_cast<bool>(*trimSilence);

    // The document is the whole authored state, so no `irFileData` MEANS no
    // impulse response - restoring a document saved before the first IR load
    // (an undo of that load, a preset with none) has to unload the current
    // one, or the model says "no IR" while playback keeps convolving with it
    // (#2317 review). An empty blob reads the same way.
    if (const auto* irFileData = state.getProperty(StateIDs::irFileData).getBinaryData()) {
        irData_ = *irFileData;
    } else {
        irData_ = {};
        if (!state.hasProperty(StateIDs::name))
            irName_.clear();
    }
    loadImpulseResponseFromData();
}

void MagdaConvolutionPlugin::loadImpulseResponseFromData() {
    if (irData_.getSize() == 0) {
        irLengthSeconds_.store(0.0, std::memory_order_relaxed);
        // dsp::Convolution has no unload, but a single-sample unit impulse IS
        // the identity: loading it returns the stage to the pass-through a
        // fresh convolution gives, so "no IR" sounds the same whether the
        // device never loaded one or just un-loaded it (#2317 review).
        juce::AudioBuffer<float> dirac(1, 1);
        dirac.setSample(0, 0, 1.0f);
        chain_.get<kConvolution>().loadImpulseResponse(
            std::move(dirac), sampleRate_ > 0.0 ? sampleRate_ : 44100.0,
            juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::no,
            juce::dsp::Convolution::Normalise::no);
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
