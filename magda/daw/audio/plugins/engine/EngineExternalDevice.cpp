#include "plugins/engine/EngineExternalDevice.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
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
/// Records a parameter the plugin moved itself and wakes the host once. JUCE
/// calls this on whatever thread made the change, the audio thread included.
class EngineExternalDevice::PluginListener final : public juce::AudioProcessorListener {
  public:
    PluginListener(std::shared_ptr<PluginEdits> edits, const std::vector<int>& slotOfParameter,
                   std::atomic<bool>& catalogStale)
        : edits_(std::move(edits)),
          slotOfParameter_(slotOfParameter),
          catalogStale_(catalogStale) {}

    void audioProcessorParameterChanged(juce::AudioProcessor*, int parameterIndex,
                                        float newValue) override {
        const auto slot = slotFor(parameterIndex);
        if (slot < 0)
            return;

        auto& edits = *edits_;
        const auto at = static_cast<std::size_t>(slot);

        // Classified now rather than at the flush: by then the block that
        // drove the slot, or the gesture around the move, has been and gone.
        const auto source = edits.driven[at].load(std::memory_order_relaxed)
                                ? magda::ObservationSource::Driven
                            : edits.gesturing[at].load(std::memory_order_relaxed)
                                ? magda::ObservationSource::EditorGesture
                                : magda::ObservationSource::Readback;

        // A gesture's value is kept apart too, so readback landing before the
        // flush cannot take the base update away from it.
        if (source == magda::ObservationSource::EditorGesture) {
            edits.gestured[at].store(newValue, std::memory_order_release);
            edits.gestureDirty[at].store(true, std::memory_order_release);
        }

        edits.reported[at].store(PluginEdits::pack(newValue, source), std::memory_order_release);
        edits.dirty[at].store(true, std::memory_order_release);

        // Before listening, what is recorded waits for the wake that listening sends.
        edits.wakeHost();
    }

    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*,
                                                   int parameterIndex) override {
        if (const auto slot = slotFor(parameterIndex); slot >= 0)
            edits_->gesturing[static_cast<std::size_t>(slot)].store(true,
                                                                    std::memory_order_release);
    }

    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*,
                                                 int parameterIndex) override {
        if (const auto slot = slotFor(parameterIndex); slot >= 0)
            edits_->gesturing[static_cast<std::size_t>(slot)].store(false,
                                                                    std::memory_order_release);
    }

    /// A VST3's kParamTitlesChanged arrives as parameterInfoChanged.
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override {
        if (details.parameterInfoChanged)
            catalogStale_.store(true, std::memory_order_release);
    }

  private:
    /// The plan slot for the plugin's parameter at @p parameterIndex, or -1.
    int slotFor(int parameterIndex) const {
        if (parameterIndex < 0 || parameterIndex >= static_cast<int>(slotOfParameter_.size()))
            return -1;

        return slotOfParameter_[static_cast<std::size_t>(parameterIndex)];
    }

    std::shared_ptr<PluginEdits> edits_;
    const std::vector<int>& slotOfParameter_;
    std::atomic<bool>& catalogStale_;
};

std::uint64_t EngineExternalDevice::PluginEdits::pack(float normalised,
                                                      magda::ObservationSource source) {
    return (static_cast<std::uint64_t>(source) << 32) | std::bit_cast<std::uint32_t>(normalised);
}

EngineExternalDevice::Observation EngineExternalDevice::PluginEdits::unpack(int slot,
                                                                            std::uint64_t packed) {
    return {.slot = slot,
            .normalised = std::bit_cast<float>(static_cast<std::uint32_t>(packed & 0xFFFFFFFFu)),
            .source = static_cast<magda::ObservationSource>(packed >> 32)};
}

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
    mailbox_ = std::vector<std::atomic<std::uint64_t>>(parameters_.size());
    outcomes_.assign(kEditOutcomeCapacity, 0);
    listener_ = std::make_unique<PluginListener>(edits_, slotOfParameter_, catalogStale_);
    instance_->addListener(listener_.get());
}

void EngineExternalDevice::listenForPluginEdits(std::function<void()> wake) {
    jassert(!edits_->listening.load());
    if (wake == nullptr)
        return;

    edits_->wake = std::move(wake);
    edits_->listening.store(true, std::memory_order_release);

    edits_->wakeQueued.store(true, std::memory_order_release);
    edits_->wake();
}

EngineExternalDevice::PluginEditSource EngineExternalDevice::pluginEdits() const {
    return PluginEditSource{edits_};
}

bool EngineExternalDevice::PluginEditSource::drain(
    const std::function<void(Observation)>& sink) const {
    const auto edits = edits_.lock();
    if (edits == nullptr)
        return false;

    // Cleared first, so a report landing during the walk wakes another drain.
    edits->wakeQueued.store(false, std::memory_order_release);

    for (std::size_t slot = 0; slot < edits->dirty.size(); ++slot) {
        const auto gestured = edits->gestureDirty[slot].exchange(false, std::memory_order_acq_rel);
        if (!edits->dirty[slot].exchange(false, std::memory_order_acq_rel) && !gestured)
            continue;

        const auto latest = PluginEdits::unpack(
            static_cast<int>(slot), edits->reported[slot].load(std::memory_order_acquire));

        if (gestured && latest.source != magda::ObservationSource::EditorGesture)
            sink({.slot = latest.slot,
                  .normalised = edits->gestured[slot].load(std::memory_order_acquire),
                  .source = magda::ObservationSource::EditorGesture});

        sink(latest);
    }

    return true;
}

void EngineExternalDevice::forgetDriverState() {
    for (auto& driven : edits_->driven)
        driven.store(false, std::memory_order_relaxed);
}

namespace {

/// A mailbox word: the sequence high, the value's bits low.
std::uint64_t packEdit(std::uint32_t sequence, float normalised) {
    return (static_cast<std::uint64_t>(sequence) << 32) | std::bit_cast<std::uint32_t>(normalised);
}

std::uint32_t sequenceOf(std::uint64_t packed) {
    return static_cast<std::uint32_t>(packed >> 32);
}

}  // namespace

std::optional<EngineExternalDevice::QueuedEdit> EngineExternalDevice::queueParameterEdit(
    int slot, float normalised) {
    if (!mapsSlot(slot) || !std::isfinite(normalised) || normalised < 0.0f || normalised > 1.0f)
        return std::nullopt;

    const auto at = static_cast<std::size_t>(slot);

    // The wrapper pair is the model's own, and nothing of it exists on the
    // plugin to write.
    const auto& mapping = parameters_[at];
    if (mapping.parameter == nullptr || mapping.role != magda::WrapperRole::None)
        return std::nullopt;

    if (++nextEditSequence_ == 0)
        ++nextEditSequence_;

    const auto sequence = nextEditSequence_;
    const auto replaced =
        mailbox_[at].exchange(packEdit(sequence, normalised), std::memory_order_acq_rel);
    anyEditQueued_.store(true, std::memory_order_release);

    if (!isRendered())
        pumpParameterEdits();

    return QueuedEdit{.sequence = sequence, .superseded = sequenceOf(replaced)};
}

bool EngineExternalDevice::withdrawParameterEdit(int slot, std::uint32_t sequence) {
    if (!mapsSlot(slot))
        return false;

    auto& entry = mailbox_[static_cast<std::size_t>(slot)];
    auto held = entry.load(std::memory_order_acquire);
    return sequenceOf(held) == sequence &&
           entry.compare_exchange_strong(held, 0, std::memory_order_acq_rel);
}

bool EngineExternalDevice::takeEditOutcomes(const std::function<void(EditOutcome)>& each) {
    const auto written = outcomesWritten_.load(std::memory_order_acquire);
    auto read = outcomesRead_.load(std::memory_order_relaxed);

    for (; read != written; read = (read + 1) % kEditOutcomeCapacity) {
        const auto packed = outcomes_[read];
        each({.slot = static_cast<int>((packed >> 1) & 0x7FFFFFFFu),
              .sequence = sequenceOf(packed),
              .refused = (packed & 1u) != 0});
    }

    outcomesRead_.store(read, std::memory_order_release);
    return !outcomesLost_.exchange(false, std::memory_order_acq_rel);
}

void EngineExternalDevice::recordEditOutcome(std::size_t slot, std::uint32_t sequence,
                                             bool refused) {
    const auto written = outcomesWritten_.load(std::memory_order_relaxed);
    const auto next = (written + 1) % kEditOutcomeCapacity;
    if (next == outcomesRead_.load(std::memory_order_acquire)) {
        outcomesLost_.store(true, std::memory_order_release);
        return;
    }

    outcomes_[written] = (static_cast<std::uint64_t>(sequence) << 32) |
                         (static_cast<std::uint64_t>(slot) << 1) | (refused ? 1u : 0u);
    outcomesWritten_.store(next, std::memory_order_release);
}

void EngineExternalDevice::pumpParameterEdits() {
    const juce::ScopedLock lock(instance_->getCallbackLock());
    if (applyParameterEdits())
        edits_->wakeHost();
}

void EngineExternalDevice::discardParameterEdits() {
    const juce::ScopedLock lock(instance_->getCallbackLock());
    anyEditQueued_.store(false, std::memory_order_release);

    for (std::size_t at = 0; at < mailbox_.size(); ++at)
        if (const auto taken = mailbox_[at].exchange(0, std::memory_order_acq_rel); taken != 0)
            recordEditOutcome(at, sequenceOf(taken), true);
}

void EngineExternalDevice::setRendered(bool rendered) {
    rendered_.store(rendered, std::memory_order_release);
}

bool EngineExternalDevice::isRendered() const {
    return rendered_.load(std::memory_order_acquire);
}

bool EngineExternalDevice::editsFenced() const {
    const auto queued = std::ranges::any_of(
        mailbox_, [](const auto& entry) { return entry.load(std::memory_order_acquire) != 0; });

    return !queued && !awaitingBlock_.load(std::memory_order_acquire);
}

bool EngineExternalDevice::fenceParameterEdits() {
    const juce::ScopedLock lock(instance_->getCallbackLock());
    if (applyParameterEdits())
        edits_->wakeHost();

    if (!awaitingBlock_.load(std::memory_order_acquire))
        return true;

    if (!prepared_ || instance_->isSuspended())
        return false;

    // A block arriving meanwhile passes through, as it does during a capture.
    scratch_.clear();
    midi_.clear();
    instance_->processBlock(scratch_, midi_);
    midi_.clear();

    awaitingBlock_.store(false, std::memory_order_release);
    return true;
}

bool EngineExternalDevice::applyParameterEdits() {
    if (!anyEditQueued_.exchange(false, std::memory_order_acq_rel))
        return false;

    awaitingBlock_.store(true, std::memory_order_release);

    for (std::size_t at = 0; at < mailbox_.size(); ++at) {
        const auto taken = mailbox_[at].exchange(0, std::memory_order_acq_rel);
        if (taken == 0)
            continue;

        // A person holding it in the plugin's editor has it until they let go.
        const auto refused = edits_->gesturing[at].load(std::memory_order_acquire);

        // Forgotten from lastTable_, so a table carrying the slot delivers again
        // even at the value it held. Both run under the callback lock.
        if (!refused) {
            parameters_[at].parameter->setValue(
                std::bit_cast<float>(static_cast<std::uint32_t>(taken & 0xFFFFFFFFu)));
            lastTable_[at] = std::numeric_limits<float>::quiet_NaN();
        }

        recordEditOutcome(at, sequenceOf(taken), refused);
    }

    return true;
}

std::optional<float> EngineExternalDevice::readParameter(int slot) const {
    if (!mapsSlot(slot))
        return std::nullopt;

    const auto& mapping = parameters_[static_cast<std::size_t>(slot)];
    if (mapping.parameter == nullptr || mapping.role != magda::WrapperRole::None)
        return std::nullopt;

    return mapping.parameter->getValue();
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

        const auto at = static_cast<std::size_t>(slot);

        // Recorded whatever the entry resolved to: a lane playing over a slot
        // is what makes the plugin's own report of it unwanted (#2633).
        edits_->driven[at].store(params.drivenAt(entry), std::memory_order_relaxed);

        const auto& mapping = parameters_[at];
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

                // Held in the editor: a driven value stays owed for the release,
                // while a base is recorded so letting go does not restore a stale one.
                if (edits_->gesturing[at].load(std::memory_order_relaxed)) {
                    lastTable_[at] = params.drivenAt(entry)
                                         ? std::numeric_limits<float>::quiet_NaN()
                                         : normalised;
                    break;
                }

                if (hostValueMoved(slot, normalised))
                    mapping.parameter->setValue(normalised);
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

    // One-off edits before the table, so a slot both carry ends on the table's value.
    const auto editsApplied = applyParameterEdits();
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

    // The plugin has now processed what this block applied, which a capture waits for.
    awaitingBlock_.store(false, std::memory_order_release);
    if (editsApplied)
        edits_->wakeHost();
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
    const auto outcome = magda::applySavedPluginState(*instance_, saved);

    // The table's last delivery says nothing about a patch written since, so a
    // restored instance cannot skip a host-controlled write
    // (docs/specs/hosted-plugin-parameter-control.md).
    {
        const juce::ScopedLock lock(instance_->getCallbackLock());
        std::ranges::fill(lastTable_, std::numeric_limits<float>::quiet_NaN());
    }

    // A chunk can rename parameters as well as move them.
    catalogStale_.store(true, std::memory_order_release);

    // A plugin that threw partway holds half a patch, which is not worth showing.
    if (outcome != magda::SavedStateOutcome::Failed)
        reportEveryParameter();

    return outcome;
}

void EngineExternalDevice::reportEveryParameter() {
    for (std::size_t slot = 0; slot < parameters_.size(); ++slot) {
        const auto& mapping = parameters_[slot];
        if (mapping.parameter == nullptr || mapping.role != magda::WrapperRole::None)
            continue;

        edits_->reported[slot].store(
            PluginEdits::pack(mapping.parameter->getValue(), magda::ObservationSource::Readback),
            std::memory_order_release);
        edits_->dirty[slot].store(true, std::memory_order_release);
    }

    edits_->wakeHost();
}

magda::HostParameters EngineExternalDevice::describeParameters() const {
    const auto stale = catalogStale_.exchange(false, std::memory_order_acq_rel);
    if (stale || !catalog_.has_value()) {
        // Over the slots writes go through, not a fresh hostParameterOrder():
        // a plugin re-flagging what is automatable would shift every slot after it.
        std::vector<juce::AudioProcessorParameter*> order;
        order.reserve(parameters_.size());
        for (const auto& mapping : parameters_)
            order.push_back(mapping.parameter);

        catalog_ = magda::describeHostParameters(order, magda::DeviceInfo{});
    }

    auto described = *catalog_;
    for (auto& info : described.parameters) {
        if (!mapsSlot(info.paramIndex))
            continue;

        if (const auto* parameter =
                parameters_[static_cast<std::size_t>(info.paramIndex)].parameter)
            info.currentValue = parameter->getValue();
    }

    return described;
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
