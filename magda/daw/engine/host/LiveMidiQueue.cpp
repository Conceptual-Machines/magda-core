#include "LiveMidiQueue.hpp"

#include <cstring>

namespace magda::daw::engine_host {

void LiveMidiQueue::push(int source, int slot, const juce::MidiMessage& message) {
    const auto size = message.getRawDataSize();

    if (size <= 0 || size > 3 || message.isSysEx()) {
        oversized_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    Event event;
    event.source = source;
    event.slot = slot;
    event.size = size;
    std::memcpy(event.bytes, message.getRawData(), static_cast<std::size_t>(size));

    if (!fifo_.push(std::move(event)))
        overflowed_.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace magda::daw::engine_host
