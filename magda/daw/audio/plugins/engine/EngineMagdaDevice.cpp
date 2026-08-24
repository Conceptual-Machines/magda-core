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

/// The SDK's mutable MIDI view over a flat vector.
///
/// The fork's adapter wraps te::MidiMessageArray, which is already this shape.
/// The engine's port is a juce::MidiBuffer, which is a byte stream and cannot
/// be addressed by index or edited in place, so the block's events are decoded
/// into the scratch on the way in and encoded back out of it afterwards.
///
/// **Never move-assign a juce::MidiMessage that is already alive.** JUCE's move
/// assignment overwrites the destination's pointer without freeing what it
/// held, while the destructor frees on size alone, so moving into a live
/// message that happened to be heap-allocated leaks its buffer. Move
/// *construction* is safe, because there is no destination to leak, which is
/// why decoding is push_back.
///
/// std::swap is safe as well, and it is what everything that rearranges these
/// uses. Its first move empties the source, so neither assignment after it has
/// a live buffer to drop, and what it costs is two pointers rather than a
/// realloc: copy assignment is also safe and reallocs whatever the destination
/// holds, which on the audio thread is a heap operation nobody asked for.
///
/// That rules out two things this would otherwise be written with.
/// std::vector::erase shifts its tail down by move assignment, and every
/// destination in that shift is live; std::stable_sort moves elements through a
/// temporary and back, and whether a destination has been moved-from first is
/// its implementation's business rather than a guarantee. So removal walks the
/// entry to the back by swapping and pops it, and sorting permutes an index
/// list. Both are spelled out below rather than left to an algorithm, because
/// what is wrong with the algorithm is invisible at the call site.
///
/// A long message costs an allocation on the audio thread whichever way this is
/// written, and that is the SDK boundary rather than this adapter: message()
/// hands a device a const juce::MidiMessage&, MidiBufferIterator only makes one
/// by value, and JUCE has no non-owning MidiMessage. Anything past eight bytes
/// is heap, which is SysEx and nothing else -- every note and every controller
/// change is inline and free. The fork's adapter avoids it by wrapping storage
/// that already owns its messages, so this is a real difference between the two
/// hosts and it needs the SDK to close (#1836).
///
/// Nothing here grows the vector. Its capacity is the most events the input
/// port's own bound admits, so what is refused is a producer past that bound
/// rather than an ordinary block: growing it would allocate, and one dropped
/// event is cheaper than a callback that missed its deadline. The engine treats
/// the same violation the same way -- assert where it is written, count it
/// where it costs something.
///
/// That bound is a count of events and it is only half the rule. The port's
/// budget is bytes, and the two part company on SysEx: a handful of dumps sit
/// well inside any event count while being multiples of the byte budget. The
/// count is what keeps this vector from reallocating; the byte budget is
/// enforced where it is actually spent, writing back onto the port.
class EngineMidiBufferView final : public DeviceMidiBuffer {
  public:
    EngineMidiBufferView(std::vector<DeviceMidiEvent>& events, int capacity,
                         std::vector<int>& order)
        : events_(events), capacity_(capacity), order_(order) {}

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
        // Swapped rather than assigned. Copy assignment is safe where move
        // assignment is not, but it reallocs the destination for a message that
        // is on the heap, and the caller's parameter has already paid for a
        // copy: swapping hands that buffer over and leaves the old one in a
        // local that frees it. No heap operation of its own.
        std::swap(events_[static_cast<std::size_t>(index)], event);
    }

    void removeEvent(int index) override {
        if (index < 0 || static_cast<std::size_t>(index) >= events_.size())
            return;

        // Walked to the back by swapping, which exchanges pointers and touches
        // no heap. A copy shift would realloc every entry it passed, and it was
        // only there because vector::erase moves.
        for (std::size_t at = static_cast<std::size_t>(index); at + 1 < events_.size(); ++at)
            std::swap(events_[at], events_[at + 1]);

        // The removed message is at the back now, and pop_back destroys it: the
        // one free a removal owes.
        events_.pop_back();
    }

    void addEvent(DeviceMidiEvent event) override {
        if (static_cast<int>(events_.size()) >= capacity_) {
            jassertfalse;  // a device past the port's bound; see the class comment
            return;
        }

        // Move construction into storage that holds nothing yet, which is the
        // one move this class is allowed.
        events_.push_back(std::move(event));
    }

    void clear() override {
        events_.clear();
    }

    void sortByTimestamp() override {
        // The order first, which is integers and safe to move however the
        // algorithm likes, and then one pass applying it. Stable, because two
        // events at one instant have an order already and it is the order they
        // were delivered in: a note-off ahead of the note-on that follows it
        // stays ahead of it.
        order_.resize(events_.size());
        for (std::size_t at = 0; at < order_.size(); ++at)
            order_[at] = static_cast<int>(at);

        std::stable_sort(order_.begin(), order_.end(), [this](int first, int second) {
            return events_[static_cast<std::size_t>(first)].message.getTimeStamp() <
                   events_[static_cast<std::size_t>(second)].message.getTimeStamp();
        });

        // Applied as cycles, so the permutation costs one swap per element that
        // moves and no second buffer. std::swap is safe here where assignment
        // is not: its first move leaves the source empty, so neither assignment
        // that follows has a live buffer to drop.
        for (std::size_t at = 0; at < order_.size(); ++at) {
            auto target = static_cast<std::size_t>(order_[at]);
            while (target < at)
                target = static_cast<std::size_t>(order_[target]);

            if (target != at)
                std::swap(events_[at], events_[target]);
        }
    }

    bool isAllNotesOff() const override {
        return allNotesOff_;
    }

    void setAllNotesOff(bool allNotesOff) override {
        allNotesOff_ = allNotesOff;
    }

  private:
    std::vector<DeviceMidiEvent>& events_;
    int capacity_;
    std::vector<int>& order_;
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
    // Both callers run off the audio thread, and both run again if the other
    // does: prepare() sizes from whatever bound is known, and the executor tells
    // us the real one straight after. A device the plan gave no MIDI input is
    // told zero and keeps nothing.
    midiCapacity_ = midiEventsWithin(midiInputBoundBytes_);

    midiScratch_.clear();
    midiScratch_.reserve(static_cast<std::size_t>(midiCapacity_));

    midiOrder_.clear();
    midiOrder_.reserve(static_cast<std::size_t>(midiCapacity_));
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
        // Emptied by destroying what it held, which is what frees a long
        // message rather than leaking it. See the view.
        midiScratch_.clear();

        if (block.midiIn != nullptr)
            for (const auto metadata : *block.midiIn) {
                if (static_cast<int>(midiScratch_.size()) >= midiCapacity_) {
                    jassertfalse;  // a producer past the port's bound
                    break;
                }

                auto message = metadata.getMessage();

                // Seconds from the start of the block, which is what a device
                // reads on both sides: the fork stamps its events that way and
                // the engine's ports count samples.
                message.setTimeStamp(static_cast<double>(metadata.samplePosition) / sampleRate_);
                midiScratch_.push_back({std::move(message), 0});
            }

        midi.emplace(midiScratch_, midiCapacity_, midiOrder_);
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

    for (const auto& event : midiScratch_) {
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
