#include "LiveMidiCollector.hpp"

#include <algorithm>

#include "exec/EngineDevice.hpp"

namespace magda::daw::engine_host {
namespace {

/// What one queued event costs a MidiBuffer: a sample position and a length in
/// front of its three bytes.
constexpr int kQueuedEventBytes = 9;

}  // namespace

void LiveMidiCollector::prepare() {
    midiBySlot_.resize(LiveMidiSources::kSlots);
    for (auto& buffer : midiBySlot_)
        buffer.ensureSize(static_cast<std::size_t>(engine::kMaxMidiBytesPerPort));

    ownerOfSlot_.assign(LiveMidiSources::kSlots, LiveMidiSources::kNoSource);
    written_.reserve(LiveMidiSources::kSlots);
    streams_.reserve(LiveMidiSources::kSlots);
}

std::span<const engine::LiveMidiStream> LiveMidiCollector::collect(LiveMidiQueue& queue,
                                                                   const LiveMidiSources& sources) {
    for (const auto slot : written_)
        midiBySlot_[static_cast<std::size_t>(slot)].clear();

    written_.clear();
    streams_.clear();

    queue.drain([this, &sources](const LiveMidiQueue::Event& event) {
        if (event.slot < 0 || event.slot >= static_cast<int>(midiBySlot_.size())) {
            drop();
            return;
        }

        const auto slot = static_cast<std::size_t>(event.slot);
        auto& buffer = midiBySlot_[slot];
        const auto first = buffer.isEmpty();

        // Against the registry while the slot is still free to change hands,
        // and against this block's own answer once it cannot: the first id
        // through owns the buffer until it is handed over.
        const auto owner = first ? sources.ownerOfSlot(event.slot) : ownerOfSlot_[slot];
        if (owner != event.source) {
            drop();
            return;
        }

        if (static_cast<int>(buffer.data.size()) + kQueuedEventBytes >
            engine::kMaxMidiBytesPerPort) {
            drop();
            return;
        }

        if (first) {
            ownerOfSlot_[slot] = event.source;
            written_.push_back(event.slot);
        }

        buffer.addEvent(event.bytes, event.size, 0);
    });

    // In slot order, so one block's streams read like another's for the same
    // notes rather than in whatever order they were played.
    std::ranges::sort(written_);

    for (const auto slot : written_)
        streams_.push_back(
            {static_cast<engine::LiveMidiSourceId>(ownerOfSlot_[static_cast<std::size_t>(slot)]),
             &midiBySlot_[static_cast<std::size_t>(slot)]});

    return streams_;
}

}  // namespace magda::daw::engine_host
