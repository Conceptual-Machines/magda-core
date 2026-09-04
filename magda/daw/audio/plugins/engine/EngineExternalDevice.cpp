#include "plugins/engine/EngineExternalDevice.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

#include "core/ParameterUtils.hpp"
#include "plugin_manager/ExternalPluginState.hpp"

namespace magda::daw::audio::engine_adapter {

namespace {

/// What an encoded event costs before its own bytes. Derived from the engine's
/// own cost model, the same way EngineMagdaDevice derives it, so the two cannot
/// drift apart.
constexpr int kMidiEventOverheadBytes =
    magda::engine::kMidiShortMessageBytes - 3;  // a short message is three bytes of data

/// Below this the fork treats the dry level as off and skips the mix entirely,
/// which is what makes the common case one processBlock and no copy.
constexpr float kDryLevelFloor = 0.00004f;

/// Above this the fork treats the wet level as unity and skips the gain pass.
constexpr float kWetLevelCeiling = 0.999f;

/// The parameter slots the fork puts in front of a plugin's own: dry, then wet.
constexpr int kWrapperParameterCount = 2;

/// The model's description of the parameter at plan slot @p index, or none.
///
/// Both buckets, because MAGDA splits the fork's one list in two: the plugin's
/// parameters and the wrapper pair it never declared. Both carry the fork's
/// index in paramIndex, which is what the plan addresses them by, so the split
/// is a UI concern and this is the one place that has to put them back together.
std::optional<magda::ParameterInfo> modelParameterAt(const magda::DeviceInfo& device, int index) {
    for (const auto* bucket : {&device.parameters, &device.wrapperParameters})
        for (const auto& info : *bucket)
            if (info.paramIndex == index)
                return info;

    return std::nullopt;
}

}  // namespace

/**
 * @brief Where the transport is, as a plugin asks for it.
 *
 * A block's own description, published for the plugin to read during the call.
 * Held in atomics rather than as a pointer to the block, for the fork's own
 * reason: a plugin is entitled to ask on the message thread, hours after the
 * block that produced the answer, and some do. The worst that can do here is
 * report a stale position, which is what the fork does too.
 */
class EngineExternalDevice::PlayHead final : public juce::AudioPlayHead {
  public:
    void setBlock(const magda::engine::BlockInfo& block, double sampleRate) {
        playing_.store(block.playing, std::memory_order_relaxed);
        timeSeconds_.store(block.seconds.start, std::memory_order_relaxed);
        timeSamples_.store(
            static_cast<std::int64_t>(std::llround(block.seconds.start * sampleRate)),
            std::memory_order_relaxed);
        ppqPosition_.store(block.beats.start, std::memory_order_relaxed);

        if (block.tempo == nullptr) {
            // A caller assembling a block by hand: the defaults are what the
            // transport would have published, and the bar grid is the one the
            // engine's own default map has.
            bpm_.store(kDefaultBpm, std::memory_order_relaxed);
            numerator_.store(4, std::memory_order_relaxed);
            denominator_.store(4, std::memory_order_relaxed);
            ppqOfBarStart_.store(std::floor(block.beats.start / 4.0) * 4.0,
                                 std::memory_order_relaxed);
            return;
        }

        // The tempo and the bar grid the block's first sample sounds under, not
        // the ones its first beat sits in: a block can open a hundredth of a
        // sample before a boundary it is otherwise entirely past
        // (BlockInfo::openingBeat), and taking them from there would run the
        // call on the section it had already left.
        //
        // One bpm and one signature for the whole call is all this interface
        // has to give, so a block spanning a tempo change reports what its
        // first sample is in for all of it. That is what every host does and
        // what plugins are built to tolerate (#2340).
        //
        // Where the block is stays its own: the time and the PPQ position above
        // are its first sample's, which is what the plugin is being handed.
        const auto opening = block.openingBeat();

        bpm_.store(block.tempo->bpmAt(opening), std::memory_order_relaxed);

        const auto position = block.tempo->barsAndBeatsAt(opening);
        numerator_.store(position.numerator, std::memory_order_relaxed);
        denominator_.store(position.denominator, std::memory_order_relaxed);

        // PPQ counts quarter notes and the position inside a bar is counted in
        // the signature's own beats, so the two part company anywhere the
        // denominator is not four: three eighths into a 6/8 bar is one and a
        // half quarter notes, not three.
        const auto quartersPerBeat = 4.0 / std::max(1, position.denominator);

        // The bar the block's first sample is in, under the signature it
        // renders in. Not the bar its middle is in: a block is not cut at bar
        // lines, so a long one straddles one, and taking the middle's bar would
        // make what the plugin is told depend on how the host happened to size
        // the callback.
        //
        // Straight back from where the grid was read, because the opening beat
        // already carries the tolerance a sample's question is answered to.
        ppqOfBarStart_.store(opening - (position.beat * quartersPerBeat),
                             std::memory_order_relaxed);
    }

    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo result;

        result.setIsPlaying(playing_.load(std::memory_order_relaxed));
        result.setTimeInSeconds(timeSeconds_.load(std::memory_order_relaxed));
        result.setTimeInSamples(timeSamples_.load(std::memory_order_relaxed));
        result.setBpm(bpm_.load(std::memory_order_relaxed));
        result.setTimeSignature(
            TimeSignature{.numerator = numerator_.load(std::memory_order_relaxed),
                          .denominator = denominator_.load(std::memory_order_relaxed)});

        const auto barStart = ppqOfBarStart_.load(std::memory_order_relaxed);
        result.setPpqPositionOfLastBarStart(barStart);
        result.setPpqPosition(std::max(barStart, ppqPosition_.load(std::memory_order_relaxed)));

        return result;
    }

  private:
    static constexpr double kDefaultBpm = 120.0;

    std::atomic<bool> playing_{false};
    std::atomic<double> timeSeconds_{0.0};
    std::atomic<std::int64_t> timeSamples_{0};
    std::atomic<double> ppqPosition_{0.0};
    std::atomic<double> ppqOfBarStart_{0.0};
    std::atomic<double> bpm_{kDefaultBpm};
    std::atomic<int> numerator_{4};
    std::atomic<int> denominator_{4};
};

EngineExternalDevice::EngineExternalDevice(std::unique_ptr<juce::AudioPluginInstance> instance,
                                           const magda::DeviceInfo& device, bool offlineRender)
    : instance_(std::move(instance)),
      playHead_(std::make_unique<PlayHead>()),
      offlineRender_(offlineRender) {
    jassert(instance_ != nullptr);

    // The fork's list, rebuilt: the wrapper pair first, then the plugin's own
    // automatable parameters in plugin order. A parameter the plugin says is
    // not automatable is not in the list at all, which is why this cannot be
    // the plugin's parameter array with two added -- it is a filtered view of
    // it, and the filter is what decides which slot a project's saved value
    // lands on.
    const auto mappingFor = [](juce::AudioProcessorParameter* parameter, magda::WrapperRole role,
                               std::optional<magda::ParameterInfo> info) {
        return ParameterMapping{.parameter = parameter, .role = role, .info = std::move(info)};
    };

    parameters_.push_back(
        mappingFor(nullptr, magda::WrapperRole::DryGain, modelParameterAt(device, 0)));
    parameters_.push_back(
        mappingFor(nullptr, magda::WrapperRole::WetGain, modelParameterAt(device, 1)));

    // The pairs past the main one, in the plan's own order: extraOutputs[k] is
    // pair k + 1. Read from the model rather than from the instance's bus
    // layout, because it is the model the plan compiled its ports from -- the
    // two agree, and where they do not it is the plan that decided how many
    // ports there are.
    const auto& pairs = device.multiOut.outputPairs;
    for (std::size_t pair = 1; pair < pairs.size(); ++pair) {
        const auto& declared = pairs[pair];
        extraOutputPairs_.push_back({.firstChannel = std::max(0, declared.firstPin - 1),
                                     .numChannels = std::max(0, declared.numChannels)});
    }

    const auto order = magda::hostParameterOrder(*instance_);
    for (int slot = kWrapperParameterCount; slot < static_cast<int>(order.size()); ++slot)
        parameters_.push_back(mappingFor(order[static_cast<std::size_t>(slot)],
                                         magda::WrapperRole::None, modelParameterAt(device, slot)));
}

EngineExternalDevice::~EngineExternalDevice() {
    // The playhead outlives nothing: the instance is told to forget it before
    // either is destroyed, because JUCE leaves the pointer where it was and a
    // plugin asking after the fact would read freed memory. The fork does the
    // same thing in the same order (deinitialise: "must be done first").
    instance_->setPlayHead(nullptr);

    if (prepared_)
        instance_->releaseResources();
}

void EngineExternalDevice::prepare(const magda::engine::RenderContext& context) {
    // Before prepareToPlay, because it is what a plugin reads when it decides
    // how much work a block is worth: an oversampling saturator sizes its
    // filters here.
    instance_->setNonRealtime(offlineRender_);
    instance_->setPlayHead(playHead_.get());

    instance_->setRateAndBufferSizeDetails(context.sampleRate, context.maxBlockSize);

    // Prepared once, and again only when the rate or the block size actually
    // moved. This is the fork's rule and its comment is worth carrying with it:
    // it used to call releaseResources() before re-preparing, and with VST3
    // that shuts down the MIDI input buses with no way to get them back, which
    // breaks every synth. So a device the store retains across a re-prepare at
    // the same settings is left alone rather than torn down and rebuilt.
    if (!prepared_ || context.sampleRate != sampleRate_ ||
        context.maxBlockSize != preparedBlockSize_) {
        instance_->prepareToPlay(context.sampleRate, context.maxBlockSize);
    }

    prepared_ = true;
    preparedBlockSize_ = context.maxBlockSize;
    sampleRate_ = context.sampleRate;

    latencySamples_ = instance_->getLatencySamples();

    const auto totalInputs = instance_->getTotalNumInputChannels();
    const auto totalOutputs = instance_->getTotalNumOutputChannels();

    // The main bus is what the chain feeds; anything after it is where a
    // sidechain key goes, which is how every dynamics plugin takes one. A
    // plugin with no bus layout at all reports its channels only in total, and
    // then all of them are the main bus.
    const auto* mainBus = instance_->getBus(true, 0);
    mainInputChannels_ = mainBus != nullptr ? mainBus->getNumberOfChannels() : totalInputs;
    sidechainInputChannels_ = std::max(0, totalInputs - mainInputChannels_);

    // The fork's width, verbatim: a plugin is processed at its own channel
    // count whatever the chain around it is, and never at none.
    processChannels_ = std::max({1, totalInputs, totalOutputs});

    // What the plugin actually writes, which is not the same number and is the
    // one the chain has to be filled from. A plugin with more inputs than
    // outputs -- a stereo-in mono-out utility, anything with a sidechain -- is
    // processed at the wider count, and the channels past its outputs still
    // hold what was copied in. Reading those back as output hands the chain its
    // own input, or a sidechain key, in place of the second half of a signal.
    outputChannels_ = std::max(1, totalOutputs);

    // Wide enough for either path: the block's channels when the plugin's width
    // already matches, the plugin's when it does not.
    channels_.assign(static_cast<std::size_t>(std::max(processChannels_, context.numChannels)),
                     nullptr);

    scratch_.setSize(processChannels_, context.maxBlockSize, false, true, false);
    dryScratch_.setSize(std::max(processChannels_, context.numChannels), context.maxBlockSize,
                        false, true, false);

    // What comes in, plus room for a plugin that answers with more than it was
    // given. Both are the port's own budget, which is the only figure either
    // side of this is sized from.
    midi_.ensureSize(static_cast<std::size_t>(midiInputBoundBytes_) +
                     magda::engine::kMaxMidiBytesPerPort);
}

void EngineExternalDevice::reset() {
    instance_->reset();
}

void EngineExternalDevice::setMidiInputBoundBytes(int bytes) {
    midiInputBoundBytes_ = bytes;

    if (prepared_)
        midi_.ensureSize(static_cast<std::size_t>(midiInputBoundBytes_) +
                         magda::engine::kMaxMidiBytesPerPort);
}

void EngineExternalDevice::setMidiOutputBoundBytes(int bytes) {
    midiOutputBoundBytes_ = bytes;
}

int EngineExternalDevice::latencySamples() const {
    return latencySamples_;
}

void EngineExternalDevice::writeParameters(const magda::engine::DeviceParams& params) {
    for (int slot = 0; slot < static_cast<int>(parameters_.size()); ++slot) {
        const auto& mapping = parameters_[static_cast<std::size_t>(slot)];
        const auto values = params[slot];

        // A slot the table does not carry is one nothing resolved: a project
        // that saved fewer parameters than this build of the plugin has, or a
        // wrapper pair a project never wrote. The plugin keeps whatever its own
        // state put there, which for the pair is fully wet.
        if (values.empty())
            continue;

        // Through the model's own inverse rather than as a raw number. An
        // external parameter's units are already normalised, so this is the
        // identity for almost all of them, and the almost is the point: it is
        // the same conversion the value went into the table through, and
        // ParameterUtils is the only thing that knows where it is not.
        const auto normalised =
            mapping.info ? magda::ParameterUtils::realToNormalized(values.value(), *mapping.info)
                         : std::clamp(values.value(), 0.0f, 1.0f);

        switch (mapping.role) {
            case magda::WrapperRole::DryGain:
                // The wrapper pair is the host's own number and no chunk can
                // carry it, so it is always taken from the model.
                dryGain_ = normalised;
                break;

            case magda::WrapperRole::WetGain:
                wetGain_ = normalised;
                break;

            default:
                if (mapping.parameter == nullptr)
                    break;

                // What the plan resolved, with nothing read into it. Whether
                // this value came from the project's array, a lane, a macro or
                // a knob somebody just turned is the model's business, and the
                // one case where the model could hold something the plugin has
                // already corrected -- a stale array beside a good chunk -- is
                // settled before this device exists, by the restoration handing
                // the corrected values back to whoever owns the model
                // (ExternalPluginState.hpp). A device that tried to work that
                // out from the numbers could not: no comparison tells a stale
                // value apart from a lane passing through the same one.

                // Only on a change, which is the fork's rule rather than a
                // saving: a plugin is entitled to treat every write as a
                // gesture, and one that rebuilds a filter or repaints an editor
                // on each would do it every block on a parameter nobody moved.
                if (mapping.parameter->getValue() != normalised)
                    mapping.parameter->setValue(normalised);
                break;
        }
    }
}

void EngineExternalDevice::readMidiIn(const juce::MidiBuffer& in) {
    midi_.clear();

    for (const auto metadata : in)
        midi_.addEvent(metadata.data, metadata.numBytes, metadata.samplePosition);
}

void EngineExternalDevice::writeMidiOut(juce::MidiBuffer& out, int numSamples) const {
    // The plugin's own output: JUCE hands it one buffer for both directions and
    // refills it before returning. A chain that wants the raw input asks the
    // plan for it (DeviceInfo::midiInThru) rather than asking the plugin.
    //
    // JUCE's AU path refills only under wantsMidiMessages, so for a plugin with
    // no MIDI of its own the buffer is still the input, and writing that back
    // doubles it behind a ChainMidiMerge (#2348).
    const bool pluginHasMidiOfItsOwn = instance_->acceptsMidi() || instance_->producesMidi();
    if (!pluginHasMidiOfItsOwn)
        return;

    // Counted in bytes against what the executor reserved for this port, rather
    // than the flat constant (#2341).
    int bytesWritten = 0;

    for (const auto metadata : midi_) {
        const auto cost = kMidiEventOverheadBytes + metadata.numBytes;
        if (bytesWritten + cost > midiOutputBoundBytes_) {
            jassertfalse;  // a plugin past the port's budget
            break;
        }

        bytesWritten += cost;
        out.addEvent(metadata.data, metadata.numBytes,
                     std::clamp(metadata.samplePosition, 0, std::max(0, numSamples - 1)));
    }
}

void EngineExternalDevice::writeExtraOutputs(magda::engine::DeviceBlock& block,
                                             const juce::AudioBuffer<float>& processed,
                                             int numSamples) const {
    const auto pairs = std::min(block.extraOutputs.size(), extraOutputPairs_.size());

    for (std::size_t pair = 0; pair < pairs; ++pair) {
        auto& destination = block.extraOutputs[pair];
        const auto& source = extraOutputPairs_[pair];
        const auto channels =
            std::min(static_cast<int>(destination.getNumChannels()), source.numChannels);

        for (int channel = 0; channel < channels; ++channel) {
            const auto from = source.firstChannel + channel;

            // A pair whose channels the plugin does not have is left as it
            // arrived, which is cleared. That is a model that recorded more
            // pairs than this build of the plugin reports, and silence is the
            // honest answer: the alternative is handing a track whatever
            // happened to be in the buffer at that index.
            if (from >= outputChannels_ || from >= processed.getNumChannels())
                break;

            juce::FloatVectorOperations::copy(
                destination.getChannelPointer(static_cast<std::size_t>(channel)),
                processed.getReadPointer(from), numSamples);
        }
    }
}

void EngineExternalDevice::processPluginBlock(juce::AudioBuffer<float>& audio) {
    const auto numSamples = audio.getNumSamples();

    if (dryGain_ <= kDryLevelFloor) {
        instance_->processBlock(audio, midi_);

        if (wetGain_ < kWetLevelCeiling)
            audio.applyGain(0, numSamples, wetGain_);

        return;
    }

    const auto numChannels = audio.getNumChannels();

    for (int channel = 0; channel < numChannels; ++channel)
        dryScratch_.copyFrom(channel, 0, audio, channel, 0, numSamples);

    instance_->processBlock(audio, midi_);

    if (wetGain_ < kWetLevelCeiling)
        audio.applyGain(0, numSamples, wetGain_);

    for (int channel = 0; channel < numChannels; ++channel)
        audio.addFrom(channel, 0, dryScratch_.getReadPointer(channel), numSamples, dryGain_);
}

/// The plugin's own buffer, filled from the block, processed, and copied back.
///
/// The path taken whenever the plugin's width and the chain's differ, or the
/// plugin has a sidechain bus to fill: both need a buffer that is not the
/// executor's, since the executor's is exactly the chain's width and carries no
/// key.
void EngineExternalDevice::processThroughScratch(magda::engine::DeviceBlock& block, int numSamples,
                                                 int destChannels) {
    juce::AudioBuffer<float> audio(scratch_.getArrayOfWritePointers(), processChannels_, 0,
                                   numSamples);

    for (int channel = 0; channel < processChannels_; ++channel) {
        if (channel < destChannels)
            juce::FloatVectorOperations::copy(
                audio.getWritePointer(channel),
                block.audio.getChannelPointer(static_cast<std::size_t>(channel)), numSamples);
        else
            audio.clear(channel, 0, numSamples);
    }

    // The fork's two bridging rules, against the plugin's main bus rather than
    // against its total input count. They mean the same thing: the fork's
    // buffer already has the sidechain channels appended by the rack around it,
    // so its total is this main count wherever these two cases can fire, and
    // naming the main bus is what keeps a mono plugin with a mono key out of
    // the stereo branch.
    if (destChannels == 1 && mainInputChannels_ == 2) {
        // Mono in, stereo wanted: duplicate rather than leave a silent side.
        audio.copyFrom(1, 0, audio, 0, 0, numSamples);
    } else if (destChannels == 2 && mainInputChannels_ == 1) {
        // Stereo in, mono wanted: the average, which is the sum at half gain
        // and not the left channel.
        audio.addFrom(0, 0, block.audio.getChannelPointer(1), numSamples);
        audio.applyGain(0, 0, numSamples, 0.5f);
    }

    const auto sidechainChannels =
        std::min(sidechainInputChannels_, static_cast<int>(block.sidechain.getNumChannels()));

    for (int channel = 0; channel < sidechainInputChannels_; ++channel) {
        const auto destination = mainInputChannels_ + channel;
        if (destination >= processChannels_)
            break;

        if (channel < sidechainChannels)
            juce::FloatVectorOperations::copy(
                audio.getWritePointer(destination),
                block.sidechain.getChannelPointer(static_cast<std::size_t>(channel)), numSamples);
        else
            audio.clear(destination, 0, numSamples);
    }

    processPluginBlock(audio);
    writeExtraOutputs(block, audio, numSamples);

    for (int channel = 0; channel < destChannels; ++channel) {
        auto* destination = block.audio.getChannelPointer(static_cast<std::size_t>(channel));

        if (channel < outputChannels_)
            juce::FloatVectorOperations::copy(destination, audio.getReadPointer(channel),
                                              numSamples);
        else if (channel < 2)
            // A mono plugin in a stereo chain: its one output goes to both
            // sides, so what follows it has two channels to read rather than a
            // silent right.
            //
            // Bounded by the plugin's output count and not by the width it was
            // processed at. Those differ for anything with more inputs than
            // outputs, and there the channels between the two hold input rather
            // than answer: a sidechain key, or the right half of a signal the
            // plugin summed to mono.
            juce::FloatVectorOperations::copy(destination, audio.getReadPointer(0), numSamples);
        else
            juce::FloatVectorOperations::clear(destination, numSamples);
    }
}

void EngineExternalDevice::process(magda::engine::DeviceBlock& block) {
    // The plugin's own callback lock, and its own suspended flag. A host reading
    // or writing this plugin's state holds one and sets the other
    // (ExternalPluginState.hpp), and what a block may not do is touch the plugin
    // at all while that is happening: it is entitled to be halfway through
    // swapping a sample or a program, and neither its DSP nor its parameters are
    // safe to reach into meanwhile.
    //
    // Everything plugin-facing is inside this, parameter writes included. They
    // are the reason the gate is here rather than around the processBlock call:
    // a capture reads the plugin's chunk and then its parameter values, and a
    // block that moved a parameter between those two reads would produce a
    // saved pair that disagrees with itself.
    //
    // Tried rather than waited on, because this is the audio thread. A block
    // that arrives mid-transaction leaves the buffer as it was handed over,
    // which is the passthrough the plan already gives a Device op with no
    // instance bound.
    //
    // The lock is the half that makes suspension mean anything. Holding it here
    // is what makes suspendProcessing() wait for a block already in progress
    // rather than set a flag and return while the plugin is still running.
    const juce::ScopedTryLock guard(instance_->getCallbackLock());
    if (!guard.isLocked() || instance_->isSuspended())
        return;

    writeParameters(block.params);

    const auto numSamples = static_cast<int>(block.audio.getNumSamples());
    const auto destChannels = static_cast<int>(block.audio.getNumChannels());

    playHead_->setBlock(block.block, sampleRate_);

    if (block.midiIn != nullptr)
        readMidiIn(*block.midiIn);
    else
        midi_.clear();

    if (destChannels == processChannels_ && sidechainInputChannels_ == 0 &&
        outputChannels_ >= destChannels && block.extraOutputs.empty()) {
        // The plugin's width and the block's already agree and it writes every
        // channel the slot will be read at, so it processes the executor's own
        // buffer and nothing is copied. The fork's fast path, and the one
        // almost every plugin in a stereo chain takes.
        //
        // The output count is part of the test rather than a detail: a
        // stereo-in mono-out plugin matches the first half of it and leaves the
        // right channel holding the input it was handed, which is not silence
        // and is not its answer.
        //
        // A multi-out instrument is out of it entirely: its further pairs live
        // in the channels past the ones the chain reads, and processing in
        // place into the chain's own two would leave nowhere for them to be
        // written.
        for (int channel = 0; channel < destChannels; ++channel)
            channels_[static_cast<std::size_t>(channel)] =
                block.audio.getChannelPointer(static_cast<std::size_t>(channel));

        juce::AudioBuffer<float> audio(channels_.data(), destChannels, numSamples);
        processPluginBlock(audio);
    } else {
        processThroughScratch(block, numSamples, destChannels);
    }

    if (block.midiOut != nullptr)
        writeMidiOut(*block.midiOut, numSamples);
}

std::optional<magda::ExternalPluginSnapshot> EngineExternalDevice::captureState() {
    // The read itself is engine-agnostic and stated once: what a plugin holds,
    // in the encoding a project keeps it in, with the plugin suspended across
    // the whole of it (ExternalPluginState.hpp). What this adds is the only
    // thing that could not live there -- that nothing else can reach the
    // instance to do it, so the suspension the read asks for is a suspension
    // this device's own process() is already honouring.
    return magda::captureExternalPluginState(*instance_);
}

}  // namespace magda::daw::audio::engine_adapter
