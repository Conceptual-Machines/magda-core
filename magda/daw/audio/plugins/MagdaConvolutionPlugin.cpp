#include "plugins/MagdaConvolutionPlugin.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

namespace magda::daw::audio {

namespace te = tracktion::engine;

namespace {

// The device's own state property names. `irFileData`, `name`, `normalise` and
// `trimSilence` deliberately match the retired Tracktion device's, which is how
// a migrated project keeps its impulse response (core/LegacyDeviceAliases.cpp).
const juce::Identifier kGainProp("gain");
const juce::Identifier kLowCutProp("lowCut");
const juce::Identifier kHighCutProp("highCut");
const juce::Identifier kMixProp("mix");
const juce::Identifier kFilterQProp("filterQ");

constexpr float kMinFrequency = 10.0f;
constexpr float kMaxFrequency = 20000.0f;
constexpr float kSmoothingSeconds = 0.01f;

/// Cutoff range, mapped so that a normalised value is affine in log2 of the
/// frequency. That is exactly the mapping the retired device got from storing a
/// MIDI note number over the same 10 Hz - 20 kHz span, so saved automation and
/// macro positions mean the same frequency after the migration.
juce::NormalisableRange<float> frequencyRange() {
    return {kMinFrequency, kMaxFrequency,
            [](float start, float end, float normalised) {
                return start * std::pow(end / start, normalised);
            },
            [](float start, float end, float value) {
                return std::log(juce::jmax(start, value) / start) / std::log(end / start);
            },
            [](float start, float end, float value) { return juce::jlimit(start, end, value); }};
}

juce::NormalisableRange<float> gainRange() {
    juce::NormalisableRange<float> range{-12.0f, 6.0f};
    range.setSkewForCentre(0.0f);
    return range;
}

juce::String frequencyToString(float hz) {
    if (hz >= 1000.0f)
        return juce::String(hz / 1000.0f, 2) + " kHz";
    return juce::String(juce::roundToInt(hz)) + " Hz";
}

float frequencyFromString(const juce::String& text) {
    const auto trimmed = text.trim().toLowerCase();
    const auto number = trimmed.retainCharacters("0123456789.-").getFloatValue();
    return trimmed.contains("k") ? number * 1000.0f : number;
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

MagdaConvolutionPlugin::MagdaConvolutionPlugin(const te::PluginCreationInfo& info) : Plugin(info) {
    auto* um = getUndoManager();

    irName.referTo(state, te::IDs::name, um);
    normalise.referTo(state, te::IDs::normalise, um, true);
    trimSilence.referTo(state, te::IDs::trimSilence, um, false);

    gainValue.referTo(state, kGainProp, um, 0.0f);
    lowCutValue.referTo(state, kLowCutProp, um, kMinFrequency);
    highCutValue.referTo(state, kHighCutProp, um, kMaxFrequency);
    mixValue.referTo(state, kMixProp, um, 1.0f);
    filterQValue.referTo(state, kFilterQProp, um, 1.0f / juce::MathConstants<float>::sqrt2);

    gainParam = addParam(
        "gain", TRANS("Gain"), gainRange(),
        [](float value) { return juce::Decibels::toString(value); },
        [](const juce::String& s) { return s.getFloatValue(); });
    gainParam->attachToCurrentValue(gainValue);

    lowCutParam = addParam("lowCut", TRANS("Low Cut"), frequencyRange(), frequencyToString,
                           frequencyFromString);
    lowCutParam->attachToCurrentValue(lowCutValue);

    highCutParam = addParam("highCut", TRANS("High Cut"), frequencyRange(), frequencyToString,
                            frequencyFromString);
    highCutParam->attachToCurrentValue(highCutValue);

    mixParam = addParam(
        "mix", TRANS("Mix"), {0.0f, 1.0f, 0.0f},
        [](float value) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; },
        [](const juce::String& s) { return s.getFloatValue() / 100.0f; });
    mixParam->attachToCurrentValue(mixValue);

    filterQParam = addParam(
        "filterQ", TRANS("Filter Q"), {0.1f, 14.0f, 0.0f},
        [](float value) { return juce::String(value, 2); },
        [](const juce::String& s) { return s.getFloatValue(); });
    filterQParam->attachToCurrentValue(filterQValue);

    loadImpulseResponseFromState();
}

MagdaConvolutionPlugin::~MagdaConvolutionPlugin() {
    notifyListenersOfDeletion();

    for (auto& param : {gainParam, lowCutParam, highCutParam, mixParam, filterQParam})
        if (param != nullptr)
            param->detachFromCurrentValue();
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
    auto& formats = engine.getAudioFileFormatManager().readFormatManager;

    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::move(stream)));
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

    writer.reset();  // flush before the block is moved into the state

    state.setProperty(te::IDs::irFileData, juce::var(std::move(encoded)), getUndoManager());
    return true;
}

//==============================================================================
double MagdaConvolutionPlugin::getLatencySeconds() {
    // Zero, by construction: the convolution is built in JUCE's default
    // configuration, which is uniform partitioned and zero latency, so every
    // engine it installs reports a latency of zero. Reading the live engine
    // back instead would race the audio thread's pointer swap for a number that
    // cannot change. Anything that gives the device latency - a fixed-latency
    // engine - has to update this too.
    return 0.0;
}

double MagdaConvolutionPlugin::getTailLength() const {
    // A convolution rings for exactly the length of its impulse response, plus
    // the filters' own short decay. Declaring it is what lets an offline render
    // or a freeze keep the reverb's tail instead of cutting it at the last note.
    return irLengthSeconds_.load(std::memory_order_relaxed);
}

void MagdaConvolutionPlugin::initialise(const te::PluginInitialisationInfo& info) {
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = info.sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(info.blockSizeSamples);
    spec.numChannels = 2;
    chain_.prepare(spec);

    lowCutSmoother_.setTargetValue(lowCutParam->getCurrentValue());
    highCutSmoother_.setTargetValue(highCutParam->getCurrentValue());
    gainSmoother_.setTargetValue(gainParam->getCurrentValue());
    filterQSmoother_.setTargetValue(filterQParam->getCurrentValue());

    const auto levels = wetDryFor(mixParam->getCurrentValue());
    wetSmoother_.setTargetValue(levels.wet);
    drySmoother_.setTargetValue(levels.dry);

    for (auto* smoother : {&lowCutSmoother_, &highCutSmoother_, &gainSmoother_, &filterQSmoother_,
                           &wetSmoother_, &drySmoother_})
        smoother->reset(info.sampleRate, kSmoothingSeconds);
}

void MagdaConvolutionPlugin::deinitialise() {}

void MagdaConvolutionPlugin::reset() {
    chain_.reset();
}

void MagdaConvolutionPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (fc.destBuffer == nullptr || fc.bufferNumSamples <= 0)
        return;

    lowCutSmoother_.setTargetValue(lowCutParam->getCurrentValue());
    highCutSmoother_.setTargetValue(highCutParam->getCurrentValue());
    gainSmoother_.setTargetValue(gainParam->getCurrentValue());
    filterQSmoother_.setTargetValue(filterQParam->getCurrentValue());

    const auto levels = wetDryFor(mixParam->getCurrentValue());
    wetSmoother_.setTargetValue(levels.wet);
    drySmoother_.setTargetValue(levels.dry);

    // The dry copy has to be taken before the chain writes over destBuffer.
    // AudioScratchBuffer is a pooled buffer, so this does not allocate.
    te::AudioScratchBuffer dry(*fc.destBuffer);

    auto& highPass = chain_.get<kHighPass>().state;
    auto& lowPass = chain_.get<kLowPass>().state;
    auto& gain = chain_.get<kOutputGain>();

    const bool smoothing = gainSmoother_.isSmoothing() || lowCutSmoother_.isSmoothing() ||
                           highCutSmoother_.isSmoothing() || filterQSmoother_.isSmoothing();

    const auto updateCoefficients = [&](int numSamples) {
        const auto q = filterQSmoother_.skip(numSamples);
        *highPass = juce::dsp::IIR::ArrayCoefficients<float>::makeHighPass(
            sampleRate, lowCutSmoother_.skip(numSamples), q);
        *lowPass = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass(
            sampleRate, highCutSmoother_.skip(numSamples), q);
        gain.setGainLinear(juce::Decibels::decibelsToGain(gainSmoother_.skip(numSamples)));
    };

    if (smoothing) {
        // Re-derive the coefficients every 32 samples so a moving control does
        // not step; the convolution itself is unaffected by the sub-blocking.
        constexpr int kControlBlock = 32;
        for (int done = 0; done < fc.bufferNumSamples;) {
            const int numThisTime = std::min(kControlBlock, fc.bufferNumSamples - done);
            updateCoefficients(numThisTime);

            auto block = juce::dsp::AudioBlock<float>(*fc.destBuffer)
                             .getSubBlock(static_cast<size_t>(fc.bufferStartSample + done),
                                          static_cast<size_t>(numThisTime));
            juce::dsp::ProcessContextReplacing<float> context(block);
            chain_.process(context);

            done += numThisTime;
        }
    } else {
        updateCoefficients(fc.bufferNumSamples);

        auto block = juce::dsp::AudioBlock<float>(*fc.destBuffer)
                         .getSubBlock(static_cast<size_t>(fc.bufferStartSample),
                                      static_cast<size_t>(fc.bufferNumSamples));
        juce::dsp::ProcessContextReplacing<float> context(block);
        chain_.process(context);
    }

    // Gain has to be applied over the render region, not from sample zero: a
    // SmoothedValue advances once across the whole buffer it is handed, so the
    // views below keep it in step with the samples actually being rendered.
    juce::AudioBuffer<float> wetRegion(fc.destBuffer->getArrayOfWritePointers(),
                                       fc.destBuffer->getNumChannels(), fc.bufferStartSample,
                                       fc.bufferNumSamples);
    wetSmoother_.applyGain(wetRegion, fc.bufferNumSamples);

    // Fully wet needs no dry pass, and the smoother is parked at zero there.
    if (drySmoother_.getCurrentValue() > 0.0f || drySmoother_.getTargetValue() > 0.0f) {
        const int numChannels =
            std::min(fc.destBuffer->getNumChannels(), dry.buffer.getNumChannels());

        juce::AudioBuffer<float> dryRegion(dry.buffer.getArrayOfWritePointers(), numChannels,
                                           fc.bufferStartSample, fc.bufferNumSamples);
        drySmoother_.applyGain(dryRegion, fc.bufferNumSamples);

        for (int channel = 0; channel < numChannels; ++channel)
            fc.destBuffer->addFrom(channel, fc.bufferStartSample, dry.buffer, channel,
                                   fc.bufferStartSample, fc.bufferNumSamples);
    }
}

//==============================================================================
void MagdaConvolutionPlugin::restorePluginStateFromValueTree(const juce::ValueTree& v) {
    te::copyPropertiesToCachedValues(v, gainValue, lowCutValue, highCutValue, mixValue,
                                     filterQValue, normalise, trimSilence);

    auto* um = getUndoManager();
    if (const auto* name = v.getPropertyPointer(te::IDs::name))
        state.setProperty(te::IDs::name, *name, um);

    if (const auto* irFileData = v.getProperty(te::IDs::irFileData).getBinaryData())
        state.setProperty(te::IDs::irFileData, juce::var(juce::MemoryBlock(*irFileData)), um);

    for (auto* param : getAutomatableParameters())
        param->updateFromAttachedValue();
}

void MagdaConvolutionPlugin::loadImpulseResponseFromState() {
    const auto* irFileData = state.getProperty(te::IDs::irFileData).getBinaryData();
    if (irFileData == nullptr || irFileData->getSize() == 0) {
        irLengthSeconds_.store(0.0, std::memory_order_relaxed);
        return;
    }

    // Read through the engine's format manager rather than assuming FLAC: the
    // blob is whatever encoded the IR when it was stored.
    auto stream = std::make_unique<juce::MemoryInputStream>(*irFileData, false);
    auto& formats = engine.getAudioFileFormatManager().readFormatManager;

    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::move(stream)));
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
        trimSilence.get() ? juce::dsp::Convolution::Trim::yes : juce::dsp::Convolution::Trim::no,
        normalise.get() ? juce::dsp::Convolution::Normalise::yes
                        : juce::dsp::Convolution::Normalise::no);
}

void MagdaConvolutionPlugin::valueTreePropertyChanged(juce::ValueTree& v,
                                                      const juce::Identifier& id) {
    if (v == state &&
        (id == te::IDs::irFileData || id == te::IDs::normalise || id == te::IDs::trimSilence))
        loadImpulseResponseFromState();

    Plugin::valueTreePropertyChanged(v, id);
}

}  // namespace magda::daw::audio
