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

/// The SDK's mutable MIDI view over storage that outlives the block.
///
/// The fork's adapter wraps te::MidiMessageArray, which is already this shape.
/// The engine's port is a juce::MidiBuffer, which is a byte stream and cannot
/// be addressed by index or edited in place, so the block's events are decoded
/// into the scratch on the way in and encoded back out of it afterwards.
///
/// The storage is entries rather than a vector that grows and empties, and that
/// is what keeps a long message from allocating every block. juce::MidiMessage
/// keeps anything past its inline storage on the heap, so clearing the vector
/// would free every SysEx dump each block and malloc it again the next;
/// assigning into an entry that is still alive reuses what it already holds.
/// Short messages -- every note, every controller change -- are inline and cost
/// nothing either way.
///
/// What remains is one realloc where an entry is handed a longer message than
/// it last held, which is JUCE's copy assignment and not something an adapter
/// can go around while the SDK hands devices a juce::MidiMessage. It happens
/// as a stream's longest message arrives, not per block.
///
/// Nothing here grows the storage. Its size is the most events the input port's
/// own bound admits, so what is refused is a producer past that bound rather
/// than an ordinary block: growing it would allocate on the audio thread, and
/// one dropped event is cheaper than a callback that missed its deadline. The
/// engine treats the same violation the same way -- assert where it is written,
/// count it where it costs something.
///
/// That bound is a count of events and it is only half the rule. The port's
/// budget is bytes, and the two part company on SysEx: a handful of dumps sit
/// well inside any event count while being multiples of the byte budget. The
/// count is what keeps this storage from reallocating; the byte budget is
/// enforced where it is actually spent, writing back onto the port.
class EngineMidiBufferView final : public DeviceMidiBuffer {
  public:
    EngineMidiBufferView(std::vector<DeviceMidiEvent>& storage, int& live)
        : storage_(storage), live_(live) {}

    int size() const override {
        return live_;
    }

    const juce::MidiMessage& message(int index) const override {
        return storage_[static_cast<std::size_t>(index)].message;
    }

    std::uint32_t sourceId(int index) const override {
        return storage_[static_cast<std::size_t>(index)].sourceId;
    }

    void setEvent(int index, DeviceMidiEvent event) override {
        storage_[static_cast<std::size_t>(index)] = std::move(event);
    }

    void removeEvent(int index) override {
        for (int at = index; at + 1 < live_; ++at)
            storage_[static_cast<std::size_t>(at)] =
                std::move(storage_[static_cast<std::size_t>(at + 1)]);
        if (live_ > 0)
            --live_;
    }

    void addEvent(DeviceMidiEvent event) override {
        if (static_cast<std::size_t>(live_) >= storage_.size()) {
            jassertfalse;  // a device past the port's bound; see the class comment
            return;
        }

        storage_[static_cast<std::size_t>(live_++)] = std::move(event);
    }

    void clear() override {
        live_ = 0;
    }

    void sortByTimestamp() override {
        // Stable, because two events at one instant have an order already and
        // it is the order they were delivered in: a note-off ahead of the
        // note-on that follows it stays ahead of it.
        std::stable_sort(storage_.begin(), storage_.begin() + live_,
                         [](const DeviceMidiEvent& first, const DeviceMidiEvent& second) {
                             return first.message.getTimeStamp() < second.message.getTimeStamp();
                         });
    }

    bool isAllNotesOff() const override {
        return allNotesOff_;
    }

    void setAllNotesOff(bool allNotesOff) override {
        allNotesOff_ = allNotesOff;
    }

  private:
    std::vector<DeviceMidiEvent>& storage_;
    int& live_;
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

    channels_.assign(static_cast<std::size_t>(std::max(0, context.numChannels)), nullptr);

    sizeMidiScratch();
}

void EngineMagdaDevice::setMidiInputBoundBytes(int bytes) {
    midiInputBoundBytes_ = bytes;
    sizeMidiScratch();
}

void EngineMagdaDevice::sizeMidiScratch() {
    // Resized rather than reserved: the entries stay alive between blocks so a
    // long message reuses the storage it already holds. See the view.
    //
    // Both callers run off the audio thread, and both run again if the other
    // does: prepare() sizes from whatever bound is known, and the executor tells
    // us the real one straight after. A device the plan gave no MIDI input is
    // told zero and keeps nothing.
    midiLive_ = 0;
    midiScratch_.assign(static_cast<std::size_t>(midiEventsWithin(midiInputBoundBytes_)), {});
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

    // Non-owning: the executor's buffer, seen through the container the SDK
    // takes. Nothing is copied and nothing is allocated.
    juce::AudioBuffer<float> audio(channels_.data(), static_cast<int>(numChannels), numSamples);

    std::optional<EngineMidiBufferView> midi;
    if (block.midiIn != nullptr || block.midiOut != nullptr) {
        midiLive_ = 0;

        if (block.midiIn != nullptr)
            for (const auto metadata : *block.midiIn) {
                if (static_cast<std::size_t>(midiLive_) >= midiScratch_.size()) {
                    jassertfalse;  // a producer past the port's bound
                    break;
                }

                auto& entry = midiScratch_[static_cast<std::size_t>(midiLive_++)];

                // Assigned into the entry rather than constructed over it, so
                // an entry that already holds heap storage reuses it.
                entry.message = metadata.getMessage();
                entry.sourceId = 0;

                // Seconds from the start of the block, which is what a device
                // reads on both sides: the fork stamps its events that way and
                // the engine's ports count samples.
                entry.message.setTimeStamp(static_cast<double>(metadata.samplePosition) /
                                           sampleRate_);
            }

        midi.emplace(midiScratch_, midiLive_);
    }

    std::optional<EngineTempoMapView> tempo;
    if (block.block.tempo != nullptr)
        tempo.emplace(*block.block.tempo);

    DeviceProcessContext context{
        .audio = &audio,
        .midi = midi ? &*midi : nullptr,
        .tempoMap = tempo ? &*tempo : nullptr,
        .startSample = 0,
        .numSamples = numSamples,
        .midiTimeOffsetSeconds = 0.0,
        .timelineStartSeconds = block.block.startSeconds,
        .timelineEndSeconds = block.block.endSeconds,
        .isPlaying = block.block.playing,
        .isScrubbing = false,
        .isRendering = offlineRender_,
    };

    device_->process(context);

    if (block.midiOut == nullptr)
        return;

    // What the device left in the scratch, back onto the port. An event outside
    // the block lands on the nearest sample it has rather than being dropped: a
    // device that placed a note one sample past the end meant the note.
    //
    // Counted in bytes, because that is what the port's budget is and what the
    // executor sized its storage from. The scratch's own guard is a count of
    // events, which is the right bound for a vector and the wrong one for this:
    // a device emitting SysEx stays far inside that count while going far past
    // kMaxMidiBytesPerPort, and every byte over it grows a juce::MidiBuffer the
    // executor reserved once and a delay line downstream sized to match.
    int bytesWritten = 0;

    for (int index = 0; index < midiLive_; ++index) {
        const auto& event = midiScratch_[static_cast<std::size_t>(index)];
        const auto cost = kMidiEventOverheadBytes + event.message.getRawDataSize();
        if (bytesWritten + cost > magda::engine::kMaxMidiBytesPerPort) {
            jassertfalse;  // a device past the port's budget; see the view's comment
            break;
        }

        bytesWritten += cost;

        const auto sample =
            static_cast<int>(std::llround(event.message.getTimeStamp() * sampleRate_));
        block.midiOut->addEvent(event.message, std::clamp(sample, 0, std::max(0, numSamples - 1)));
    }
}

}  // namespace magda::daw::audio::engine_adapter
