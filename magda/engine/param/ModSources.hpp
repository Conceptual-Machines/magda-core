#pragma once

#include <cstdint>
#include <optional>
#include <set>

#include "core/ModInfo.hpp"
#include "core/RackInfo.hpp"
#include "core/TrackInfo.hpp"
#include "core/TypeIds.hpp"

/**
 * @file ModSources.hpp
 * @brief Which track a modifier listens to.
 *
 * Two compilers need the answer and they must not work it out separately. The
 * plan compiler needs it to emit the edge that carries a source track's signal
 * to the modulation system, and the parameter table compiler needs it to record
 * which modifier is on the far end of that edge. A rule stated twice is a rule
 * that will eventually be two rules, and the failure would be silent: an edge
 * to a track no modifier reads, or a modifier reading an edge that was never
 * emitted.
 *
 * The rule itself is short. A modifier listens to the sidechain source of the
 * scope it lives on, and to the track it lives on when that scope has none.
 * That is what the fork does, arrived at from the other end: it collects a
 * track's own modifiers into that track's detector cache and, separately,
 * collects the modifiers of any scope sidechained from a track into that
 * track's cache instead (PluginManager::rebuildSidechainLFOCache).
 *
 * What it listens for depends on what it is. A follower and an audio-triggered
 * modifier read the source's audio; a MIDI-triggered one reads its MIDI.
 * Everything else listens to nothing at all, and says so by having no source:
 * a free-running LFO on a sidechained device is not a reason to carry that
 * track's audio anywhere.
 */

namespace magda::engine {

/** @brief What a modifier listens to its source for. */
enum class ModListen : std::uint8_t {
    Nothing,  ///< free-running or timeline-locked, and not a follower
    Audio,    ///< the source's level: a follower, or an audio trigger
    Midi,     ///< the source's notes: a MIDI trigger
};

/// What @p mod listens for, before the question of which track it listens to.
inline ModListen modListenOf(const magda::ModInfo& mod) {
    if (!mod.enabled)
        return ModListen::Nothing;

    // A follower is nothing but its source, whatever its trigger mode says: the
    // trigger fields belong to the shape-driven kinds and a follower has no
    // phase to restart.
    if (mod.type == magda::ModType::Follower)
        return ModListen::Audio;

    switch (mod.triggerMode) {
        case magda::LFOTriggerMode::MIDI:
            return ModListen::Midi;
        case magda::LFOTriggerMode::Audio:
            return ModListen::Audio;
        case magda::LFOTriggerMode::Free:
        case magda::LFOTriggerMode::Transport:
            break;
    }

    return ModListen::Nothing;
}

/**
 * @brief The track a modifier on this scope listens to.
 *
 * @p sidechainSource is the scope's own sidechain source, or INVALID_TRACK_ID
 * where the scope has none or it is not an audio one. @p ownTrack is the track
 * the scope lives on, which is the answer whenever the scope is not sidechained
 * from somewhere else.
 *
 * Empty for a modifier that listens to nothing.
 */
inline std::optional<magda::TrackId> modifierSourceTrack(const magda::ModInfo& mod,
                                                         magda::TrackId sidechainSource,
                                                         magda::TrackId ownTrack) {
    if (modListenOf(mod) == ModListen::Nothing)
        return std::nullopt;

    const auto source = sidechainSource != magda::INVALID_TRACK_ID ? sidechainSource : ownTrack;
    if (source == magda::INVALID_TRACK_ID)
        return std::nullopt;

    return source;
}

/**
 * @brief The source a sidechain config names, or INVALID_TRACK_ID for none.
 *
 * Either type. What a sidechain says here is where a scope's modifiers listen,
 * and what each of them listens for is its own business: an audio sidechain
 * carrying a note-triggered modifier's notes is the same edge to the same
 * track, and the tap emitted for it carries both the level and the MIDI.
 *
 * That is a decision rather than a transcription. The fork picks the monitor
 * from the sidechain's type, so a note-triggered modifier on an audio-keyed
 * device is fed by the level detector there and hears no notes at all, which
 * makes what a modifier does depend on a setting belonging to the device it
 * happens to sit on. Here the modifier's own trigger mode decides, which is the
 * reading the model's own controls describe.
 */
inline magda::TrackId sidechainSourceOf(const magda::SidechainConfig& sidechain) {
    return sidechain.isActive() ? sidechain.sourceTrackId : magda::INVALID_TRACK_ID;
}

/// Every track the modifiers on @p mods listen to, added to @p out.
inline void collectModulationSources(const magda::ModArray& mods, magda::TrackId sidechainSource,
                                     magda::TrackId ownTrack, std::set<magda::TrackId>& out) {
    for (const auto& mod : mods)
        if (const auto source = modifierSourceTrack(mod, sidechainSource, ownTrack))
            out.insert(*source);
}

void collectModulationSources(const std::vector<magda::ChainElement>& elements,
                              magda::TrackId ownTrack, std::set<magda::TrackId>& out);

/// Every track the modifiers anywhere on @p track listen to, added to @p out.
/// Walked in the same shape the parameter table walks its scopes, because the
/// two lists have to be the same list.
void collectModulationSources(const magda::TrackInfo& track, std::set<magda::TrackId>& out);

}  // namespace magda::engine
