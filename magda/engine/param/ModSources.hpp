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

/// Where @p mod listens, which only means anything when it listens to audio.
/// The model's own field, read through here so both compilers ask one question.
inline magda::ModTapPoint modTapPointOf(const magda::ModInfo& mod) {
    return mod.tapPoint;
}

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
 * Either type, because the type is not this question's. A sidechain says which
 * track a scope reads; the trigger mode on the modifier says what that modifier
 * is waiting for. The fork separates them the same way: it installs its MIDI
 * monitor for a track with a note-triggered modifier and its level monitor for
 * one with an audio-triggered modifier, both read off the modes rather than off
 * any sidechain's type (trackNeedsSidechainMonitor, trackNeedsAudioSidechain-
 * Monitor).
 */
inline magda::TrackId sidechainSourceOf(const magda::SidechainConfig& sidechain) {
    return sidechain.isActive() ? sidechain.sourceTrackId : magda::INVALID_TRACK_ID;
}

/**
 * @brief One place the modulation system reads a track.
 *
 * A track is read at one point or two, never more: the two the engines have.
 * What decides which is what listens, and a track with a follower at the far
 * end and a trigger at the near one needs both.
 */
struct ModTap {
    magda::TrackId track = magda::INVALID_TRACK_ID;
    magda::ModTapPoint point = magda::ModTapPoint::PreFx;

    auto operator<=>(const ModTap&) const = default;
};

/**
 * @brief Every point the modifiers on @p mods read, added to @p out.
 *
 * An audio listener reads the point it names. A note listener reads the near
 * one, and not because notes are near: MIDI has no fader to sit either side
 * of, so it rides the tap that a track listened to at all always has rather
 * than earning one of its own.
 */
inline void collectModulationTaps(const magda::ModArray& mods, magda::TrackId sidechainSource,
                                  magda::TrackId ownTrack, std::set<ModTap>& out) {
    for (const auto& mod : mods) {
        const auto source = modifierSourceTrack(mod, sidechainSource, ownTrack);
        if (!source)
            continue;

        out.insert(ModTap{*source, modListenOf(mod) == ModListen::Midi ? magda::ModTapPoint::PreFx
                                                                       : modTapPointOf(mod)});
    }
}

/// Every track the modifiers on @p mods listen to for notes, added to @p out.
/// A source has to compile its MIDI ops whether or not its own chain wants
/// them, which is a question about the track rather than about a tap point.
inline void collectNoteSources(const magda::ModArray& mods, magda::TrackId sidechainSource,
                               magda::TrackId ownTrack, std::set<magda::TrackId>& out) {
    for (const auto& mod : mods)
        if (modListenOf(mod) == ModListen::Midi)
            if (const auto source = modifierSourceTrack(mod, sidechainSource, ownTrack))
                out.insert(*source);
}

void collectModulationTaps(const std::vector<magda::ChainElement>& elements,
                           magda::TrackId ownTrack, std::set<ModTap>& out,
                           std::set<magda::TrackId>& notes);

/// Every point the modifiers anywhere on @p track read, and every track any of
/// them reads for notes. Walked in the same shape the parameter table walks its
/// scopes, because the two lists have to be the same list.
void collectModulationTaps(const magda::TrackInfo& track, std::set<ModTap>& out,
                           std::set<magda::TrackId>& notes);

}  // namespace magda::engine
