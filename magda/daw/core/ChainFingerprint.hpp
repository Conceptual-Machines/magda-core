#pragma once

#include "ChainNode.hpp"
#include "RackInfo.hpp"

namespace magda {

/**
 * @brief Cheap structural fingerprint over a ChainNode's mods and macros.
 *
 * Captures everything the structural rebuild path keys off; non-structural
 * changes (LFO rate, waveform, link amount) take the in-place properties path.
 *
 * Composable: per-node fingerprints sum together so callers can keep a
 * per-track digest while RackSyncManager works rack-by-rack.
 *
 * Lives in core/ (not audio/) so tests can construct fingerprints from pure
 * MAGDA model data without pulling in tracktion_engine.h.
 */
struct ChainFingerprint {
    int modCount = 0;        ///< enabled mods with at least one link
    int modLinkCount = 0;    ///< total mod->target links
    int macroCount = 0;      ///< macros with at least one link
    int macroLinkCount = 0;  ///< total macro->target links
    int bipolarCount = 0;    ///< bipolar links across both kinds (offsets diverge)

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
 * @brief Compute a structural fingerprint for one ChainNode.
 *
 * Counts only enabled mods that have at least one link, and macros with at
 * least one link. An enabled-but-linkless mod still creates a TE modifier
 * (so a macro can target its rate) but the fingerprint doesn't bump for it
 * — the structural rebuild fires when the first link appears.
 */
inline ChainFingerprint fingerprintOf(const ConstChainNode& node) {
    ChainFingerprint fp;
    if (!node.valid())
        return fp;

    if (node.mods) {
        for (const auto& mod : *node.mods) {
            if (mod.enabled && !mod.links.empty()) {
                ++fp.modCount;
                fp.modLinkCount += static_cast<int>(mod.links.size());
                for (const auto& link : mod.links)
                    fp.bipolarCount += link.bipolar ? 1 : 0;
            }
        }
    }

    if (node.macros) {
        for (const auto& macro : *node.macros) {
            if (!macro.links.empty()) {
                ++fp.macroCount;
                fp.macroLinkCount += static_cast<int>(macro.links.size());
                for (const auto& link : macro.links)
                    fp.bipolarCount += link.bipolar ? 1 : 0;
            }
        }
    }

    return fp;
}

/**
 * @brief Sum the fingerprint for an entire MAGDA rack — rack-scope mods +
 *        macros, plus each enabled inner device's mods + macros.
 *
 * All of these live on the same rackType modifier list at sync time, so a
 * structural change to any of them needs the full syncRackModulation
 * rebuild path. resyncAllModifiers uses this to gate structural rebuild
 * vs the in-place property path.
 */
inline ChainFingerprint computeRackFingerprint(const RackInfo& rackInfo) {
    ChainFingerprint fp;

    ConstChainNode rackNode;
    rackNode.scope = ChainScope::Rack;
    rackNode.mods = &rackInfo.mods;
    rackNode.macros = &rackInfo.macros;
    fp += fingerprintOf(rackNode);

    for (const auto& chain : rackInfo.chains) {
        for (const auto& chainElement : chain.elements) {
            if (isRack(chainElement)) {
                fp += computeRackFingerprint(getRack(chainElement));
                continue;
            }
            if (!isDevice(chainElement))
                continue;

            const auto& device = getDevice(chainElement);
            if (device.bypassed)
                continue;
            ConstChainNode deviceNode;
            deviceNode.scope = ChainScope::Device;
            deviceNode.mods = &device.mods;
            deviceNode.macros = &device.macros;
            fp += fingerprintOf(deviceNode);
        }
    }
    return fp;
}

}  // namespace magda
