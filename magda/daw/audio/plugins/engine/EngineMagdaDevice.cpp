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

/// How many events the MIDI scratch is sized for.
///
/// The engine's per-port budget converted at the *cheapest* event, not the
/// typical one. The budget is bytes, and the smallest message there is has one
/// byte of data -- a clock, a start, a stop -- so a port that respects
/// kMaxMidiBytesPerPort to the byte can still carry far more events than the
/// same budget divided by the cost of a note. Sizing from the note is what
/// leaves a stream that is entirely legal growing this vector on the audio
/// thread, which is the allocation the reservation exists to avoid.
constexpr int kMidiScratchEvents =
    magda::engine::kMaxMidiBytesPerPort / (kMidiEventOverheadBytes + 1);

DeviceProperties propertiesForRequiredDevice(const std::unique_ptr<MagdaDevice>& device) {
    jassert(device != nullptr);
    return device->properties();
}

/// The SDK's mutable MIDI view over a flat vector.
///
/// The fork's adapter wraps te::MidiMessageArray, which is already this shape.
/// The engine's port is a juce::MidiBuffer, which is a byte stream and cannot
/// be addressed by index or edited in place, so the block's events are decoded
/// into the scratch on the way in and encoded back out of it afterwards.
///
/// The vector never grows. Its capacity covers every stream the port's budget
/// admits, so what is refused here is a producer past that budget rather than
/// an ordinary block, and refusing is what the alternative costs: growing it
/// would allocate on the audio thread, and one dropped event is cheaper than a
/// callback that missed its deadline. The engine treats the same violation the
/// same way -- assert where it is written, count it where it costs something.
///
/// One allocation this cannot rule out is a SysEx message, which juce::
/// MidiMessage keeps on the heap and copies by allocating. That is the SDK's
/// container rather than this adapter, and te::MidiMessageArray holds the same
/// type on the same terms, so it is a cost both hosts pay identically.
class EngineMidiBufferView final : public DeviceMidiBuffer {
  public:
    explicit EngineMidiBufferView(std::vector<DeviceMidiEvent>& events) : events_(events) {}

    int size() const override {
        return static_cast<int>(events_.size());
    }

    const juce::MidiMessage& message(int index) const override {
        return events_[static_cast<std::size_t>(index)].message;
    }

    std::uint32_t sourceId(int index) const override {
        return events_[static_cast<std::size_t>(index)].sourceId;
    }

    void setEvent(int index, DeviceMidiEvent event) override {
        events_[static_cast<std::size_t>(index)] = std::move(event);
    }

    void removeEvent(int index) override {
        events_.erase(events_.begin() + index);
    }

    void addEvent(DeviceMidiEvent event) override {
        if (events_.size() >= events_.capacity()) {
            jassertfalse;  // a device past the port's budget; see the class comment
            return;
        }

        events_.push_back(std::move(event));
    }

    void clear() override {
        events_.clear();
    }

    void sortByTimestamp() override {
        // Stable, because two events at one instant have an order already and
        // it is the order they were delivered in: a note-off ahead of the
        // note-on that follows it stays ahead of it.
        std::stable_sort(events_.begin(), events_.end(),
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
    std::vector<DeviceMidiEvent>& events_;
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

    midiScratch_.clear();
    midiScratch_.reserve(static_cast<std::size_t>(kMidiScratchEvents));
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
        midiScratch_.clear();

        if (block.midiIn != nullptr)
            for (const auto metadata : *block.midiIn) {
                if (midiScratch_.size() >= midiScratch_.capacity()) {
                    jassertfalse;  // a producer past the port's budget
                    break;
                }

                auto message = metadata.getMessage();
                // Seconds from the start of the block, which is what a device
                // reads on both sides: the fork stamps its events that way and
                // the engine's ports count samples.
                message.setTimeStamp(static_cast<double>(metadata.samplePosition) / sampleRate_);
                midiScratch_.push_back({std::move(message), 0});
            }

        midi.emplace(midiScratch_);
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
    for (const auto& event : midiScratch_) {
        const auto sample =
            static_cast<int>(std::llround(event.message.getTimeStamp() * sampleRate_));
        block.midiOut->addEvent(event.message, std::clamp(sample, 0, std::max(0, numSamples - 1)));
    }
}

}  // namespace magda::daw::audio::engine_adapter
