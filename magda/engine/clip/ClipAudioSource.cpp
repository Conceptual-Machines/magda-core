#include "clip/ClipAudioSource.hpp"

#include <algorithm>

namespace magda::engine {

namespace {

/// Whether a span reaches into the block at all. Half-open at both ends, so a
/// clip ending exactly on a block boundary contributes nothing to the block
/// that starts there.
bool reachesInto(const SnapshotSpan& span, const BlockInfo& block) {
    return span.startSeconds < block.endSeconds && span.endSeconds > block.startSeconds;
}

}  // namespace

ClipAudioSource::ClipAudioSource(TrackId trackId, ClipSnapshotFeed& clips, ClipStreamFeed& streams)
    : trackId_(trackId), clips_(clips), streams_(streams) {}

void ClipAudioSource::prepare(const RenderContext& context) {
    scratch_.setSize(context.numChannels, context.maxBlockSize, false, true, false);
    scratch_.clear();

    for (auto& voice : voices_)
        voice.prepare(context);
}

ClipVoice* ClipAudioSource::voiceFor(ClipId clipId, EventId eventId) {
    for (auto& voice : voices_)
        if (voice.playing(clipId, eventId))
            return &voice;

    for (auto& voice : voices_)
        if (voice.clipId() == INVALID_CLIP_ID)
            return &voice;

    return nullptr;
}

void ClipAudioSource::render(const BlockInfo& block, juce::dsp::AudioBlock<float> out) {
    out.clear();

    const ClipStreamFeed::Reader streams(streams_);
    const auto [firstStream, lastStream] =
        streams ? streams->rangeFor(trackId_)
                : std::pair<const ClipStreamTable::Entry*, const ClipStreamTable::Entry*>{nullptr,
                                                                                          nullptr};

    // Every stream this track has, sounding or not, once per block. The stream
    // a cue is most useful to is one nobody is reading from: a clip that has
    // not started would otherwise not hear about the position it was pointed at
    // until the material was already due (#2016).
    for (const auto* entry = firstStream; entry != lastStream; ++entry)
        entry->stream->applyPendingCue();

    const auto numSamples = std::min(block.numSamples, static_cast<int>(out.getNumSamples()));

    const auto silence = [this] {
        for (auto& voice : voices_)
            voice.release();
    };

    if (!block.playing || numSamples <= 0)
        return silence();

    const ClipSnapshotFeed::Reader snapshot(clips_);
    if (!snapshot)
        return silence();

    const auto* track = snapshot->find(trackId_);
    if (track == nullptr)
        return silence();

    // What sounds, and through what. Gathered before anything is rendered so
    // the voices that are not in it can be let go first: a voice still holding
    // a clip that stopped last block would otherwise keep a slot a clip
    // starting this one needs.
    std::array<Sounding, kMaxVoicesPerTrack> sounding;
    auto soundingCount = 0;

    for (const auto& clip : track->audio) {
        if (!reachesInto(clip.span, block))
            continue;

        for (const auto& event : clip.events) {
            if (!reachesInto(event.span, block))
                continue;

            const auto* found =
                std::find_if(firstStream, lastStream, [&](const ClipStreamTable::Entry& entry) {
                    return entry.clipId == clip.clipId && entry.eventId == event.eventId;
                });

            if (found == lastStream || soundingCount == kMaxVoicesPerTrack) {
                // No reader standing by, or no room left for one. Either way
                // this clip does not sound and the count is what says so.
                starved_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            sounding[static_cast<std::size_t>(soundingCount++)] =
                Sounding{&clip, &event, found->stream.get()};
        }
    }

    for (auto& voice : voices_) {
        if (voice.clipId() == INVALID_CLIP_ID)
            continue;

        const auto stillSounding =
            std::any_of(sounding.begin(), sounding.begin() + soundingCount, [&](const Sounding& s) {
                return voice.playing(s.clip->clipId, s.event->eventId);
            });

        if (!stillSounding)
            voice.release();
    }

    auto scratch =
        juce::dsp::AudioBlock<float>(scratch_).getSubBlock(0, static_cast<std::size_t>(numSamples));

    for (auto index = 0; index < soundingCount; ++index) {
        const auto& entry = sounding[static_cast<std::size_t>(index)];

        auto* voice = voiceFor(entry.clip->clipId, entry.event->eventId);
        if (voice == nullptr) {
            // Unreachable while the gather above is capped at the voice count,
            // and counted rather than asserted because the two numbers being
            // the same is a property of this file rather than of the type.
            starved_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        voice->render(*entry.clip, *entry.event, block, *entry.stream, scratch, out);
    }
}

}  // namespace magda::engine
