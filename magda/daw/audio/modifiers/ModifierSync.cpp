#include "modifiers/ModifierSync.hpp"

#include "modifiers/ADSRDebugLog.hpp"
#include "modifiers/ModifierHelpers.hpp"

namespace magda {

namespace {

// Walk MAGDA mods + the freshly built TE modifier map to find the TE param a
// ModParam-kind link should attach to. modParamIndex 0 is the unified Rate
// lane: routes to TE's `rate` in Hz mode and `rateType` in sync mode. Index 1
// is depth. Same-scope only — cross-device / cross-rack ModParam targeting
// isn't supported (returns nullptr).
template <typename Link>
te::AutomatableParameter* resolveSameScopeModParam(
    const Link& link, const std::vector<ModInfo>& scopeMods,
    const std::map<ModId, te::Modifier::Ptr>& scopeTeMods) {
    if (link.target.kind != decltype(link.target.kind)::ModParam)
        return nullptr;

    auto teIt = scopeTeMods.find(link.target.modId);
    if (teIt == scopeTeMods.end() || !teIt->second)
        return nullptr;

    bool sync = false;
    for (const auto& m : scopeMods) {
        if (m.id == link.target.modId) {
            sync = m.tempoSync;
            break;
        }
    }
    const juce::String wantedID =
        link.target.modParamIndex == 0 ? (sync ? "rateType" : "rate") : "depth";
    for (auto* p : teIt->second->getAutomatableParameters()) {
        if (p && p->paramID == wantedID)
            return p;
    }
    return nullptr;
}

// Insert a new TE modifier of the right type for `modInfo` into `modList`,
// applying LFO properties + gating per the context's flags. Returns nullptr
// on unsupported types or insertion failure.
te::Modifier::Ptr createModifier(const ModInfo& modInfo, te::ModifierList& modList,
                                 const ModifierSyncContext& ctx, ModifierSyncState& state) {
    te::Modifier::Ptr modifier;

    switch (modInfo.type) {
        case ModType::LFO: {
            juce::ValueTree lfoState(te::IDs::LFO);
            auto lfoMod = modList.insertModifier(lfoState, -1, nullptr);
            if (!lfoMod)
                break;

            if (auto* lfo = dynamic_cast<te::LFOModifier*>(lfoMod.get())) {
                auto& snapHolder = state.curveSnapshots[modInfo.id];
                if (!snapHolder)
                    snapHolder = std::make_unique<CurveSnapshotHolder>();
                applyLFOProperties(lfo, modInfo, snapHolder.get());

                // Cross-track sidechain LFOs must not retrigger from the
                // destination track's own MIDI — they're driven externally
                // via triggerSidechainNoteOn from the source track.
                if (ctx.hasCrossTrackSidechain)
                    lfo->setSkipNativeResync(true);

                // Audio-trigger LFOs start gated; the audio thread clears the
                // gate on each peak (gateSidechainLFOs / triggerNoteOn).
                // MIDI-trigger LFOs are gated in applyLFOProperties from
                // MAGDA's held-note model so rack and top-level scopes match.
                if (modInfo.triggerMode == LFOTriggerMode::Audio) {
                    lfo->setGated(true);
                } else if (modInfo.triggerMode == LFOTriggerMode::MIDI) {
                    lfo->setGateOnTriggerSource(true);
                }
            }
            modifier = lfoMod;
            break;
        }
        case ModType::Random: {
            juce::ValueTree randomState(te::IDs::RANDOM);
            auto randomMod = modList.insertModifier(randomState, -1, nullptr);
            if (!randomMod)
                break;

            if (auto* rnd = dynamic_cast<te::RandomModifier*>(randomMod.get()))
                applyRandomProperties(rnd, modInfo);
            modifier = randomMod;
            break;
        }
        case ModType::Follower: {
            juce::ValueTree envState(te::IDs::ENVELOPEFOLLOWER);
            auto efMod = modList.insertModifier(envState, -1, nullptr);
            if (!efMod)
                break;

            if (auto* ef = dynamic_cast<te::EnvelopeFollowerModifier*>(efMod.get())) {
                applyFollowerProperties(ef, modInfo);
                // Always externally driven: a FollowerSourceTapPlugin streams the
                // band-limited post-FX level of the source track (own track in
                // self-mode, the sidechain source in cross-track mode). TE's
                // pc.destBuffer path isn't the track audio at the modifier's
                // position in our graph, so self-mode would otherwise stay silent.
                ef->setUsesExternalInput(true);
                MAGDA_ADSR_AUDIO_LOG(
                    "follower-sync create modId="
                    << static_cast<int>(modInfo.id) << " gainDb=" << modInfo.followerGainDb
                    << " hpOn=" << static_cast<int>(modInfo.followerHpEnabled)
                    << " hpHz=" << modInfo.followerHpFreq
                    << " lpOn=" << static_cast<int>(modInfo.followerLpEnabled)
                    << " lpHz=" << modInfo.followerLpFreq << " attack=" << modInfo.followerAttackMs
                    << " hold=" << modInfo.followerHoldMs
                    << " release=" << modInfo.followerReleaseMs
                    << " usesExternal=" << static_cast<int>(ef->getUsesExternalInput()));
            }
            modifier = efMod;
            break;
        }
        case ModType::Envelope: {
            juce::ValueTree adsrState(te::IDs::ADSR);
            auto adsrMod = modList.insertModifier(adsrState, -1, nullptr);
            if (!adsrMod)
                break;

            if (auto* adsr = dynamic_cast<te::ADSRModifier*>(adsrMod.get())) {
                // applyADSRProperties updates parameters and the non-audio gate modes.
                // Audio mode starts gated here, then the audio monitor owns the gate.
                applyADSRProperties(adsr, modInfo);
                if (modInfo.triggerMode == LFOTriggerMode::Audio)
                    adsr->setGated(true);
            }
            modifier = adsrMod;
            break;
        }
    }

    return modifier;
}

// Look up the TE plugin for a link.target, and return its automatable parameter
// at `paramIndex` if in range.
te::AutomatableParameter* resolveLinkTargetParam(const ModifierSyncContext& ctx,
                                                 const ChainNodePath& targetPath, int paramIndex) {
    auto* plugin = ctx.lookup ? ctx.lookup->getPlugin(targetPath) : nullptr;
    if (!plugin)
        return nullptr;
    auto params = plugin->getAutomatableParameters();
    if (paramIndex < 0 || paramIndex >= static_cast<int>(params.size()))
        return nullptr;
    return params[static_cast<size_t>(paramIndex)];
}

template <typename Source>
void removeSourceAssignments(const ModifierSyncContext& ctx, ModifierSyncState& state,
                             Source& source) {
    if (ctx.forEachScopePlugin) {
        ctx.forEachScopePlugin([&source](te::Plugin* plugin) {
            if (!plugin)
                return;
            for (auto* param : plugin->getAutomatableParameters()) {
                if (param)
                    param->removeModifier(source);
            }
        });
    }

    for (auto& [_modId, modifier] : state.modifiers) {
        if (!modifier)
            continue;
        for (auto* param : modifier->getAutomatableParameters()) {
            if (param)
                param->removeModifier(source);
        }
    }
}

}  // namespace

// =============================================================================
// syncStructure — full rebuild
// =============================================================================

void ModifierSyncWalker::syncStructure(
    const ConstChainNode& node, const ModifierSyncContext& ctx, ModifierSyncState& state,
    std::vector<std::unique_ptr<CurveSnapshotHolder>>& deferredHolders) {
    if (!node.valid())
        return;

    // ---- Tear down existing modifiers ----
    if (!state.modifiers.empty()) {
        clearLFOCustomWaveCallbacks(state.modifiers);

        for (auto& [modId, mod] : state.modifiers) {
            if (!mod)
                continue;

            // Scrub modifier assignments from every target reachable from this
            // scope, including same-scope modifier params.
            removeSourceAssignments(ctx, state, *mod);

            if (ctx.modifierList)
                ctx.modifierList->state.removeChild(mod->state, nullptr);
        }

        state.modifiers.clear();
    }

    // Defer destruction of curve snapshots until the next sync cycle so the
    // audio thread can finish any in-flight evaluateCallback before the
    // memory goes away. clearLFOCustomWaveCallbacks above stored null into
    // the LFO's userData pointer; deferring covers the publication-race window.
    deferCurveSnapshots(state.curveSnapshots, deferredHolders);
    state.curveSnapshots.clear();

    // ---- Tear down existing macros ----
    if (!state.macroParams.empty() && ctx.macroList) {
        for (auto& [macroIdx, macroParam] : state.macroParams) {
            if (!macroParam)
                continue;

            removeSourceAssignments(ctx, state, *macroParam);

            ctx.macroList->removeMacroParameter(*macroParam);
        }
        state.macroParams.clear();
    }

    if (!ctx.modifierList)
        return;  // No TE container yet (e.g. rack with null rackType) — nothing to build.

    // ---- Pass 1: create all TE modifiers ----
    //
    // Two-pass build: ModParam-kind links can target a modifier that's later
    // in the iteration. Splitting the passes guarantees every TE modifier
    // exists before any link is wired, so cross-mod links don't silently drop.
    if (node.mods) {
        for (const auto& modInfo : *node.mods) {
            // Match the gate used by today's syncDeviceModifiers / syncModifiers:
            // an enabled-but-linkless mod is still kept around as a target for
            // ModParam-kind incoming links. Disabled mods skip TE entirely.
            if (!modInfo.enabled)
                continue;

            auto modifier = createModifier(modInfo, *ctx.modifierList, ctx, state);
            if (modifier)
                state.modifiers[modInfo.id] = modifier;
        }

        // ---- Pass 2: wire link assignments ----
        for (const auto& modInfo : *node.mods) {
            if (!modInfo.enabled)
                continue;

            auto srcIt = state.modifiers.find(modInfo.id);
            if (srcIt == state.modifiers.end() || !srcIt->second)
                continue;

            auto& sourceMod = *srcIt->second;

            for (const auto& link : modInfo.links) {
                if (!link.enabled || !link.isValid())
                    continue;

                if (link.target.kind == ControlTarget::Kind::ModParam) {
                    // Refuse self-target (UI filters it out; belt-and-braces).
                    if (link.target.modId == modInfo.id)
                        continue;
                    if (auto* targetParam =
                            resolveSameScopeModParam(link, *node.mods, state.modifiers))
                        addLinkModifier(*targetParam, sourceMod, link);
                    continue;
                }

                if (auto* param =
                        resolveLinkTargetParam(ctx, link.target.devicePath, link.target.paramIndex))
                    addLinkModifier(*param, sourceMod, link);
            }
        }
    }

    // ---- Macros — single-pass create + wire ----
    if (node.macros && ctx.macroList) {
        for (int i = 0; i < static_cast<int>(node.macros->size()); ++i) {
            const auto& macroInfo = (*node.macros)[static_cast<size_t>(i)];
            if (!macroInfo.isLinked())
                continue;

            auto* macroParam = ctx.macroList->createMacroParameter();
            if (!macroParam)
                continue;

            macroParam->macroName = macroInfo.name;
            macroParam->setParameterFromHost(macroInfo.value, juce::dontSendNotification);
            state.macroParams[i] = macroParam;

            for (const auto& link : macroInfo.links) {
                if (!link.target.isValid())
                    continue;

                if (link.target.kind == ControlTarget::Kind::ModParam) {
                    if (auto* targetParam =
                            node.mods ? resolveSameScopeModParam(link, *node.mods, state.modifiers)
                                      : nullptr) {
                        addLinkModifier(*targetParam, *macroParam, link);
                    }
                    continue;
                }

                auto* param =
                    resolveLinkTargetParam(ctx, link.target.devicePath, link.target.paramIndex);
                if (!param)
                    continue;

                addLinkModifier(*param, *macroParam, link);
            }
        }
    }
}

// =============================================================================
// syncProperties — in-place update
// =============================================================================

void ModifierSyncWalker::syncProperties(const ConstChainNode& node, const ModifierSyncContext& ctx,
                                        ModifierSyncState& state) {
    if (!node.valid())
        return;

    // Keep TE modifier objects alive, but rebuild their assignments from the
    // MAGDA link model. This makes deleted links and target path changes
    // immediately remove live Tracktion assignments.
    for (auto& [_modId, modifier] : state.modifiers) {
        if (modifier)
            removeSourceAssignments(ctx, state, *modifier);
    }
    for (auto& [_macroIdx, macroParam] : state.macroParams) {
        if (macroParam)
            removeSourceAssignments(ctx, state, *macroParam);
    }

    // ---- Update LFO properties + rebuild mod assignments ----
    if (node.mods) {
        for (const auto& modInfo : *node.mods) {
            if (!modInfo.enabled)
                continue;

            auto modIt = state.modifiers.find(modInfo.id);
            if (modIt == state.modifiers.end() || !modIt->second)
                continue;

            auto& modifier = modIt->second;

            if (auto* lfo = dynamic_cast<te::LFOModifier*>(modifier.get())) {
                auto& snapHolder = state.curveSnapshots[modInfo.id];
                if (!snapHolder)
                    snapHolder = std::make_unique<CurveSnapshotHolder>();
                applyLFOProperties(lfo, modInfo, snapHolder.get());
                // MIDI gate state is part of the MAGDA model and is applied
                // above. Audio-trigger gate state remains owned by the audio
                // sidechain path.
            } else if (auto* adsr = dynamic_cast<te::ADSRModifier*>(modifier.get())) {
                applyADSRProperties(adsr, modInfo);
            } else if (auto* rnd = dynamic_cast<te::RandomModifier*>(modifier.get())) {
                applyRandomProperties(rnd, modInfo);
            } else if (auto* ef = dynamic_cast<te::EnvelopeFollowerModifier*>(modifier.get())) {
                applyFollowerProperties(ef, modInfo);
                // Always external-input driven (fed by FollowerSourceTapPlugin),
                // matching syncStructure - keep self-mode working after a param
                // tweak, which takes this in-place path (no link-fingerprint change).
                ef->setUsesExternalInput(true);
                MAGDA_ADSR_AUDIO_LOG(
                    "follower-sync update modId="
                    << static_cast<int>(modInfo.id) << " gainDb=" << modInfo.followerGainDb
                    << " hpOn=" << static_cast<int>(modInfo.followerHpEnabled)
                    << " hpHz=" << modInfo.followerHpFreq
                    << " lpOn=" << static_cast<int>(modInfo.followerLpEnabled)
                    << " lpHz=" << modInfo.followerLpFreq << " attack=" << modInfo.followerAttackMs
                    << " hold=" << modInfo.followerHoldMs
                    << " release=" << modInfo.followerReleaseMs
                    << " usesExternal=" << static_cast<int>(ef->getUsesExternalInput()));
            }

            for (const auto& link : modInfo.links) {
                if (!link.enabled || !link.isValid())
                    continue;

                te::AutomatableParameter* param = nullptr;
                if (link.target.kind == ControlTarget::Kind::ModParam) {
                    if (link.target.modId == modInfo.id)
                        continue;  // Self-target — skipped, see syncStructure.
                    param = resolveSameScopeModParam(link, *node.mods, state.modifiers);
                } else {
                    param =
                        resolveLinkTargetParam(ctx, link.target.devicePath, link.target.paramIndex);
                }
                if (!param)
                    continue;

                addLinkModifier(*param, *modifier, link);
            }
        }
    }

    // ---- Rebuild macro assignments ----
    if (node.macros) {
        for (int macroIdx = 0; macroIdx < static_cast<int>(node.macros->size()); ++macroIdx) {
            auto mpIt = state.macroParams.find(macroIdx);
            if (mpIt == state.macroParams.end() || mpIt->second == nullptr)
                continue;
            auto* macroParam = mpIt->second;

            const auto& macroInfo = (*node.macros)[static_cast<size_t>(macroIdx)];
            for (const auto& link : macroInfo.links) {
                if (!link.target.isValid())
                    continue;

                te::AutomatableParameter* param = nullptr;
                if (link.target.kind == ControlTarget::Kind::ModParam) {
                    param = node.mods ? resolveSameScopeModParam(link, *node.mods, state.modifiers)
                                      : nullptr;
                } else {
                    param =
                        resolveLinkTargetParam(ctx, link.target.devicePath, link.target.paramIndex);
                }
                if (!param)
                    continue;

                addLinkModifier(*param, *macroParam, link);
            }
        }
    }
}

}  // namespace magda
