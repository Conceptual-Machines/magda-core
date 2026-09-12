#include "plugins/engine/EngineExternalDevice.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

#include "core/ParameterUtils.hpp"
#include "plugin_manager/ExternalPluginState.hpp"

namespace magda::daw::audio::engine_adapter {

namespace {

/// What an encoded event costs before its own bytes, as EngineMagdaDevice
/// derives it.
constexpr int kMidiEventOverheadBytes =
    magda::engine::kMidiShortMessageBytes - 3;  // a short message is three bytes of data

/// Below this the dry level is off and the mix is skipped.
constexpr float kDryLevelFloor = 0.00004f;

/// Above this the wet level is unity and the gain pass is skipped.
constexpr float kWetLevelCeiling = 0.999f;

/// The slots in front of a plugin's own: dry, then wet.
constexpr int kWrapperParameterCount = 2;

/// The model's description of the parameter at plan slot @p index, or none.
/// Both buckets, since the wrapper pair shares the plugin's index space.
std::optional<magda::ParameterInfo> modelParameterAt(const magda::DeviceInfo& device, int index) {
    for (const auto* bucket : {&device.parameters, &device.wrapperParameters})
        for (const auto& info : *bucket)
            if (info.paramIndex == index)
                return info;

    return std::nullopt;
}

/**
 * @brief Replace anything that is not a number with silence (#2240).
 *
 * Cleared rather than clamped: a NaN survives every gain and poisons every sum.
 */
void clearNonFinite(juce::AudioBuffer<float>& audio, int numSamples) {
    for (int channel = 0; channel < audio.getNumChannels(); ++channel) {
        auto* samples = audio.getWritePointer(channel);

        for (int at = 0; at < numSamples; ++at)
            if (!std::isfinite(samples[at]))
                samples[at] = 0.0f;
    }
}

}  // namespace

/**
 * @brief The plugin's editor in a window of its own (#2580).
 *
 * Its close button tells the owner rather than deleting itself.
 */
class EngineExternalDevice::EditorWindow final : public juce::DocumentWindow {
  public:
    EditorWindow(juce::AudioPluginInstance& plugin, std::function<void()> closed)
        : juce::DocumentWindow(plugin.getName(), juce::Colours::black,
                               juce::DocumentWindow::closeButton),
          closed_(std::move(closed)) {
        setUsingNativeTitleBar(true);
        setContentOwned(plugin.createEditorIfNeeded(), true);
        setResizable(plugin.getActiveEditor() != nullptr && plugin.getActiveEditor()->isResizable(),
                     false);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    /// The editor goes while the plugin is still there to be told.
    ~EditorWindow() override {
        clearContentComponent();
    }

    void closeButtonPressed() override {
        // Not delete-this: the owner holds this by unique_ptr.
        if (closed_)
            closed_();
    }

  private:
    std::function<void()> closed_;
};

/**
 * @brief Where the transport is, as a plugin asks for it.
 *
 * Atomics rather than a pointer to the block: a plugin may ask on the message
 * thread long after the block, and a stale position is the accepted answer.
 */
/// Records a parameter the plugin moved itself and queues one flush. JUCE
/// calls this on whatever thread made the change, the audio thread included.
class EngineExternalDevice::PluginListener final : public juce::AudioProcessorListener {
  public:
    PluginListener(std::shared_ptr<PluginEdits> edits, const std::vector<int>& slotOfParameter,
                   const std::atomic<bool>& writing)
        : edits_(std::move(edits)), slotOfParameter_(slotOfParameter), writing_(writing) {}

    void audioProcessorParameterChanged(juce::AudioProcessor*, int parameterIndex,
                                        float newValue) override {
        if (writing_.load(std::memory_order_relaxed))
            return;

        if (parameterIndex < 0 || parameterIndex >= static_cast<int>(slotOfParameter_.size()))
            return;

        const auto slot = slotOfParameter_[static_cast<std::size_t>(parameterIndex)];
        if (slot < 0)
            return;

        auto& edits = *edits_;
        edits.pending[static_cast<std::size_t>(slot)].store(newValue, std::memory_order_relaxed);
        edits.dirty[static_cast<std::size_t>(slot)].store(true, std::memory_order_release);

        if (edits.flushQueued.exchange(true, std::memory_order_acq_rel))
            return;

        juce::MessageManager::callAsync([weak = std::weak_ptr<PluginEdits>(edits_)] {
            const auto edits = weak.lock();
            if (edits == nullptr)
                return;

            edits->flushQueued.store(false, std::memory_order_release);

            for (std::size_t slot = 0; slot < edits->dirty.size(); ++slot) {
                if (!edits->dirty[slot].exchange(false, std::memory_order_acq_rel))
                    continue;

                if (edits->sink)
                    edits->sink(static_cast<int>(slot),
                                edits->pending[slot].load(std::memory_order_relaxed));
            }
        });
    }

    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}

  private:
    std::shared_ptr<PluginEdits> edits_;
    const std::vector<int>& slotOfParameter_;
    const std::atomic<bool>& writing_;
};

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
            // A block assembled by hand: the engine's default map.
            bpm_.store(kDefaultBpm, std::memory_order_relaxed);
            numerator_.store(4, std::memory_order_relaxed);
            denominator_.store(4, std::memory_order_relaxed);
            ppqOfBarStart_.store(std::floor(block.beats.start / 4.0) * 4.0,
                                 std::memory_order_relaxed);
            return;
        }

        // Read at the beat the first sample sounds under
        // (BlockInfo::openingBeat). One bpm and one signature per call is all
        // the interface gives, so a block spanning a change reports its first
        // sample's for all of it (#2340).
        const auto opening = block.openingBeat();

        bpm_.store(block.tempo->bpmAt(opening), std::memory_order_relaxed);

        const auto position = block.tempo->barsAndBeatsAt(opening);
        numerator_.store(position.numerator, std::memory_order_relaxed);
        denominator_.store(position.denominator, std::memory_order_relaxed);

        // PPQ counts quarter notes; the position in the bar is in the
        // signature's own beats.
        const auto quartersPerBeat = 4.0 / std::max(1, position.denominator);

        // The bar the first sample is in, back from where the grid was read.
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

    // The wrapper pair first, then the plugin's automatable parameters in
    // plugin order; non-automatable ones are not in the list at all.
    const auto mappingFor = [](juce::AudioProcessorParameter* parameter, magda::WrapperRole role,
                               std::optional<magda::ParameterInfo> info) {
        return ParameterMapping{.parameter = parameter, .role = role, .info = std::move(info)};
    };

    parameters_.push_back(
        mappingFor(nullptr, magda::WrapperRole::DryGain, modelParameterAt(device, 0)));
    parameters_.push_back(
        mappingFor(nullptr, magda::WrapperRole::WetGain, modelParameterAt(device, 1)));

    // The pairs past the main one, from the model the plan compiled its ports
    // from: extraOutputs[k] is pair k + 1.
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

    lastTable_.assign(parameters_.size(), std::numeric_limits<float>::quiet_NaN());

    slotOfParameter_.assign(static_cast<std::size_t>(instance_->getParameters().size()), -1);
    for (int slot = 0; slot < static_cast<int>(parameters_.size()); ++slot)
        if (const auto* parameter = parameters_[static_cast<std::size_t>(slot)].parameter)
            slotOfParameter_[static_cast<std::size_t>(parameter->getParameterIndex())] = slot;

    edits_ = std::make_shared<PluginEdits>(parameters_.size());
    listener_ = std::make_unique<PluginListener>(edits_, slotOfParameter_, writing_);
    instance_->addListener(listener_.get());
}

void EngineExternalDevice::listenForPluginEdits(std::function<void(int, float)> sink) {
    edits_->sink = std::move(sink);
}

EngineExternalDevice::~EngineExternalDevice() {
    // First: the editor is the plugin's own component (#2580).
    jassert(editor_ == nullptr || juce::MessageManager::existsAndIsCurrentThread());
    editor_.reset();

    instance_->removeListener(listener_.get());

    // JUCE leaves the playhead pointer in place, so the instance is told to
    // forget it before either is destroyed.
    instance_->setPlayHead(nullptr);

    if (prepared_)
        instance_->releaseResources();
}

void EngineExternalDevice::prepare(const magda::engine::RenderContext& context) {
    // Before prepareToPlay, which is where a plugin reads it.
    instance_->setNonRealtime(offlineRender_);
    instance_->setPlayHead(playHead_.get());

    instance_->setRateAndBufferSizeDetails(context.sampleRate, context.maxBlockSize);

    // Re-prepared only when the rate or block size moved, and never through
    // releaseResources(): on VST3 that shuts down the MIDI input buses for good.
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

    // The chain feeds the main bus; a sidechain key goes to whatever follows.
    // A plugin with no bus layout reports only a total, all of it main.
    const auto* mainBus = instance_->getBus(true, 0);
    mainInputChannels_ = mainBus != nullptr ? mainBus->getNumberOfChannels() : totalInputs;
    sidechainInputChannels_ = std::max(0, totalInputs - mainInputChannels_);

    // Processed at its own width, never at none.
    processChannels_ = std::max({1, totalInputs, totalOutputs});

    // What the plugin writes. Channels past this still hold what was copied
    // in, so the chain is filled from this count and not the processed width.
    outputChannels_ = std::max(1, totalOutputs);

    // Wide enough for either path.
    channels_.assign(static_cast<std::size_t>(std::max(processChannels_, context.numChannels)),
                     nullptr);

    scratch_.setSize(processChannels_, context.maxBlockSize, false, true, false);
    dryScratch_.setSize(std::max(processChannels_, context.numChannels), context.maxBlockSize,
                        false, true, false);

    // What comes in, plus room for a plugin that answers with more.
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
    // The entries the table carries, not every slot the plugin has (#2629).
    for (int entry = 0; entry < params.size(); ++entry) {
        const auto slot = params.slotAt(entry);
        if (!mapsSlot(slot))
            continue;

        const auto& mapping = parameters_[static_cast<std::size_t>(slot)];
        const auto values = params.valuesAt(entry);

        // Nothing resolved it, so the plugin keeps what its own state put there.
        if (values.empty())
            continue;

        // The position, not the value: a hosted parameter is normalised by
        // definition, and a configured range can be degenerate (-inf dB).
        const auto normalised = values.position();

        switch (mapping.role) {
            case magda::WrapperRole::DryGain:
                dryGain_ = normalised;
                break;

            case magda::WrapperRole::WetGain:
                wetGain_ = normalised;
                break;

            default: {
                if (mapping.parameter == nullptr)
                    break;

                // Only when the host's value moved. Comparing against the
                // plugin's own value would revert every edit made in its editor.
                auto& last = lastTable_[static_cast<std::size_t>(slot)];
                if (last == normalised)
                    break;
                last = normalised;

                writing_.store(true, std::memory_order_relaxed);
                mapping.parameter->setValue(normalised);
                writing_.store(false, std::memory_order_relaxed);
                break;
            }
        }
    }
}

void EngineExternalDevice::readMidiIn(const juce::MidiBuffer& in, bool allNotesOff) {
    midi_.clear();

    // The panic travels beside the buffer (#2418); every channel, ahead of
    // the block's own events.
    if (allNotesOff)
        for (int channel = 1; channel <= 16; ++channel)
            midi_.addEvent(juce::MidiMessage::allNotesOff(channel), 0);

    for (const auto metadata : in)
        midi_.addEvent(metadata.data, metadata.numBytes, metadata.samplePosition);
}

void EngineExternalDevice::writeMidiOut(juce::MidiBuffer& out, int numSamples) const {
    // JUCE's AU path refills the buffer only under wantsMidiMessages, so for a
    // plugin with no MIDI of its own it is still the input (#2348).
    const bool pluginHasMidiOfItsOwn = instance_->acceptsMidi() || instance_->producesMidi();
    if (!pluginHasMidiOfItsOwn)
        return;

    // In bytes, against what the executor reserved for this port (#2341).
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

            // A pair this build of the plugin does not have stays cleared.
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
        clearNonFinite(audio, numSamples);

        if (wetGain_ < kWetLevelCeiling)
            audio.applyGain(0, numSamples, wetGain_);

        return;
    }

    const auto numChannels = audio.getNumChannels();

    for (int channel = 0; channel < numChannels; ++channel)
        dryScratch_.copyFrom(channel, 0, audio, channel, 0, numSamples);

    instance_->processBlock(audio, midi_);

    // Before the dry is added back, so a NaN cannot poison it.
    clearNonFinite(audio, numSamples);

    if (wetGain_ < kWetLevelCeiling)
        audio.applyGain(0, numSamples, wetGain_);

    for (int channel = 0; channel < numChannels; ++channel)
        audio.addFrom(channel, 0, dryScratch_.getReadPointer(channel), numSamples, dryGain_);
}

/// The plugin's own buffer, filled from the block, processed, and copied back.
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

    // Bridged against the main bus, so a mono plugin with a mono key stays out
    // of the stereo branch.
    if (destChannels == 1 && mainInputChannels_ == 2) {
        // Mono in, stereo wanted: duplicate.
        audio.copyFrom(1, 0, audio, 0, 0, numSamples);
    } else if (destChannels == 2 && mainInputChannels_ == 1) {
        // Stereo in, mono wanted: the average.
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
            // A mono plugin in a stereo chain: its one output goes to both sides.
            juce::FloatVectorOperations::copy(destination, audio.getReadPointer(0), numSamples);
        else
            juce::FloatVectorOperations::clear(destination, numSamples);
    }
}

void EngineExternalDevice::process(magda::engine::DeviceBlock& block) {
    // A state read or write holds the callback lock and sets suspended
    // (ExternalPluginState.hpp). Tried, not waited on: a block that arrives
    // mid-transaction passes through. Parameter writes are inside the gate so
    // a capture's chunk and parameter values agree.
    const juce::ScopedTryLock guard(instance_->getCallbackLock());
    if (!guard.isLocked() || instance_->isSuspended())
        return;

    writeParameters(block.params);

    const auto numSamples = static_cast<int>(block.audio.getNumSamples());
    const auto destChannels = static_cast<int>(block.audio.getNumChannels());

    playHead_->setBlock(block.block, sampleRate_);

    if (block.midiIn != nullptr)
        readMidiIn(*block.midiIn, block.midiInAllNotesOff);
    else
        midi_.clear();

    if (destChannels == processChannels_ && sidechainInputChannels_ == 0 &&
        outputChannels_ >= destChannels && block.extraOutputs.empty()) {
        // In place: the widths agree and the plugin writes every channel the
        // slot is read at. A multi-out instrument needs the scratch path for
        // its further pairs.
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

bool EngineExternalDevice::showEditor() {
    if (editor_ != nullptr) {
        editor_->toFront(true);
        return true;
    }

    if (!instance_->hasEditor())
        return false;

    editor_ = std::make_unique<EditorWindow>(*instance_, [this] { hideEditor(); });
    return true;
}

void EngineExternalDevice::hideEditor() {
    editor_.reset();
}

bool EngineExternalDevice::isEditorOpen() const {
    return editor_ != nullptr;
}

std::optional<magda::ExternalPluginSnapshot> EngineExternalDevice::captureState() {
    // Shared with the fork in ExternalPluginState.hpp; process() honours the
    // suspension it asks for.
    return magda::captureExternalPluginState(*instance_);
}

magda::SavedStateOutcome EngineExternalDevice::applyState(const magda::DeviceInfo& saved) {
    return magda::applySavedPluginState(*instance_, saved);
}

magda::HostParameters EngineExternalDevice::describeParameters() const {
    return magda::describeHostParameters(*instance_, magda::DeviceInfo{});
}

juce::String EngineExternalDevice::parameterText(int slot, float normalised) const {
    if (slot < 0 || slot >= static_cast<int>(parameters_.size()))
        return {};

    // Null for the wrapper pair and for a parameter this build no longer has.
    const auto* parameter = parameters_[static_cast<std::size_t>(slot)].parameter;
    if (parameter == nullptr)
        return {};

    constexpr int kMaxTextLength = 16;
    return parameter->getText(std::clamp(normalised, 0.0f, 1.0f), kMaxTextLength);
}

}  // namespace magda::daw::audio::engine_adapter
