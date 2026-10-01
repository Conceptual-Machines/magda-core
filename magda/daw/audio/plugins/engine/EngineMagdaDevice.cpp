#include "plugins/engine/EngineMagdaDevice.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <ranges>
#include <utility>

#include "core/ParameterUtils.hpp"

namespace magda::daw::audio::engine_adapter {

namespace {

/// What an encoded event costs before its own bytes.
///
/// The engine states the cost model rather than exporting this: an event costs
/// six bytes plus its length, which is what makes a note or a controller change
/// kMidiShortMessageBytes. Derived from that pair so the two cannot drift.
constexpr int kMidiEventOverheadBytes =
    magda::engine::kMidiShortMessageBytes - 3;  // a short message is three bytes of data

/// How many events a port carrying @p boundBytes can hand over.
///
/// Converted at the *cheapest* event, not the typical one. The budget is bytes,
/// and the smallest message there is has one byte of data -- a clock, a start,
/// a stop -- so a port that respects its bound to the byte can still carry far
/// more events than the same bound divided by the cost of a note. Sizing from
/// the note is what leaves a stream that is entirely legal growing the storage
/// on the audio thread, which is the allocation the reservation exists to
/// avoid.
int midiEventsWithin(int boundBytes) {
    return std::max(0, boundBytes) / (kMidiEventOverheadBytes + 1);
}

sdk::DeviceProperties propertiesForRequiredDevice(const std::unique_ptr<MagdaDevice>& device) {
    jassert(device != nullptr);
    return device->properties();
}

/// The SDK's read-only view of what reached the device, over the input scratch.
///
/// The engine's port is a juce::MidiBuffer, a byte stream that cannot be
/// addressed by index, so the block's events are decoded into the scratch first.
/// A sysex points at the port's own bytes, which the block does not modify, so
/// nothing here allocates.
class EngineMidiInputView final : public sdk::MidiInput {
  public:
    EngineMidiInputView(const std::vector<sdk::MidiEvent>& events, bool allNotesOff)
        : events_(events), allNotesOff_(allNotesOff) {}

    int size() const override {
        return static_cast<int>(events_.size());
    }

    const sdk::MidiEvent& event(int index) const override {
        return events_[static_cast<std::size_t>(index)];
    }

    bool isAllNotesOff() const override {
        return allNotesOff_;
    }

  private:
    const std::vector<sdk::MidiEvent>& events_;
    /// What the port carried, beside its events rather than in them: the
    /// engine's juce::MidiBuffer has nowhere to put it (#2418).
    bool allNotesOff_ = false;
};

/// The SDK's write-only sink for what the device emits, over the output scratch.
///
/// Nothing here grows a vector: the event count and the byte budget are the
/// output port's bounds, and an event past either is refused rather than
/// allocated for, since one dropped event is cheaper than a callback that
/// missed its deadline. The budget is the one the executor reserved for the
/// port (#2341), so what is accepted here fits the port when it is written back.
class EngineMidiOutputView final : public sdk::MidiOutput {
  public:
    EngineMidiOutputView(std::vector<sdk::MidiEvent>& events, std::vector<std::uint8_t>& bytes,
                         int capacity, int budgetBytes)
        : events_(events), bytes_(bytes), capacity_(capacity), budgetBytes_(budgetBytes) {}

    /// What the device left here, for the executor to put back on the port.
    bool isAllNotesOff() const {
        return allNotesOff_;
    }

    bool addEvent(const sdk::MidiEvent& event) override {
        const auto cost = kMidiEventOverheadBytes + static_cast<int>(event.size());
        if (static_cast<int>(events_.size()) >= capacity_ || spentBytes_ + cost > budgetBytes_) {
            jassertfalse;  // a device past the port's bound; see the class comment
            return false;
        }

        spentBytes_ += cost;
        auto stored = event;
        if (event.isSysex()) {
            // Within the reserve: every sysex byte is part of the cost above.
            const auto offset = bytes_.size();
            bytes_.insert(bytes_.end(), event.longData, event.longData + event.longSize);
            stored.longData = bytes_.data() + offset;
        }
        events_.push_back(stored);
        return true;
    }

    void setAllNotesOff(bool allNotesOff) override {
        allNotesOff_ = allNotesOff;
    }

  private:
    std::vector<sdk::MidiEvent>& events_;
    std::vector<std::uint8_t>& bytes_;
    int capacity_;
    int budgetBytes_;
    int spentBytes_ = 0;
    bool allNotesOff_ = false;
};

/// The block's tempo map, as the SDK asks for it.
///
/// A view over the snapshot the transport published for this callback, which is
/// immutable and outlives the block. Seconds in, because that is the face of a
/// block a device is given alongside its samples.
class EngineTempoMapView final : public sdk::TempoMap {
  public:
    explicit EngineTempoMapView(const magda::engine::TempoMap& map) : map_(map) {}

    double beatsAtSeconds(double seconds) const override {
        return map_.timeToBeat(seconds);
    }

    double bpmAtSeconds(double seconds) const override {
        return map_.bpmAt(map_.timeToBeat(seconds));
    }

  private:
    const magda::engine::TempoMap& map_;
};

}  // namespace

EngineMagdaDevice::EngineMagdaDevice(std::unique_ptr<MagdaDevice> device, bool offlineRender)
    : device_(std::move(device)),
      properties_(propertiesForRequiredDevice(device_)),
      dspTimingName_(properties_.pluginId + " dsp"),
      offlineRender_(offlineRender) {
    device_->setHost(this);
    parameters_.reserve(static_cast<std::size_t>(std::max(0, device_->parameterCount())));

    for (auto [index, info] : std::views::zip(std::views::iota(0), device_->parameters())) {
        // The plan addresses a device's parameters by ParameterInfo::paramIndex
        // and allocates a slot for every index from zero, so a device that left
        // it unset is addressed by its declaration order; two answers to one
        // question would put a parameter's value on the wrong parameter.
        const int plan = info.paramIndex >= 0 ? info.paramIndex : index;
        parameters_.push_back({plan, std::move(info)});
    }
}

EngineMagdaDevice::~EngineMagdaDevice() {
    // The other half of prepare(), which the executor has no call for: a plan
    // swap retires an op rather than unpreparing it, so a device's last moment
    // is its destruction. Skipped for one that never ran, because release() is
    // paired with prepare() and a device that was built and dropped was never
    // handed a sample rate.
    if (prepared_)
        device_->release();

    device_->setHost(nullptr);
}

void EngineMagdaDevice::stateChanged(sdk::StateNode state) {
    if (stateReporter_)
        stateReporter_(std::move(state));
}

void EngineMagdaDevice::rebuildRequired() {
    if (rebuildHandler_)
        rebuildHandler_();
}

void EngineMagdaDevice::prepare(const magda::engine::RenderContext& context) {
    // A second prepare is a device being re-prepared at a new rate or block
    // size, and the SDK pairs each one with a release.
    if (prepared_)
        device_->release();

    prepared_ = true;
    sampleRate_ = context.sampleRate;

    // The first block after a prepare writes every slot: what the device holds
    // is whatever it was constructed or restored with.
    for (auto& mapping : parameters_)
        mapping.written = std::numeric_limits<float>::quiet_NaN();

    // Properties are constant between prepares, so this is the one place they are
    // read again; latency is only valid once the device has been prepared.
    properties_ = device_->properties();

    device_->prepare({
        .sampleRate = context.sampleRate,
        .maximumBlockSize = context.maxBlockSize,
    });

    latencySamples_ = device_->latencySamples();

    channels_.assign(static_cast<std::size_t>(std::max(0, context.numChannels)), nullptr);
    sidechainChannels_.assign(static_cast<std::size_t>(std::max(0, properties_.sidechain.channels)),
                              nullptr);

    sizeMidiScratch();
}

void EngineMagdaDevice::setMidiInputBoundBytes(int bytes) {
    midiInputBoundBytes_ = bytes;
    sizeMidiScratch();
}

void EngineMagdaDevice::setMidiOutputBoundBytes(int bytes) {
    midiOutputBoundBytes_ = bytes;
    sizeMidiScratch();
}

bool EngineMagdaDevice::forwardsMidiInput() const {
    // Only meaningful for a device the plan reads MIDI from: what a device
    // that declares no output writes is dropped either way.
    return properties_.forwardsMidiInput && properties_.producesMidi;
}

void EngineMagdaDevice::sizeMidiScratch() {
    // Every caller runs off the audio thread, and each runs again when another
    // does: prepare() sizes from whatever bounds are known, and the executor
    // tells us the real ones straight after. Each scratch is sized from its own
    // port's bound; a device the plan gave neither port keeps nothing.
    midiInCapacity_ = midiEventsWithin(midiInputBoundBytes_);
    midiInScratch_.clear();
    midiInScratch_.reserve(static_cast<std::size_t>(midiInCapacity_));

    midiOutCapacity_ = midiEventsWithin(midiOutputBoundBytes_);
    midiOutScratch_.clear();
    midiOutScratch_.reserve(static_cast<std::size_t>(midiOutCapacity_));
    midiOutBytes_.clear();
    midiOutBytes_.reserve(static_cast<std::size_t>(std::max(0, midiOutputBoundBytes_)));
}

void EngineMagdaDevice::reset() {
    device_->reset();
}

void EngineMagdaDevice::setOfflineRender(bool offline) {
    offlineRender_ = offline;
}

int EngineMagdaDevice::latencySamples() const {
    return latencySamples_;
}

double EngineMagdaDevice::tailSeconds() const {
    // Live: a convolution's tail is whatever impulse is loaded.
    const auto tail = device_->tailSamples();
    if (tail == sdk::kInfiniteTail)
        return std::numeric_limits<double>::infinity();
    return static_cast<double>(tail) / sampleRate_;
}

void EngineMagdaDevice::writeParameters(const magda::engine::DeviceParams& params) {
    if (parametersStale_.exchange(false, std::memory_order_acq_rel))
        for (auto& mapping : parameters_)
            mapping.written = std::numeric_limits<float>::quiet_NaN();

    for (int slot = 0; slot < static_cast<int>(parameters_.size()); ++slot) {
        auto& mapping = parameters_[static_cast<std::size_t>(slot)];
        const auto values = params[mapping.plan];

        // A parameter the table does not have is a parameter nothing resolved,
        // and the device keeps whatever it was set to rather than being handed
        // a zero that would read as the bottom of its range.
        if (values.empty())
            continue;

        const auto position = values.segments().front().startValue;
        if (position != mapping.position || values.domain() != mapping.domain) {
            mapping.position = position;
            mapping.domain = values.domain();
            mapping.normalized =
                magda::ParameterUtils::realToNormalized(values.value(), mapping.info);
        }

        // Only when the model moved it. A device sets its own parameters at
        // most while it restores state, which prepare() or a live restore's
        // invalidateParameterWrites() follows.
        if (mapping.normalized != mapping.written) {
            mapping.written = mapping.normalized;
            device_->setParameterValue(slot, mapping.normalized);
        }
    }
}

void EngineMagdaDevice::process(magda::engine::DeviceBlock& block) {
    writeParameters(block.params);

    const auto numSamples = static_cast<int>(block.audio.getNumSamples());
    const auto numChannels =
        std::min(channels_.size(), static_cast<std::size_t>(block.audio.getNumChannels()));

    for (std::size_t channel = 0; channel < numChannels; ++channel)
        channels_[channel] = block.audio.getChannelPointer(channel);

    // The key on its own port, sized to what the device declared: the plan
    // gives it to us as its own block and the SDK takes it as one, so nothing
    // in between decides where in a buffer a key belongs (#2329).
    const auto sidechainChannels = std::min(
        sidechainChannels_.size(), static_cast<std::size_t>(block.sidechain.getNumChannels()));
    for (std::size_t channel = 0; channel < sidechainChannels; ++channel)
        sidechainChannels_[channel] = block.sidechain.getChannelPointer(channel);

    // Non-owning: the executor's buffer, seen through the container the SDK
    // takes. Nothing is copied and nothing is allocated.
    sdk::ProcessContext context;
    context.audio = BufferView(channels_.data(), static_cast<int>(numChannels), numSamples);
    if (sidechainChannels > 0)
        context.sidechain = ConstBufferView(sidechainChannels_.data(),
                                            static_cast<int>(sidechainChannels), numSamples);

    // Both views or neither, which is the SDK's contract: a device with an input
    // and nothing to write to still gets a sink, and its output is discarded.
    std::optional<EngineMidiInputView> midiIn;
    std::optional<EngineMidiOutputView> midiOut;
    if (block.midiIn != nullptr || block.midiOut != nullptr) {
        midiInScratch_.clear();
        midiOutScratch_.clear();
        midiOutBytes_.clear();

        occurrences_.restart();
        if (block.midiIn != nullptr)
            for (const auto metadata : *block.midiIn) {
                if (static_cast<int>(midiInScratch_.size()) >= midiInCapacity_) {
                    jassertfalse;  // a producer past the port's bound
                    break;
                }

                auto event = sdk::MidiEvent::fromBytes(
                    metadata.data, static_cast<std::uint32_t>(metadata.numBytes),
                    metadata.samplePosition);

                // How far into its sample a note-on falls (#2741).
                if (block.midiInFractions != nullptr && event.isNoteOn())
                    event.fraction = block.midiInFractions->at(
                        event.sample, event.channel(), event.noteNumber(),
                        occurrences_.next(event.sample, event.channel(), event.noteNumber()));

                midiInScratch_.push_back(event);
            }

        midiIn.emplace(midiInScratch_, block.midiInAllNotesOff);
        midiOut.emplace(midiOutScratch_, midiOutBytes_, midiOutCapacity_, midiOutputBoundBytes_);
    }

    std::optional<EngineTempoMapView> tempo;
    if (block.block.tempo != nullptr)
        tempo.emplace(*block.block.tempo);

    context.midiIn = midiIn ? &*midiIn : nullptr;
    context.midiOut = midiOut ? &*midiOut : nullptr;
    context.tempoMap = tempo ? &*tempo : nullptr;
    context.timelineStartSeconds = block.block.seconds.start;
    context.timelineEndSeconds = block.block.seconds.end;
    context.isPlaying = block.block.playing;
    context.isRendering = offlineRender_;

    {
        const DeviceTimingScope dsp(dspTimingName_.c_str());
        device_->process(context);
    }

    // Back onto the port, ahead of the two returns below: the flag is the
    // device's answer whether or not it wrote an event, and dropping it on an
    // empty block is how a panic gets lost (#2418).
    if (midiOut && properties_.producesMidi)
        block.midiOutAllNotesOff = midiOut->isAllNotesOff();

    if (block.midiOut == nullptr)
        return;

    // A port the device never declared: what it wrote is dropped.
    if (!properties_.producesMidi)
        return;

    // What the device wrote, back onto the port. An event outside the block
    // lands on the nearest sample it has rather than being dropped: a device
    // that placed a note one sample past the end meant the note. The output
    // view has already counted the bytes against what the executor reserved for
    // this port (#2341); thru is the plan's merge behind the device (#2347).
    for (const auto& event : midiOutScratch_) {
        const auto sample = std::clamp(event.sample, 0, std::max(0, numSamples - 1));
        block.midiOut->addEvent(event.data(), static_cast<int>(event.size()), sample);

        // One entry per note-on; a stamp the clamp moved sounds on its sample.
        if (block.midiOutFractions != nullptr && event.isNoteOn())
            block.midiOutFractions->add(sample, event.channel(), event.noteNumber(),
                                        sample == event.sample ? event.fraction : 0.0f);
    }
}

}  // namespace magda::daw::audio::engine_adapter
