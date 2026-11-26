// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>

#include "../../audio/plugins/InternalPluginRegistry.hpp"
#include "../../core/DeviceParamMigrations.hpp"
#include "../../core/DrumGridPads.hpp"
#include "../../core/PluginCapabilities.hpp"
#include "../../core/ViewModeState.hpp"
#include "ProjectMigration.hpp"
#include "ProjectSerializer.hpp"
#include "SerializationHelpers.hpp"

namespace magda {

namespace {

void enforcePostFxAnalysisDeviceOrder(std::vector<PostFxChainElement>& elements) {
    const auto analysisOrderOf = [](const PostFxChainElement& element) {
        return daw::audio::internalPostFxAnalysisOrder(element.device.pluginId);
    };
    auto findAnalysis = [&elements, &analysisOrderOf](int order) {
        return std::ranges::find(elements, order, analysisOrderOf);
    };

    auto osc = findAnalysis(0);
    auto spectrum = findAnalysis(1);
    if (osc != elements.end() && spectrum != elements.end() && spectrum < osc)
        std::iter_swap(osc, spectrum);
}

}  // namespace

// ============================================================================
// Track serialization helpers
// ============================================================================

juce::var ProjectSerializer::serializeTrackInfo(const TrackInfo& track) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", track.id);
    obj->setProperty("type", static_cast<int>(track.type));
    obj->setProperty("name", track.name);
    obj->setProperty("colour", colourToString(track.colour));

    // Hierarchy
    obj->setProperty("parentId", track.parentId);
    juce::Array<juce::var> childIdsArray;
    for (auto childId : track.childIds) {
        childIdsArray.add(childId);
    }
    obj->setProperty("childIds", juce::var(childIdsArray));

    // Mixer state
    obj->setProperty("volume", track.volume);
    obj->setProperty("pan", track.pan);
    obj->setProperty("manualVolume", track.manualVolume);
    obj->setProperty("manualPan", track.manualPan);
    obj->setProperty("muted", track.muted);
    obj->setProperty("soloed", track.soloed);
    obj->setProperty("recordArmed", track.recordArmed);
    obj->setProperty("inputMonitor", static_cast<int>(track.inputMonitor));
    obj->setProperty("frozen", track.frozen);
    obj->setProperty("enabled", track.enabled);
    obj->setProperty("playbackMode", static_cast<int>(track.playbackMode));
    if (track.mixerChannelWidth > 0)
        obj->setProperty("mixerChannelWidth", track.mixerChannelWidth);
    if (track.mixerFaderTopInset > 0)
        obj->setProperty("mixerFaderTopInset", track.mixerFaderTopInset);
    if (track.activeSessionClipId != INVALID_CLIP_ID)
        obj->setProperty("activeSessionClipId", track.activeSessionClipId);

    // View settings per view mode
    auto* viewSettingsObj = new juce::DynamicObject();
    for (auto mode : {ViewMode::Live, ViewMode::Arrange, ViewMode::Mix, ViewMode::Master}) {
        const auto& vs = track.viewSettings.get(mode);
        auto* modeObj = new juce::DynamicObject();
        modeObj->setProperty("visible", vs.visible);
        modeObj->setProperty("locked", vs.locked);
        modeObj->setProperty("collapsed", vs.collapsed);
        modeObj->setProperty("height", vs.height);
        viewSettingsObj->setProperty(getViewModeName(mode), juce::var(modeObj));
    }
    obj->setProperty("viewSettings", juce::var(viewSettingsObj));

    // Routing
    obj->setProperty("midiInputDevice", track.midiInputDevice);
    obj->setProperty("midiOutputDevice", track.midiOutputDevice);
    obj->setProperty("audioInputDevice", track.audioInputDevice);
    obj->setProperty("audioOutputDevice", track.audioOutputDevice);
    if (track.midiInputChannel != 0)
        obj->setProperty("midiInputChannel", track.midiInputChannel);
    if (track.midiOutputChannel != 0)
        obj->setProperty("midiOutputChannel", track.midiOutputChannel);

    // Aux bus index (for aux tracks)
    obj->setProperty("auxBusIndex", track.auxBusIndex);

    // Multi-out link (for MultiOut tracks)
    if (track.multiOutLink.has_value()) {
        auto* moLinkObj = new juce::DynamicObject();
        moLinkObj->setProperty("sourceTrackId", track.multiOutLink->sourceTrackId);
        moLinkObj->setProperty("sourceDeviceId", track.multiOutLink->sourceDeviceId);
        moLinkObj->setProperty("outputPairIndex", track.multiOutLink->outputPairIndex);
        obj->setProperty("multiOutLink", juce::var(moLinkObj));
    }

    if (track.padLink.has_value()) {
        auto* padLinkObj = new juce::DynamicObject();
        padLinkObj->setProperty("gridId", track.padLink->gridId);
        padLinkObj->setProperty("padId", track.padLink->padId);
        obj->setProperty("padLink", juce::var(padLinkObj));
    }

    // Sends
    juce::Array<juce::var> sendsArray;
    for (const auto& send : track.sends) {
        sendsArray.add(serializeSendInfo(send));
    }
    obj->setProperty("sends", juce::var(sendsArray));

    // Chain elements
    juce::Array<juce::var> chainArray;
    for (const auto& element : track.chain.fxChainElements) {
        chainArray.add(serializeChainElement(element));
    }
    obj->setProperty("chainElements", juce::var(chainArray));
    obj->setProperty("chainEnabled", track.chain.enabled);

    // Post-fader FX chain elements
    juce::Array<juce::var> postFxArray;
    for (const auto& element : track.chain.postFxChainElements) {
        postFxArray.add(serializeDeviceInfo(element.device));
    }
    obj->setProperty("postFxChainElements", juce::var(postFxArray));
    obj->setProperty("postFxPostFader", track.chain.postFxPostFader);

    // Mixer-analysis section: rail-managed mini oscilloscope / spectrum devices.
    // Persist their per-device pluginState so UI settings (e.g. trace colour)
    // survive project save/load instead of falling back to global last-used
    // defaults.
    if (!track.chain.mixerAnalysisElements.empty()) {
        juce::Array<juce::var> mixerAnalysisArray;
        for (const auto& element : track.chain.mixerAnalysisElements) {
            mixerAnalysisArray.add(serializeDeviceInfo(element.device));
        }
        obj->setProperty("mixerAnalysisElements", juce::var(mixerAnalysisArray));
    }

    // Track-level mods and macros
    juce::Array<juce::var> trackModsArray;
    for (const auto& mod : track.mods) {
        trackModsArray.add(serializeModInfo(mod));
    }
    obj->setProperty("trackMods", juce::var(trackModsArray));

    juce::Array<juce::var> trackMacrosArray;
    for (const auto& macro : track.macros) {
        trackMacrosArray.add(serializeMacroInfo(macro));
    }
    obj->setProperty("trackMacros", juce::var(trackMacrosArray));

    if (track.chainPresetName.isNotEmpty())
        obj->setProperty("chainPresetName", track.chainPresetName);

    // Track-level panel UI state
    obj->setProperty("globalModsPanelOpen", track.globalModsPanelOpen);
    obj->setProperty("globalMacrosPanelOpen", track.globalMacrosPanelOpen);
    obj->setProperty("selectedGlobalModIndex", track.selectedGlobalModIndex);
    obj->setProperty("selectedGlobalMacroIndex", track.selectedGlobalMacroIndex);

    return {obj};
}

bool ProjectSerializer::deserializeTrackInfo(const juce::var& json, TrackInfo& outTrack) {
    outTrack = {};
    return ser::decodeDocument(json, "track", lastError_,
                               [&outTrack](const ser::ObjectReader& in) { read(in, outTrack); });
}

void ProjectSerializer::read(const ser::ObjectReader& in, TrackInfo& outTrack) {
    in.required("id", outTrack.id);
    if (int type = 0; in.required("type", type))
        outTrack.type = trackTypeFromInt(type);
    in.required("name", outTrack.name);
    in.required("colour", outTrack.colour);

    in.required("parentId", outTrack.parentId);
    in.readArray("childIds", outTrack.childIds);

    in.required("volume", outTrack.volume);
    in.required("pan", outTrack.pan);
    // Absent before automation moved the fader: the fader is where it was left.
    outTrack.manualVolume = outTrack.volume;
    outTrack.manualPan = outTrack.pan;
    in.optional("manualVolume", outTrack.manualVolume);
    in.optional("manualPan", outTrack.manualPan);
    in.required("muted", outTrack.muted);
    in.required("soloed", outTrack.soloed);
    in.required("recordArmed", outTrack.recordArmed);
    in.optional("inputMonitor", outTrack.inputMonitor);
    in.optional("frozen", outTrack.frozen);
    in.optional("enabled", outTrack.enabled);
    in.optional("playbackMode", outTrack.playbackMode);
    in.optional("mixerChannelWidth", outTrack.mixerChannelWidth);
    in.optional("mixerFaderTopInset", outTrack.mixerFaderTopInset);
    in.optional("activeSessionClipId", outTrack.activeSessionClipId);

    if (const auto viewSettings = in.optionalObject("viewSettings")) {
        for (auto mode : {ViewMode::Live, ViewMode::Arrange, ViewMode::Mix, ViewMode::Master}) {
            if (const auto modeIn = viewSettings->optionalObject(getViewModeName(mode))) {
                TrackViewSettings vs;
                modeIn->required("visible", vs.visible);
                modeIn->required("locked", vs.locked);
                modeIn->required("collapsed", vs.collapsed);
                modeIn->required("height", vs.height);
                outTrack.viewSettings.set(mode, vs);
            }
        }
    }

    in.required("midiInputDevice", outTrack.midiInputDevice);
    in.required("midiOutputDevice", outTrack.midiOutputDevice);
    in.required("audioInputDevice", outTrack.audioInputDevice);
    in.required("audioOutputDevice", outTrack.audioOutputDevice);
    if (in.optional("midiInputChannel", outTrack.midiInputChannel))
        outTrack.midiInputChannel = juce::jlimit(0, 16, outTrack.midiInputChannel);
    if (in.optional("midiOutputChannel", outTrack.midiOutputChannel))
        outTrack.midiOutputChannel = juce::jlimit(0, 16, outTrack.midiOutputChannel);
    in.optional("auxBusIndex", outTrack.auxBusIndex);

    if (const auto link = in.optionalObject("multiOutLink")) {
        MultiOutTrackLink multiOut;
        link->required("sourceTrackId", multiOut.sourceTrackId);
        link->required("sourceDeviceId", multiOut.sourceDeviceId);
        link->required("outputPairIndex", multiOut.outputPairIndex);
        outTrack.multiOutLink = multiOut;
    }
    if (const auto link = in.optionalObject("padLink")) {
        PadChannelLink padLink;
        link->required("gridId", padLink.gridId);
        link->required("padId", padLink.padId);
        outTrack.padLink = padLink;
    }

    in.forEachObject("sends", [&outTrack](const ser::ObjectReader& item) {
        SendInfo send;
        read(item, send);
        outTrack.sends.push_back(send);
    });

    // Missing before the chain power flag and the fader boundary (#2087): both default on,
    // so a post-FX processor in an older project moves below the fader on load.
    in.optional("chainEnabled", outTrack.chain.enabled);
    in.optional("postFxPostFader", outTrack.chain.postFxPostFader);

    in.forEachObject("chainElements", [&outTrack](const ser::ObjectReader& item) {
        ChainElement element;
        read(item, element);
        outTrack.chain.fxChainElements.push_back(std::move(element));
    });
    in.forEachObject("postFxChainElements", [&outTrack](const ser::ObjectReader& item) {
        DeviceInfo device;
        read(item, device);
        outTrack.chain.postFxChainElements.push_back(PostFxChainElement{std::move(device)});
    });
    enforcePostFxAnalysisDeviceOrder(outTrack.chain.postFxChainElements);
    in.forEachObject("mixerAnalysisElements", [&outTrack](const ser::ObjectReader& item) {
        DeviceInfo device;
        read(item, device);
        outTrack.chain.mixerAnalysisElements.push_back(PostFxChainElement{std::move(device)});
    });

    if (in.has("trackMods")) {
        outTrack.mods.clear();
        in.forEachObject("trackMods", [&outTrack](const ser::ObjectReader& item) {
            ModInfo mod(static_cast<int>(outTrack.mods.size()));
            read(item, mod);
            outTrack.mods.push_back(mod);
        });
    }
    if (in.has("trackMacros")) {
        outTrack.macros.clear();
        in.forEachObject("trackMacros", [&outTrack](const ser::ObjectReader& item) {
            MacroInfo macro;
            read(item, macro);
            outTrack.macros.push_back(macro);
        });
    }

    in.optional("chainPresetName", outTrack.chainPresetName);
    in.optional("globalModsPanelOpen", outTrack.globalModsPanelOpen);
    in.optional("globalMacrosPanelOpen", outTrack.globalMacrosPanelOpen);
    in.optional("selectedGlobalModIndex", outTrack.selectedGlobalModIndex);
    in.optional("selectedGlobalMacroIndex", outTrack.selectedGlobalMacroIndex);
}

juce::var ProjectSerializer::serializeChainElement(const ChainElement& element) {
    auto* obj = new juce::DynamicObject();

    if (isDevice(element)) {
        obj->setProperty("type", "device");
        obj->setProperty("device", serializeDeviceInfo(getDevice(element)));
    } else if (isRack(element)) {
        obj->setProperty("type", "rack");
        obj->setProperty("rack", serializeRackInfo(getRack(element)));
    }

    return {obj};
}

bool ProjectSerializer::deserializeChainElement(const juce::var& json, ChainElement& outElement) {
    outElement = {};
    return ser::decodeDocument(json, "element", lastError_,
                               [&outElement](const auto& in) { read(in, outElement); });
}

void ProjectSerializer::read(const ser::ObjectReader& in, ChainElement& outElement) {
    juce::String type;
    if (!in.required("type", type))
        return;

    if (type == "device") {
        DeviceInfo device;
        read(in.object("device"), device);
        outElement = std::move(device);
    } else if (type == "rack") {
        RackInfo rack;
        read(in.object("rack"), rack);
        outElement = std::make_unique<RackInfo>(std::move(rack));
    } else {
        in.fail("type", "unknown chain element type: " + type);
    }
}

juce::var ProjectSerializer::serializeDeviceInfo(const DeviceInfo& device) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", device.id);
    obj->setProperty("name", device.name);
    obj->setProperty("pluginId", device.pluginId);
    obj->setProperty("manufacturer", device.manufacturer);
    obj->setProperty("format", static_cast<int>(device.format));
    obj->setProperty("isInstrument", device.isInstrument);
    obj->setProperty("deviceType", static_cast<int>(device.deviceType));
    if (device.browserCategoryOverride.isNotEmpty())
        obj->setProperty("browserCategoryOverride", device.browserCategoryOverride);
    obj->setProperty("uniqueId", device.uniqueId);
    obj->setProperty("fileOrIdentifier", device.fileOrIdentifier);
    obj->setProperty("bypassed", device.bypassed);
    obj->setProperty("deltaSolo", device.deltaSolo);
    obj->setProperty("expanded", device.expanded);
    obj->setProperty("modPanelOpen", device.modPanelOpen);
    obj->setProperty("gainPanelOpen", device.gainPanelOpen);
    obj->setProperty("paramPanelOpen", device.paramPanelOpen);
    obj->setProperty("aiPanelOpen", device.aiPanelOpen);
    if (device.presetName.isNotEmpty())
        obj->setProperty("presetName", device.presetName);
    obj->setProperty("faceplateShown", device.faceplateShown);
    obj->setProperty("paramsShown", device.paramsShown);
    obj->setProperty("timeBendShown", device.timeBendShown);
    obj->setProperty("zonesShown", device.zonesShown);
    obj->setProperty("oscPanelShown", device.oscPanelShown);
    obj->setProperty("matrixShown", device.matrixShown);
    obj->setProperty("vizShown", device.vizShown);
    obj->setProperty("outputShown", device.outputShown);
    obj->setProperty("padPanelShown", device.padPanelShown);
    obj->setProperty("padChainShown", device.padChainShown);

    // Parameters
    juce::Array<juce::var> paramsArray;
    for (const auto& param : device.parameters) {
        paramsArray.add(serializeParameterInfo(param));
    }
    obj->setProperty("parameters", juce::var(paramsArray));

    // Slots (#2638), under their own keys so a reader can tell them from the
    // positions older projects hold.
    const auto writeSlots = [&obj](const char* key, const std::vector<int>& slots) {
        juce::Array<juce::var> array;
        for (const auto slot : slots)
            array.add(slot);
        obj->setProperty(key, juce::var(array));
    };

    writeSlots("visibleParameterSlots", device.visibleParameters);
    writeSlots("miniMixerParameterSlots", device.miniMixerParameters);
    writeSlots("aiSoundDesignerParameterSlots", device.aiSoundDesignerParameters);
    obj->setProperty("aiSoundDesignerPrompt", device.aiSoundDesignerPrompt);

    // Device volume
    obj->setProperty("gainValue", device.gainValue);
    obj->setProperty("gainDb", device.gainDb);

    // Macros
    juce::Array<juce::var> macrosArray;
    for (const auto& macro : device.macros) {
        macrosArray.add(serializeMacroInfo(macro));
    }
    obj->setProperty("macros", juce::var(macrosArray));

    // Mods
    juce::Array<juce::var> modsArray;
    for (const auto& mod : device.mods) {
        modsArray.add(serializeModInfo(mod));
    }
    obj->setProperty("mods", juce::var(modsArray));

    obj->setProperty("currentParameterPage", device.currentParameterPage);

    // Multi-output config
    if (device.multiOut.isMultiOut) {
        auto* multiOutObj = new juce::DynamicObject();
        multiOutObj->setProperty("isMultiOut", true);
        multiOutObj->setProperty("totalOutputChannels", device.multiOut.totalOutputChannels);

        juce::Array<juce::var> pairsArray;
        for (const auto& pair : device.multiOut.outputPairs) {
            auto* pairObj = new juce::DynamicObject();
            pairObj->setProperty("outputIndex", pair.outputIndex);
            pairObj->setProperty("name", pair.name);
            // `active` and `trackId` are not written: which child track a pair
            // drives is the child track's `multiOutLink`, and persisting a
            // second copy is what let the two disagree (#2220).
            pairObj->setProperty("firstPin", pair.firstPin);
            pairObj->setProperty("numChannels", pair.numChannels);
            pairsArray.add(juce::var(pairObj));
        }
        multiOutObj->setProperty("outputPairs", juce::var(pairsArray));
        obj->setProperty("multiOut", juce::var(multiOutObj));
    }

    // Plugin native state (base64 blob, only if captured)
    if (device.hasPluginState()) {
        obj->setProperty("pluginState", device.pluginState);
    }

    // Sidechain / MIDI receive capabilities
    if (device.sidechainPort.declared()) {
        obj->setProperty("sidechainPortKind", static_cast<int>(device.sidechainPort.kind));
        obj->setProperty("sidechainPortChannels", device.sidechainPort.channels);
    }
    if (device.canReceiveMidi) {
        obj->setProperty("canReceiveMidi", true);
    }
    if (device.producesMidi) {
        obj->setProperty("producesMidi", true);
    }
    // Always persisted: default is true, so a missing field must read back as
    // true (old projects) and an explicit user-disabled false must survive.
    obj->setProperty("midiInThru", device.midiInThru);

    // Audio channel counts, only when they differ from the stereo default.
    // Never a zero output count: that is the one value that silences the chain
    // behind the device, and on a machine where the plugin is missing it would
    // do so for no reason. Same rule as the flags above, which are only written
    // when true: a stale saved value must not be able to take routing away.
    if (device.audioInputChannels != 2)
        obj->setProperty("audioInputChannels", device.audioInputChannels);
    if (device.audioOutputChannels != 2 && device.audioOutputChannels > 0)
        obj->setProperty("audioOutputChannels", device.audioOutputChannels);

    // Per-instance drum kit rows
    if (!device.kitRows.empty()) {
        juce::Array<juce::var> kitArray;
        for (const auto& r : device.kitRows) {
            auto* rowObj = new juce::DynamicObject();
            rowObj->setProperty("note", r.noteNumber);
            if (r.label.isNotEmpty())
                rowObj->setProperty("label", r.label);
            if (r.role.isNotEmpty())
                rowObj->setProperty("role", r.role);
            kitArray.add(juce::var(rowObj));
        }
        obj->setProperty("kitRows", juce::var(kitArray));
    }

    // Sidechain
    // An insert routed nowhere yet still keeps its CC knobs (#3164).
    if (device.insert.isActive() || !device.insert.ccSlots.empty() ||
        device.insert.ccDevice.isNotEmpty() || device.insert.midiChannel != 1 ||
        device.insert.nextCcParamIndex != 0) {
        auto* insertObj = new juce::DynamicObject();
        insertObj->setProperty("sendType", static_cast<int>(device.insert.sendType));
        insertObj->setProperty("returnType", static_cast<int>(device.insert.returnType));
        insertObj->setProperty("sendDevice", device.insert.sendDevice);
        insertObj->setProperty("returnDevice", device.insert.returnDevice);
        insertObj->setProperty("manualAdjustMs", device.insert.manualAdjustMs);
        insertObj->setProperty("midiChannel", device.insert.midiChannel);
        insertObj->setProperty("ccDevice", device.insert.ccDevice);
        juce::Array<juce::var> ccSlots;
        for (const auto& slot : device.insert.ccSlots) {
            auto* slotObj = new juce::DynamicObject();
            slotObj->setProperty("paramIndex", slot.paramIndex);
            slotObj->setProperty("ccNumber", slot.ccNumber);
            ccSlots.add(juce::var(slotObj));
        }
        insertObj->setProperty("ccSlots", ccSlots);
        insertObj->setProperty("nextCcParamIndex", device.insert.nextCcParamIndex);
        obj->setProperty("insert", juce::var(insertObj));
    }

    if (device.sidechain.isConfigured()) {
        auto* scObj = new juce::DynamicObject();
        scObj->setProperty("type", static_cast<int>(device.sidechain.type));
        scObj->setProperty("sourceTrackId", device.sidechain.sourceTrackId);
        if (!device.sidechain.enabled)
            scObj->setProperty("enabled", false);
        // Only when they are not the defaults a project that predates them
        // reads back as: post-fader, no trim, not listening (#2329).
        if (device.sidechain.tapPoint != ModTapPoint::PostFader)
            scObj->setProperty("tapPoint", static_cast<int>(device.sidechain.tapPoint));
        if (device.sidechain.gainDb != 0.0f)
            scObj->setProperty("gainDb", device.sidechain.gainDb);
        if (device.sidechain.listen)
            scObj->setProperty("listen", true);
        obj->setProperty("sidechain", juce::var(scObj));
    }

    // A pad-per-chain device's pads. Saved as the rack they are, so what the
    // model holds is what the project carries: nothing re-reads them out of
    // `pluginState`, which is where they used to live (#2207).
    if (device.pads)
        obj->setProperty("pads", serializeRackInfo(*device.pads.get()));

    return {obj};
}

bool ProjectSerializer::deserializeDeviceInfo(const juce::var& json, DeviceInfo& outDevice) {
    outDevice = {};
    return ser::decodeDocument(json, "device", lastError_,
                               [&outDevice](const ser::ObjectReader& in) { read(in, outDevice); });
}

void ProjectSerializer::read(const ser::ObjectReader& raw, DeviceInfo& outDevice) {
    const auto in = project_migration::upgradeDevice(raw);

    in.required("id", outDevice.id);
    in.required("name", outDevice.name);
    in.required("pluginId", outDevice.pluginId);
    in.required("manufacturer", outDevice.manufacturer);
    // A format this build does not know falls back rather than casting out of range.
    if (int format = 0; in.required("format", format))
        outDevice.format = isKnownPluginFormatValue(format) ? static_cast<PluginFormat>(format)
                                                            : PluginFormat::VST3;
    in.required("isInstrument", outDevice.isInstrument);
    outDevice.deviceType = outDevice.isInstrument ? DeviceType::Instrument : DeviceType::Effect;
    in.optional("deviceType", outDevice.deviceType);
    in.optional("browserCategoryOverride", outDevice.browserCategoryOverride);
    in.required("uniqueId", outDevice.uniqueId);
    in.required("fileOrIdentifier", outDevice.fileOrIdentifier);
    in.required("bypassed", outDevice.bypassed);
    in.optional("deltaSolo", outDevice.deltaSolo);

    in.optional("expanded", outDevice.expanded);
    in.optional("modPanelOpen", outDevice.modPanelOpen);
    in.optional("gainPanelOpen", outDevice.gainPanelOpen);
    in.optional("paramPanelOpen", outDevice.paramPanelOpen);
    in.optional("aiPanelOpen", outDevice.aiPanelOpen);
    in.optional("presetName", outDevice.presetName);
    in.optional("faceplateShown", outDevice.faceplateShown);
    in.optional("paramsShown", outDevice.paramsShown);
    if (!outDevice.faceplateShown)
        outDevice.paramsShown = true;
    in.optional("timeBendShown", outDevice.timeBendShown);
    in.optional("zonesShown", outDevice.zonesShown);
    in.optional("oscPanelShown", outDevice.oscPanelShown);
    in.optional("matrixShown", outDevice.matrixShown);
    in.optional("vizShown", outDevice.vizShown);
    in.optional("outputShown", outDevice.outputShown);
    in.optional("padPanelShown", outDevice.padPanelShown);
    in.optional("padChainShown", outDevice.padChainShown);

    in.forEachObject("parameters", [&outDevice](const ser::ObjectReader& item) {
        ParameterInfo param;
        read(item, param);
        outDevice.parameters.push_back(param);
    });
    in.readArray("visibleParameterSlots", outDevice.visibleParameters);
    in.readArray("miniMixerParameterSlots", outDevice.miniMixerParameters);
    in.readArray("aiSoundDesignerParameterSlots", outDevice.aiSoundDesignerParameters);
    in.optional("aiSoundDesignerPrompt", outDevice.aiSoundDesignerPrompt);

    in.required("gainValue", outDevice.gainValue);
    in.required("gainDb", outDevice.gainDb);

    if (in.has("macros")) {
        outDevice.macros.clear();
        in.forEachObject("macros", [&outDevice](const ser::ObjectReader& item) {
            MacroInfo macro;
            read(item, macro);
            outDevice.macros.push_back(macro);
        });
    }
    if (in.has("mods")) {
        outDevice.mods.clear();
        in.forEachObject("mods", [&outDevice](const ser::ObjectReader& item) {
            ModInfo mod;
            read(item, mod);
            outDevice.mods.push_back(mod);
        });
    }
    in.optional("currentParameterPage", outDevice.currentParameterPage);

    if (const auto multiOut = in.optionalObject("multiOut")) {
        multiOut->required("isMultiOut", outDevice.multiOut.isMultiOut);
        multiOut->required("totalOutputChannels", outDevice.multiOut.totalOutputChannels);
        // Older pairs also carry `active` and `trackId`, unread: the child tracks' links
        // are the one record of which pair drives which track (#2220).
        multiOut->forEachObject("outputPairs", [&outDevice](const ser::ObjectReader& item) {
            MultiOutOutputPair pair;
            item.required("outputIndex", pair.outputIndex);
            item.required("name", pair.name);
            item.optional("firstPin", pair.firstPin);
            item.optional("numChannels", pair.numChannels);
            outDevice.multiOut.outputPairs.push_back(pair);
        });
    }

    if (in.optional("sidechainPortKind", outDevice.sidechainPort.kind))
        in.required("sidechainPortChannels", outDevice.sidechainPort.channels);
    in.optional("canReceiveMidi", outDevice.canReceiveMidi);
    in.optional("producesMidi", outDevice.producesMidi);
    in.optional("midiInThru", outDevice.midiInThru);
    in.optional("audioInputChannels", outDevice.audioInputChannels);
    // Zero comes from the live plugin or not at all: a saved one would silence the chain.
    if (int outputs = 0; in.optional("audioOutputChannels", outputs) && outputs > 0)
        outDevice.audioOutputChannels = outputs;

    in.optional("pluginState", outDevice.pluginState);

    // Saved as the rack they are, not inside `pluginState` (#2207).
    if (const auto pads = in.optionalObject("pads")) {
        auto rack = std::make_unique<RackInfo>();
        read(*pads, *rack);
        outDevice.pads.reset(std::move(rack));
        stampPadRackId(outDevice);
    }

    in.forEachObject("kitRows", [&outDevice](const ser::ObjectReader& item) {
        KitRow row;
        if (item.required("note", row.noteNumber))
            row.noteNumber = juce::jlimit(0, 127, row.noteNumber);
        item.optional("label", row.label);
        item.optional("role", row.role);
        outDevice.kitRows.push_back(std::move(row));
    });

    if (const auto insert = in.optionalObject("insert")) {
        auto& config = outDevice.insert;
        insert->required("sendType", config.sendType);
        insert->required("returnType", config.returnType);
        insert->required("sendDevice", config.sendDevice);
        insert->required("returnDevice", config.returnDevice);
        insert->required("manualAdjustMs", config.manualAdjustMs);
        // Saved before #3164 there is no channel, so the default's.
        if (int channel = 0; insert->optional("midiChannel", channel))
            config.midiChannel = channel >= 1 && channel <= 16 ? channel : 1;
        insert->optional("ccDevice", config.ccDevice);
        insert->optional("nextCcParamIndex", config.nextCcParamIndex);
        config.ccSlots.clear();
        insert->forEachObject("ccSlots", [&config](const ser::ObjectReader& item) {
            int paramIndex = 0;
            int ccNumber = 0;
            item.optional("paramIndex", paramIndex);
            item.optional("ccNumber", ccNumber);
            config.ccSlots.push_back(
                {.paramIndex = paramIndex, .ccNumber = juce::jlimit(0, 127, ccNumber)});
        });
    }

    if (const auto sidechain = in.optionalObject("sidechain")) {
        auto& config = outDevice.sidechain;
        sidechain->required("type", config.type);
        sidechain->required("sourceTrackId", config.sourceTrackId);
        // Absent in a project that predates them: post-fader, no trim, not listening.
        sidechain->optional("enabled", config.enabled);
        sidechain->optional("tapPoint", config.tapPoint);
        sidechain->optional("gainDb", config.gainDb);
        sidechain->optional("listen", config.listen);
    }

    project_migration::migrateDevice(outDevice);
    applyCachedCapabilitiesToDevice(outDevice);
}

juce::var ProjectSerializer::serializeRackInfo(const RackInfo& rack) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", rack.id);
    obj->setProperty("name", rack.name);
    if (rack.presetName.isNotEmpty())
        obj->setProperty("presetName", rack.presetName);
    obj->setProperty("bypassed", rack.bypassed);
    obj->setProperty("deltaSolo", rack.deltaSolo);
    obj->setProperty("expanded", rack.expanded);
    obj->setProperty("modPanelOpen", rack.modPanelOpen);
    obj->setProperty("paramPanelOpen", rack.paramPanelOpen);
    obj->setProperty("volume", rack.volume);
    obj->setProperty("pan", rack.pan);
    obj->setProperty("chainSelector", rack.chainSelector);
    if (rack.multiband) {
        obj->setProperty("multiband", true);
        juce::Array<juce::var> crossovers;
        for (const auto& crossover : rack.crossovers) {
            auto* item = new juce::DynamicObject();
            item->setProperty("frequencyHz", crossover.frequencyHz);
            item->setProperty("slope", slopeDbPerOctave(crossover.slope));
            crossovers.add(juce::var(item));
        }
        obj->setProperty("crossovers", juce::var(crossovers));
        obj->setProperty("faceplateShown", rack.faceplateShown);
    }
    obj->setProperty("bandsShown", rack.bandsShown);
    obj->setProperty("chainShown", rack.chainShown);

    // Chains
    juce::Array<juce::var> chainsArray;
    for (const auto& chain : rack.chains) {
        chainsArray.add(serializeChainInfo(chain));
    }
    obj->setProperty("chains", juce::var(chainsArray));

    // Macros
    juce::Array<juce::var> macrosArray;
    for (const auto& macro : rack.macros) {
        macrosArray.add(serializeMacroInfo(macro));
    }
    obj->setProperty("macros", juce::var(macrosArray));

    // Mods
    juce::Array<juce::var> modsArray;
    for (const auto& mod : rack.mods) {
        modsArray.add(serializeModInfo(mod));
    }
    obj->setProperty("mods", juce::var(modsArray));

    // Sidechain
    if (rack.sidechain.isConfigured()) {
        auto* scObj = new juce::DynamicObject();
        scObj->setProperty("type", static_cast<int>(rack.sidechain.type));
        scObj->setProperty("sourceTrackId", rack.sidechain.sourceTrackId);
        if (!rack.sidechain.enabled)
            scObj->setProperty("enabled", false);
        obj->setProperty("sidechain", juce::var(scObj));
    }

    return {obj};
}

bool ProjectSerializer::deserializeRackInfo(const juce::var& json, RackInfo& outRack) {
    outRack = {};
    return ser::decodeDocument(json, "rack", lastError_,
                               [&outRack](const ser::ObjectReader& in) { read(in, outRack); });
}

void ProjectSerializer::read(const ser::ObjectReader& in, RackInfo& outRack) {
    in.required("id", outRack.id);
    in.required("name", outRack.name);
    in.optional("presetName", outRack.presetName);
    in.required("bypassed", outRack.bypassed);
    in.optional("deltaSolo", outRack.deltaSolo);
    in.optional("expanded", outRack.expanded);
    in.optional("modPanelOpen", outRack.modPanelOpen);
    in.optional("paramPanelOpen", outRack.paramPanelOpen);
    in.required("volume", outRack.volume);
    in.required("pan", outRack.pan);
    if (in.optional("chainSelector", outRack.chainSelector))
        outRack.chainSelector = juce::jlimit(0.0f, 127.0f, outRack.chainSelector);

    if (in.has("chains")) {
        outRack.chains.clear();
        in.forEachObject("chains", [&outRack](const ser::ObjectReader& item) {
            ChainInfo chain;
            read(item, chain);
            outRack.chains.push_back(std::move(chain));
        });
    }
    if (in.has("macros")) {
        outRack.macros.clear();
        in.forEachObject("macros", [&outRack](const ser::ObjectReader& item) {
            MacroInfo macro;
            read(item, macro);
            outRack.macros.push_back(macro);
        });
    }
    if (in.has("mods")) {
        outRack.mods.clear();
        in.forEachObject("mods", [&outRack](const ser::ObjectReader& item) {
            ModInfo mod;
            read(item, mod);
            outRack.mods.push_back(mod);
        });
    }

    outRack.multiband = false;
    in.optional("multiband", outRack.multiband);
    outRack.crossovers.clear();
    in.forEachObject("crossovers", [&outRack](const ser::ObjectReader& item) {
        float frequencyHz = 1000.0f;
        int slope = 24;
        item.optional("frequencyHz", frequencyHz);
        item.optional("slope", slope);
        outRack.crossovers.push_back({frequencyHz, slope == 12   ? CrossoverSlope::Db12
                                                   : slope == 48 ? CrossoverSlope::Db48
                                                                 : CrossoverSlope::Db24});
    });
    outRack.faceplateShown = true;
    in.optional("faceplateShown", outRack.faceplateShown);
    bool bandsShown = true;
    in.optional("bandsShown", bandsShown);
    outRack.bandsShown = !outRack.faceplateShown || bandsShown;
    in.optional("chainShown", outRack.chainShown);
    normaliseMultiband(outRack);

    if (const auto sidechain = in.optionalObject("sidechain")) {
        sidechain->required("type", outRack.sidechain.type);
        sidechain->required("sourceTrackId", outRack.sidechain.sourceTrackId);
        sidechain->optional("enabled", outRack.sidechain.enabled);
    }
}

juce::var ProjectSerializer::serializeChainInfo(const ChainInfo& chain) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", chain.id);
    obj->setProperty("name", chain.name);
    obj->setProperty("outputIndex", chain.outputIndex);
    obj->setProperty("muted", chain.muted);
    obj->setProperty("solo", chain.solo);
    obj->setProperty("bypassed", chain.bypassed);
    obj->setProperty("volume", chain.volume);
    obj->setProperty("pan", chain.pan);
    obj->setProperty("expanded", chain.expanded);

    // The notes a pad answers to, and the pitch they are transposed onto. Only
    // for a chain that is keyed by pitch: a plain parallel chain takes every
    // note, which is what the absent properties read back as (#2207).
    if (!chain.answersToEveryNote()) {
        obj->setProperty("lowNote", chain.lowNote);
        obj->setProperty("highNote", chain.highNote);
        obj->setProperty("rootNote", chain.rootNote);
    }

    if (!chain.zones.isOpen()) {
        auto* zones = new juce::DynamicObject();
        zones->setProperty("keyLow", chain.zones.keyLow);
        zones->setProperty("keyHigh", chain.zones.keyHigh);
        zones->setProperty("keyFadeLow", chain.zones.keyFadeLow);
        zones->setProperty("keyFadeHigh", chain.zones.keyFadeHigh);
        zones->setProperty("velocityLow", chain.zones.velocityLow);
        zones->setProperty("velocityHigh", chain.zones.velocityHigh);
        zones->setProperty("velocityFadeLow", chain.zones.velocityFadeLow);
        zones->setProperty("velocityFadeHigh", chain.zones.velocityFadeHigh);
        zones->setProperty("roundRobin", chain.zones.roundRobin);
        zones->setProperty("selectorLow", chain.zones.selectorLow);
        zones->setProperty("selectorHigh", chain.zones.selectorHigh);
        zones->setProperty("selectorFadeLow", chain.zones.selectorFadeLow);
        zones->setProperty("selectorFadeHigh", chain.zones.selectorFadeHigh);
        obj->setProperty("zones", juce::var(zones));
    }

    // Elements
    juce::Array<juce::var> elementsArray;
    for (const auto& element : chain.elements) {
        elementsArray.add(serializeChainElement(element));
    }
    obj->setProperty("elements", juce::var(elementsArray));

    return {obj};
}

bool ProjectSerializer::deserializeChainInfo(const juce::var& json, ChainInfo& outChain) {
    outChain = {};
    return ser::decodeDocument(json, "chain", lastError_,
                               [&outChain](const ser::ObjectReader& in) { read(in, outChain); });
}

void ProjectSerializer::read(const ser::ObjectReader& in, ChainInfo& outChain) {
    in.required("id", outChain.id);
    in.required("name", outChain.name);

    // Normalised, not trusted: a pad on a bus that reaches no track is silenced, so a saved
    // value outside the range the buses exist in would lose the audio (#2211).
    if (in.required("outputIndex", outChain.outputIndex))
        outChain.outputIndex = juce::jlimit(0, kPadBusCount - 1, outChain.outputIndex);
    in.required("muted", outChain.muted);
    in.required("solo", outChain.solo);
    in.required("bypassed", outChain.bypassed);
    in.required("volume", outChain.volume);
    in.required("pan", outChain.pan);
    in.optional("expanded", outChain.expanded);

    // Absent for a chain not keyed by pitch, which reads back as answering every note (#2207).
    in.optional("lowNote", outChain.lowNote);
    in.optional("highNote", outChain.highNote);
    in.optional("rootNote", outChain.rootNote);

    // Clamped into MIDI's range; an inverted range is kept and plays nothing.
    if (const auto zones = in.optionalObject("zones")) {
        auto& z = outChain.zones;
        const auto readZone = [&zones](const char* name, int& value, int low, int high) {
            if (zones->optional(name, value))
                value = juce::jlimit(low, high, value);
        };
        readZone("keyLow", z.keyLow, 0, 127);
        readZone("keyHigh", z.keyHigh, 0, 127);
        readZone("keyFadeLow", z.keyFadeLow, 0, 127);
        readZone("keyFadeHigh", z.keyFadeHigh, 0, 127);
        readZone("velocityLow", z.velocityLow, 1, 127);
        readZone("velocityHigh", z.velocityHigh, 1, 127);
        readZone("velocityFadeLow", z.velocityFadeLow, 0, 127);
        readZone("velocityFadeHigh", z.velocityFadeHigh, 0, 127);
        zones->optional("roundRobin", z.roundRobin);
        readZone("selectorLow", z.selectorLow, 0, 127);
        readZone("selectorHigh", z.selectorHigh, 0, 127);
        readZone("selectorFadeLow", z.selectorFadeLow, 0, 127);
        readZone("selectorFadeHigh", z.selectorFadeHigh, 0, 127);
    }

    if (in.has("elements")) {
        outChain.elements.clear();
        in.forEachObject("elements", [&outChain](const ser::ObjectReader& item) {
            ChainElement element;
            read(item, element);
            outChain.elements.push_back(std::move(element));
        });
    }
}

juce::var ProjectSerializer::serializeSendInfo(const SendInfo& data) {
    auto* obj = new juce::DynamicObject();
    SER(busIndex);
    SER(level);
    SER(preFader);
    SER(destTrackId);
    SER(enabled);
    SER(id);
    return {obj};
}

void ProjectSerializer::read(const ser::ObjectReader& in, SendInfo& data) {
    DESER(busIndex);
    DESER(level);
    DESER(preFader);
    DESER(destTrackId);
    DESER_OPT(enabled);
    DESER_OPT(id);
}

}  // namespace magda
