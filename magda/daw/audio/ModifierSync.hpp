#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "../core/ChainNode.hpp"
#include "../core/MacroInfo.hpp"
#include "../core/ModInfo.hpp"
#include "CurveSnapshot.hpp"

namespace magda {

namespace te = tracktion;

/**
 * @brief Cheap structural fingerprint over a ChainNode's mods and macros.
 *
 * Captures everything the structural rebuild path keys off; non-structural
 * changes (LFO rate, waveform, link amount) take the in-place properties path.
 *
 * Composable: per-node fingerprints sum together so PluginManager can keep a
 * per-track digest while RackSyncManager works rack-by-rack.
 *
 * Step 2 of issue #1131 — collapses the duplicated structural fingerprints
 * from PluginManager (`computeModLinkFingerprint`) and RackSyncManager
 * (`needsModifierResync`) into one shape.
 */
struct ChainFingerprint {
    int modCount = 0;        // enabled mods with at least one link
    int modLinkCount = 0;    // total mod->target links
    int macroCount = 0;      // macros with at least one link
    int macroLinkCount = 0;  // total macro->target links
    int bipolarCount = 0;    // bipolar links across both kinds (offsets diverge)

    bool operator==(const ChainFingerprint& o) const {
        return modCount == o.modCount && modLinkCount == o.modLinkCount &&
               macroCount == o.macroCount && macroLinkCount == o.macroLinkCount &&
               bipolarCount == o.bipolarCount;
    }
    bool operator!=(const ChainFingerprint& o) const {
        return !(*this == o);
    }

    ChainFingerprint& operator+=(const ChainFingerprint& o) {
        modCount += o.modCount;
        modLinkCount += o.modLinkCount;
        macroCount += o.macroCount;
        macroLinkCount += o.macroLinkCount;
        bipolarCount += o.bipolarCount;
        return *this;
    }
};

/**
 * @brief Per-node TE state — the walker reads/writes through these refs.
 *
 * Storage is owned by callers under their existing field names
 * (`SyncedDevice::modifiers`, `SyncedRack::innerModifiers`, etc.). Holding
 * references here lets the walker drive both call sites without forcing a
 * rename. Storage layout matches what RackSyncManager already uses
 * (map<ModId, ...>); step 2 migrates PluginManager's vector shape to match.
 */
struct ModifierSyncState {
    std::map<ModId, te::Modifier::Ptr>& modifiers;
    std::map<ModId, std::unique_ptr<CurveSnapshotHolder>>& curveSnapshots;
    std::map<int, te::MacroParameter*>& macroParams;
};

/**
 * @brief Per-node bindings the walker needs to interact with TE for one ChainNode.
 *
 * Step 4 of issue #1131 converged the per-scope behavioural divergences that
 * step 2 captured behind boolean flags. The walker now uses one consistent
 * behaviour across Track / Rack / Device scopes (matching the original
 * Track/Device pattern):
 *  - LFO gating only for Audio-trigger LFOs at creation (MIDI gating was
 *    redundant — the audio thread owns gate state via gateSidechainLFOs).
 *  - No legacy single-target macro field handling (modern projects use
 *    `MacroInfo::links`; old projects' `target` field was already dead in
 *    the Track/Device paths).
 *  - Bipolar macros use TE's `addModifier(value, offset)` so they pivot
 *    around 0.5 — RackSyncManager's 2-arg form lacked this.
 *
 * `hasCrossTrackSidechain` stays a per-device fact (top-level devices with
 * a cross-track sidechain set `LFOModifier::skipNativeResync`).
 */
struct ModifierSyncContext {
    /// TE modifier list to insert/remove this node's modifiers.
    /// Track:        teTrack->getModifierList()
    /// Rack:         rackType->getModifierList()
    /// Top device:   teTrack->getModifierList() — or, for an instrument,
    ///               instrumentRackManager_.getRackType(deviceId)->getModifierList().
    te::ModifierList* modifierList = nullptr;

    /// TE macro parameter list to insert/remove this node's macros.
    te::MacroParameterList* macroList = nullptr;

    /// deviceId → te::Plugin* lookup for resolving link targets. Returns
    /// nullptr if the device isn't reachable from this scope (the walker
    /// silently drops such links — matches today's behaviour).
    std::function<te::Plugin*(DeviceId)> lookupTargetPlugin;

    /// Visit every plugin where stale modifier/macro assignments may need
    /// scrubbing. Called once per existing TE modifier/macro being torn down.
    /// For a Rack scope: every inner plugin. For a Track / Top-device scope:
    /// every plugin on the TE track + every instrument-rack inner plugin +
    /// every drum-grid pad plugin.
    std::function<void(const std::function<void(te::Plugin*)>&)> forEachScopePlugin;

    /// True if this node has a cross-track sidechain source — sets
    /// `te::LFOModifier::skipNativeResync` on any LFOs created here, so
    /// the destination track's MIDI doesn't retrigger them.
    bool hasCrossTrackSidechain = false;
};

/**
 * @brief Stateless walker that builds and updates TE modifier+macro state for
 *        one MAGDA ChainNode (Track, Rack, or Device scope).
 *
 * Step 2 of issue #1131 — collapses the duplicated modifier sync code from
 * PluginManager (top-level device path + track-level mod path) and
 * RackSyncManager (rack-internal path) into one walker.
 *
 * Note on file location: the issue called for `magda/daw/core/ModifierSync.hpp`,
 * but the walker pulls in `tracktion_engine` and `CurveSnapshot.hpp`, both of
 * which live in audio/. Keeping the walker in audio/ avoids inverting the
 * core/audio layering.
 */
class ModifierSyncWalker {
  public:
    /// Compute a structural fingerprint for the node's mods + macros.
    /// Composable via `operator+=` so callers can sum across child nodes.
    static ChainFingerprint fingerprintOf(const ConstChainNode& node);

    /// Full structural rebuild. Tears down all TE modifiers + macros for this
    /// node and re-creates them from MAGDA state. Snapshots are deferred for
    /// audio-thread use-after-free safety.
    static void syncStructure(
        const ConstChainNode& node, const ModifierSyncContext& ctx,
        ModifierSyncState& state,
        std::vector<std::unique_ptr<CurveSnapshotHolder>>& deferredHolders);

    /// In-place property update. Keeps existing TE modifiers; updates LFO
    /// properties (rate, waveform, sync) and assignment depths.
    static void syncProperties(const ConstChainNode& node,
                               const ModifierSyncContext& ctx,
                               ModifierSyncState& state);
};

}  // namespace magda
