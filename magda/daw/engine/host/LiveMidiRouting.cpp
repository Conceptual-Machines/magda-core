#include "LiveMidiRouting.hpp"

#include <algorithm>

namespace magda::daw::engine_host {

std::shared_ptr<const engine::LiveRouting> LiveMidiRouting::resolve(
    const std::vector<TrackInfo>& tracks) {
    auto routing = std::make_shared<engine::LiveRouting>();
    routing->tracks.reserve(tracks.size());

    for (const auto& track : tracks) {
        engine::TrackLiveMidi entry{.trackId = track.id,
                                    .audition = sources_.auditionSourceFor(track.id),
                                    .sources = devicesFor(track),
                                    .sourcesLost = 0};

        const auto* before = previous_ != nullptr ? previous_->find(track.id) : nullptr;
        if (before != nullptr) {
            const auto gone = std::ranges::any_of(before->sources, [&](auto source) {
                return std::ranges::find(entry.sources, source) == entry.sources.end();
            });
            entry.sourcesLost = before->sourcesLost + (gone ? 1 : 0);
        }

        routing->tracks.push_back(std::move(entry));
    }

    // The model holds its tracks in arrangement order; find() binary-searches.
    std::ranges::sort(routing->tracks,
                      [](const auto& a, const auto& b) { return a.trackId < b.trackId; });

    if (previous_ != nullptr && previous_->tracks == routing->tracks)
        return nullptr;

    previous_ = routing;
    return routing;
}

std::vector<engine::LiveMidiSourceId> LiveMidiRouting::devicesFor(const TrackInfo& track) {
    // "track:<id>" names a track, not a MIDI device.
    if (!track.monitorsInput() || track.midiInputDevice.startsWith("track:"))
        return {};

    // An empty field means no device.
    if (track.midiInputDevice.isEmpty())
        return {};

    if (track.midiInputDevice == "all")
        return sources_.deviceSources();

    const auto source = sources_.resolveRoute(track.midiInputDevice);
    if (source == LiveMidiSources::kNoSource)
        return {};

    return {source};
}

}  // namespace magda::daw::engine_host
