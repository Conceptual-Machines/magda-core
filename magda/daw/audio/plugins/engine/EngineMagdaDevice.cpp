#include "plugins/engine/EngineMagdaDevice.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
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

DeviceProperties propertiesForRequiredDevice(const std::unique_ptr<MagdaDevice>& device) {
    jassert(device != nullptr);
    return device->properties();
}

/// The SDK's read-only view of what reached the device, over the input scratch.
///
/// The engine's port is a juce::MidiBuffer, a byte stream that cannot be
/// addressed by index, so the block's events are decoded into the scratch first.
/// Decoding is push_back and emptying is clear(): JUCE's MidiMessage move
/// assignment overwrites a live destination's heap pointer without freeing it,
/// so nothing here ever move-assigns into a message that is already alive.
///
/// A long message allocates on the audio thread whichever way this is written:
/// message() hands out a const juce::MidiMessage& and JUCE has no non-owning
/// one. Anything past eight bytes is heap, which is SysEx and nothing else. That
/// is the SDK boundary, not this adapter (#1836).
class EngineMidiInputView final : public DeviceMidiInput {
  public:
    EngineMidiInputView(const std::vector<DeviceMidiEvent>& events, bool allNotesOff)
        : events_(events), allNotesOff_(allNotesOff) {}

    int size() const override {
        return static_cast<int>(events_.size());
    }

    const juce::MidiMessage& message(int index) const override {
        return events_[static_cast<std::size_t>(index)].message;
    }

    std::uint32_t sourceId(int index) const override {
        return events_[static_cast<std::size_t>(index)].sourceId;
    }

    bool isAllNotesOff() const override {
        return allNotesOff_;
    }

  private:
    const std::vector<DeviceMidiEvent>& events_;
    /// What the port carried, beside its events rather than in them: the
    /// engine's juce::MidiBuffer has nowhere to put it (#2418).
    bool allNotesOff_ = false;
};

/// The SDK's write-only sink for what the device emits, over the output scratch.
///
/// Nothing here grows the vector. Its capacity is the most events the output
/// port's bound admits, so what is refused is a device past that bound rather
/// than an ordinary block: growing it would allocate, and one dropped event is
/// cheaper than a callback that missed its deadline.
///
/// That capacity is a count of events; the port's budget is bytes, and the two
/// part company on SysEx. The count keeps this vector from reallocating, the
/// byte budget is enforced where it is spent, writing back onto the port (#2341).
class EngineMidiOutputView final : public DeviceMidiOutput {
  public:
    EngineMidiOutputView(std::vector<DeviceMidiEvent>& events, int capacity)
        : events_(events), capacity_(capacity) {}

    /// What the device left here, for the executor to put back on the port.
    bool isAllNotesOff() const {
        return allNotesOff_;
    }

    void addEvent(DeviceMidiEvent event) override {
        if (static_cast<int>(events_.size()) >= capacity_) {
            jassertfalse;  // a device past the port's bound; see the class comment
            return;
        }

        // Move construction into storage that holds nothing yet: the one move a
        // live juce::MidiMessage allows without leaking (see the input view).
        events_.push_back(std::move(event));
    }

    void setAllNotesOff(bool allNotesOff) override {
        allNotesOff_ = allNotesOff;
    }

  private:
    std::vector<DeviceMidiEvent>& events_;
    int capacity_;
    bool allNotesOff_ = false;
};

/// The block's tempo map, as the SDK asks for it.
///
/// A view over the snapshot the transport published for this callback, which is
/// immutable and outlives the block. Seconds in, because that is the face of a
/// block a device is given alongside its samples.
class EngineTempoMapView final : public DeviceTempoMap {
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
      offlineRender_(offlineRender) {
    const int count = std::max(0, device_->parameterCount());
    parameters_.reserve(static_cast<std::size_t>(count));

    for (int index = 0; index < count; ++index) {
        auto info = device_->parameterInfo(index);
        // The plan addresses a device's parameters by ParameterInfo::paramIndex
        // and allocates a slot for every index from zero, so a device that left
        // it unset is addressed by its declaration order. Same fallback the
        // fork's adapter uses to name them, for the same reason: two answers to
        // one question is what puts a parameter's value on the wrong parameter.
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
}

void EngineMagdaDevice::prepare(const magda::engine::RenderContext& context) {
    // A second prepare is a device being re-prepared at a new rate or block
    // size, and the SDK pairs each one with a release.
    if (prepared_)
        device_->release();

    prepared_ = true;
    sampleRate_ = context.sampleRate;

    device_->prepare({
        .sampleRate = context.sampleRate,
        .maximumBlockSize = context.maxBlockSize,
    });

    // From the properties read once at construction, never re-read here: the
    // SDK says a device's properties are constant for its lifetime, and the
    // fork's adapter takes them at the same moment. A host that read them again
    // would be the only one of the two that noticed a device changing its mind,
    // which is a divergence rather than a correction.
    latencySamples_ =
        static_cast<int>(std::llround(properties_.latencySeconds * context.sampleRate));

    // Room for the device's own channels plus a sidechain key of the same
    // width appended after them, which is how the SDK hands one over.
    channels_.assign(static_cast<std::size_t>(std::max(0, context.numChannels) * 2), nullptr);

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
}

void EngineMagdaDevice::reset() {
    device_->reset();
}

int EngineMagdaDevice::latencySamples() const {
    return latencySamples_;
}

void EngineMagdaDevice::writeParameters(const magda::engine::DeviceParams& params) {
    for (int slot = 0; slot < static_cast<int>(parameters_.size()); ++slot) {
        const auto& mapping = parameters_[static_cast<std::size_t>(slot)];
        const auto values = params[mapping.plan];

        // A parameter the table does not have is a parameter nothing resolved,
        // and the device keeps whatever it was set to rather than being handed
        // a zero that would read as the bottom of its range.
        if (values.empty())
            continue;

        device_->setParameterValue(
            slot, magda::ParameterUtils::realToNormalized(values.value(), mapping.info));
    }
}

void EngineMagdaDevice::process(magda::engine::DeviceBlock& block) {
    writeParameters(block.params);

    const auto numSamples = static_cast<int>(block.audio.getNumSamples());
    const auto numChannels =
        std::min(channels_.size(), static_cast<std::size_t>(block.audio.getNumChannels()));

    for (std::size_t channel = 0; channel < numChannels; ++channel)
        channels_[channel] = block.audio.getChannelPointer(channel);

    // The key, appended after the device's own channels: the SDK hands a
    // sidechain over as further channels of the same buffer, and the executor
    // gives it to us as a separate block. The const_cast is safe because the
    // contract is that a device reads its key and never writes it, and the
    // alternative is copying the block every callback to say so in the type.
    auto sidechainChannels = std::min(channels_.size() - numChannels,
                                      static_cast<std::size_t>(block.sidechain.getNumChannels()));
    for (std::size_t channel = 0; channel < sidechainChannels; ++channel)
        channels_[numChannels + channel] =
            const_cast<float*>(block.sidechain.getChannelPointer(channel));

    // Non-owning: the executor's buffer, seen through the container the SDK
    // takes. Nothing is copied and nothing is allocated.
    juce::AudioBuffer<float> audio(channels_.data(),
                                   static_cast<int>(numChannels + sidechainChannels), numSamples);

    // Both views or neither, which is the SDK's contract: a device with an input
    // and nothing to write to still gets a sink, and its output is discarded.
    std::optional<EngineMidiInputView> midiIn;
    std::optional<EngineMidiOutputView> midiOut;
    if (block.midiIn != nullptr || block.midiOut != nullptr) {
        // Emptied by destroying what they held, which is what frees a long
        // message rather than leaking it. See the input view.
        midiInScratch_.clear();
        midiOutScratch_.clear();

        if (block.midiIn != nullptr)
            for (const auto metadata : *block.midiIn) {
                if (static_cast<int>(midiInScratch_.size()) >= midiInCapacity_) {
                    jassertfalse;  // a producer past the port's bound
                    break;
                }

                auto message = metadata.getMessage();

                // Seconds from the start of the block, which is what a device
                // reads on both sides: the fork stamps its events that way and
                // the engine's ports count samples.
                message.setTimeStamp(static_cast<double>(metadata.samplePosition) / sampleRate_);
                midiInScratch_.push_back({std::move(message), 0});
            }

        midiIn.emplace(midiInScratch_, block.midiInAllNotesOff);
        midiOut.emplace(midiOutScratch_, midiOutCapacity_);
    }

    std::optional<EngineTempoMapView> tempo;
    if (block.block.tempo != nullptr)
        tempo.emplace(*block.block.tempo);

    DeviceProcessContext context{
        .audio = &audio,
        .sidechainInputChannel = sidechainChannels > 0 ? static_cast<int>(numChannels) : -1,
        .midiIn = midiIn ? &*midiIn : nullptr,
        .midiOut = midiOut ? &*midiOut : nullptr,
        .tempoMap = tempo ? &*tempo : nullptr,
        .startSample = 0,
        .numSamples = numSamples,
        .midiTimeOffsetSeconds = 0.0,
        .timelineStartSeconds = block.block.seconds.start,
        .timelineEndSeconds = block.block.seconds.end,
        .isPlaying = block.block.playing,
        .isScrubbing = false,
        .isRendering = offlineRender_,
    };

    device_->process(context);

    // Back onto the port, ahead of the two returns below: the flag is the
    // device's answer whether or not it wrote an event, and dropping it on an
    // empty block is how a panic gets lost (#2418).
    if (midiOut && properties_.producesMidi)
        block.midiOutAllNotesOff = midiOut->isAllNotesOff();

    if (block.midiOut == nullptr)
        return;

    // A port the device never declared: what it wrote is dropped, as the fork's
    // adapter drops it, so producesMidi means the same on both legs.
    if (!properties_.producesMidi)
        return;

    // What the device wrote, back onto the port. An event outside the block
    // lands on the nearest sample it has rather than being dropped: a device
    // that placed a note one sample past the end meant the note.
    //
    // Counted in bytes against what the executor reserved for this port, never
    // the flat constant (#2341): the scratch's guard is an event count, and a
    // device emitting SysEx stays far inside that while going far past the
    // budget, growing a juce::MidiBuffer the executor reserved once.
    //
    // The input cannot reach this port any more by construction (#2347); thru
    // is the plan's merge behind the device.
    int bytesWritten = 0;

    for (const auto& event : midiOutScratch_) {
        const auto cost = kMidiEventOverheadBytes + event.message.getRawDataSize();
        if (bytesWritten + cost > midiOutputBoundBytes_) {
            jassertfalse;  // a device past the port's budget; see the output view
            break;
        }

        bytesWritten += cost;

        const auto sample =
            static_cast<int>(std::llround(event.message.getTimeStamp() * sampleRate_));
        block.midiOut->addEvent(event.message, std::clamp(sample, 0, std::max(0, numSamples - 1)));
    }
}

}  // namespace magda::daw::audio::engine_adapter
