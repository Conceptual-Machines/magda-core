#include "../../core/ViewModeState.hpp"
#include "ProjectSerializer.hpp"
#include "SerializationHelpers.hpp"

namespace magda {

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
    obj->setProperty("muted", track.muted);
    obj->setProperty("soloed", track.soloed);
    obj->setProperty("recordArmed", track.recordArmed);
    obj->setProperty("inputMonitor", static_cast<int>(track.inputMonitor));
    obj->setProperty("frozen", track.frozen);

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

    // Sends
    juce::Array<juce::var> sendsArray;
    for (const auto& send : track.sends) {
        sendsArray.add(serializeSendInfo(send));
    }
    obj->setProperty("sends", juce::var(sendsArray));

    // Chain elements
    juce::Array<juce::var> chainArray;
    for (const auto& element : track.chainElements) {
        chainArray.add(serializeChainElement(element));
    }
    obj->setProperty("chainElements", juce::var(chainArray));

    return juce::var(obj);
}

bool ProjectSerializer::deserializeTrackInfo(const juce::var& json, TrackInfo& outTrack) {
    if (!json.isObject()) {
        lastError_ = "Track data is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outTrack.id = obj->getProperty("id");
    outTrack.type = static_cast<TrackType>(static_cast<int>(obj->getProperty("type")));
    outTrack.name = obj->getProperty("name").toString();
    outTrack.colour = stringToColour(obj->getProperty("colour").toString());

    // Hierarchy
    outTrack.parentId = obj->getProperty("parentId");
    auto childIdsVar = obj->getProperty("childIds");
    if (childIdsVar.isArray()) {
        auto* arr = childIdsVar.getArray();
        for (const auto& idVar : *arr) {
            outTrack.childIds.push_back(static_cast<int>(idVar));
        }
    }

    // Mixer state
    outTrack.volume = obj->getProperty("volume");
    outTrack.pan = obj->getProperty("pan");
    outTrack.muted = obj->getProperty("muted");
    outTrack.soloed = obj->getProperty("soloed");
    outTrack.recordArmed = obj->getProperty("recordArmed");

    // Input monitor mode (backward compatible - defaults to Off)
    if (obj->hasProperty("inputMonitor")) {
        outTrack.inputMonitor =
            static_cast<InputMonitorMode>(static_cast<int>(obj->getProperty("inputMonitor")));
    }

    // Frozen state (backward compatible - defaults to false)
    if (obj->hasProperty("frozen")) {
        outTrack.frozen = static_cast<bool>(obj->getProperty("frozen"));
    }

    // View settings per view mode (backward compatible - defaults applied if missing)
    auto viewSettingsVar = obj->getProperty("viewSettings");
    if (viewSettingsVar.isObject()) {
        auto* vsObj = viewSettingsVar.getDynamicObject();
        for (auto mode : {ViewMode::Live, ViewMode::Arrange, ViewMode::Mix, ViewMode::Master}) {
            auto modeVar = vsObj->getProperty(getViewModeName(mode));
            if (modeVar.isObject()) {
                auto* modeObj = modeVar.getDynamicObject();
                TrackViewSettings vs;
                vs.visible = modeObj->getProperty("visible");
                vs.locked = modeObj->getProperty("locked");
                vs.collapsed = modeObj->getProperty("collapsed");
                vs.height = modeObj->getProperty("height");
                outTrack.viewSettings.set(mode, vs);
            }
        }
    }

    // Routing
    outTrack.midiInputDevice = obj->getProperty("midiInputDevice").toString();
    outTrack.midiOutputDevice = obj->getProperty("midiOutputDevice").toString();
    outTrack.audioInputDevice = obj->getProperty("audioInputDevice").toString();
    outTrack.audioOutputDevice = obj->getProperty("audioOutputDevice").toString();

    // Aux bus index (defaults to -1 if not present in older project files)
    if (obj->hasProperty("auxBusIndex")) {
        outTrack.auxBusIndex = obj->getProperty("auxBusIndex");
    }

    // Multi-out link (for MultiOut tracks)
    auto multiOutLinkVar = obj->getProperty("multiOutLink");
    if (multiOutLinkVar.isObject()) {
        auto* moLinkObj = multiOutLinkVar.getDynamicObject();
        MultiOutTrackLink link;
        link.sourceTrackId = moLinkObj->getProperty("sourceTrackId");
        link.sourceDeviceId = moLinkObj->getProperty("sourceDeviceId");
        link.outputPairIndex = moLinkObj->getProperty("outputPairIndex");
        outTrack.multiOutLink = link;
    }

    // Sends
    auto sendsVar = obj->getProperty("sends");
    if (sendsVar.isArray()) {
        auto* arr = sendsVar.getArray();
        for (const auto& sendVar : *arr) {
            SendInfo send;
            if (!deserializeSendInfo(sendVar, send))
                return false;
            outTrack.sends.push_back(send);
        }
    }

    // Chain elements
    auto chainVar = obj->getProperty("chainElements");
    if (chainVar.isArray()) {
        auto* arr = chainVar.getArray();
        for (const auto& elementVar : *arr) {
            ChainElement element;
            if (!deserializeChainElement(elementVar, element)) {
                return false;
            }
            outTrack.chainElements.push_back(std::move(element));
        }
    }

    return true;
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

    return juce::var(obj);
}

bool ProjectSerializer::deserializeChainElement(const juce::var& json, ChainElement& outElement) {
    if (!json.isObject()) {
        lastError_ = "Chain element is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();
    juce::String type = obj->getProperty("type").toString();

    if (type == "device") {
        DeviceInfo device;
        if (!deserializeDeviceInfo(obj->getProperty("device"), device)) {
            return false;
        }
        outElement = std::move(device);
    } else if (type == "rack") {
        RackInfo rack;
        if (!deserializeRackInfo(obj->getProperty("rack"), rack)) {
            return false;
        }
        outElement = std::make_unique<RackInfo>(std::move(rack));
    } else {
        lastError_ = "Unknown chain element type: " + type;
        return false;
    }

    return true;
}

juce::var ProjectSerializer::serializeDeviceInfo(const DeviceInfo& device) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", device.id);
    obj->setProperty("name", device.name);
    obj->setProperty("pluginId", device.pluginId);
    obj->setProperty("manufacturer", device.manufacturer);
    obj->setProperty("format", static_cast<int>(device.format));
    obj->setProperty("isInstrument", device.isInstrument);
    obj->setProperty("uniqueId", device.uniqueId);
    obj->setProperty("fileOrIdentifier", device.fileOrIdentifier);
    obj->setProperty("bypassed", device.bypassed);
    obj->setProperty("expanded", device.expanded);
    obj->setProperty("modPanelOpen", device.modPanelOpen);
    obj->setProperty("gainPanelOpen", device.gainPanelOpen);
    obj->setProperty("paramPanelOpen", device.paramPanelOpen);

    // Parameters
    juce::Array<juce::var> paramsArray;
    for (const auto& param : device.parameters) {
        paramsArray.add(serializeParameterInfo(param));
    }
    obj->setProperty("parameters", juce::var(paramsArray));

    // Visible parameters
    juce::Array<juce::var> visibleParamsArray;
    for (auto index : device.visibleParameters) {
        visibleParamsArray.add(index);
    }
    obj->setProperty("visibleParameters", juce::var(visibleParamsArray));

    // Gain stage
    obj->setProperty("gainParameterIndex", device.gainParameterIndex);
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
        multiOutObj->setProperty("mixerChildrenCollapsed", device.multiOut.mixerChildrenCollapsed);

        juce::Array<juce::var> pairsArray;
        for (const auto& pair : device.multiOut.outputPairs) {
            auto* pairObj = new juce::DynamicObject();
            pairObj->setProperty("outputIndex", pair.outputIndex);
            pairObj->setProperty("name", pair.name);
            pairObj->setProperty("active", pair.active);
            pairObj->setProperty("trackId", pair.trackId);
            pairObj->setProperty("firstPin", pair.firstPin);
            pairObj->setProperty("numChannels", pair.numChannels);
            pairsArray.add(juce::var(pairObj));
        }
        multiOutObj->setProperty("outputPairs", juce::var(pairsArray));
        obj->setProperty("multiOut", juce::var(multiOutObj));
    }

    // Sidechain / MIDI receive capabilities
    if (device.canSidechain) {
        obj->setProperty("canSidechain", true);
    }
    if (device.canReceiveMidi) {
        obj->setProperty("canReceiveMidi", true);
    }

    // Sidechain
    if (device.sidechain.isActive()) {
        auto* scObj = new juce::DynamicObject();
        scObj->setProperty("type", static_cast<int>(device.sidechain.type));
        scObj->setProperty("sourceTrackId", device.sidechain.sourceTrackId);
        obj->setProperty("sidechain", juce::var(scObj));
    }

    return juce::var(obj);
}

bool ProjectSerializer::deserializeDeviceInfo(const juce::var& json, DeviceInfo& outDevice) {
    if (!json.isObject()) {
        lastError_ = "Device data is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outDevice.id = obj->getProperty("id");
    outDevice.name = obj->getProperty("name").toString();
    outDevice.pluginId = obj->getProperty("pluginId").toString();
    outDevice.manufacturer = obj->getProperty("manufacturer").toString();
    outDevice.format = static_cast<PluginFormat>(static_cast<int>(obj->getProperty("format")));
    outDevice.isInstrument = obj->getProperty("isInstrument");
    outDevice.uniqueId = obj->getProperty("uniqueId").toString();
    outDevice.fileOrIdentifier = obj->getProperty("fileOrIdentifier").toString();
    outDevice.bypassed = obj->getProperty("bypassed");
    outDevice.expanded = obj->getProperty("expanded");
    outDevice.modPanelOpen = obj->getProperty("modPanelOpen");
    outDevice.gainPanelOpen = obj->getProperty("gainPanelOpen");
    outDevice.paramPanelOpen = obj->getProperty("paramPanelOpen");

    // Parameters
    auto paramsVar = obj->getProperty("parameters");
    if (paramsVar.isArray()) {
        auto* arr = paramsVar.getArray();
        for (const auto& paramVar : *arr) {
            ParameterInfo param;
            if (!deserializeParameterInfo(paramVar, param)) {
                return false;
            }
            outDevice.parameters.push_back(param);
        }
    }

    // Visible parameters
    auto visibleParamsVar = obj->getProperty("visibleParameters");
    if (visibleParamsVar.isArray()) {
        auto* arr = visibleParamsVar.getArray();
        for (const auto& indexVar : *arr) {
            outDevice.visibleParameters.push_back(static_cast<int>(indexVar));
        }
    }

    // Gain stage
    outDevice.gainParameterIndex = obj->getProperty("gainParameterIndex");
    outDevice.gainValue = obj->getProperty("gainValue");
    outDevice.gainDb = obj->getProperty("gainDb");

    // Macros
    auto macrosVar = obj->getProperty("macros");
    if (macrosVar.isArray()) {
        auto* arr = macrosVar.getArray();
        outDevice.macros.clear();
        for (const auto& macroVar : *arr) {
            MacroInfo macro;
            if (!deserializeMacroInfo(macroVar, macro)) {
                return false;
            }
            outDevice.macros.push_back(macro);
        }
    }

    // Mods
    auto modsVar = obj->getProperty("mods");
    if (modsVar.isArray()) {
        auto* arr = modsVar.getArray();
        outDevice.mods.clear();
        for (const auto& modVar : *arr) {
            ModInfo mod;
            if (!deserializeModInfo(modVar, mod)) {
                return false;
            }
            outDevice.mods.push_back(mod);
        }
    }

    outDevice.currentParameterPage = obj->getProperty("currentParameterPage");

    // Multi-output config
    auto multiOutVar = obj->getProperty("multiOut");
    if (multiOutVar.isObject()) {
        auto* moObj = multiOutVar.getDynamicObject();
        outDevice.multiOut.isMultiOut = moObj->getProperty("isMultiOut");
        outDevice.multiOut.totalOutputChannels = moObj->getProperty("totalOutputChannels");
        if (moObj->hasProperty("mixerChildrenCollapsed"))
            outDevice.multiOut.mixerChildrenCollapsed =
                moObj->getProperty("mixerChildrenCollapsed");

        auto pairsVar = moObj->getProperty("outputPairs");
        if (pairsVar.isArray()) {
            auto* pairsArr = pairsVar.getArray();
            for (const auto& pairVar : *pairsArr) {
                if (auto* pairObj = pairVar.getDynamicObject()) {
                    MultiOutOutputPair pair;
                    pair.outputIndex = pairObj->getProperty("outputIndex");
                    pair.name = pairObj->getProperty("name").toString();
                    pair.active = pairObj->getProperty("active");
                    pair.trackId = pairObj->getProperty("trackId");
                    if (pairObj->hasProperty("firstPin"))
                        pair.firstPin = pairObj->getProperty("firstPin");
                    if (pairObj->hasProperty("numChannels"))
                        pair.numChannels = pairObj->getProperty("numChannels");
                    outDevice.multiOut.outputPairs.push_back(pair);
                }
            }
        }
    }

    // Sidechain / MIDI receive capabilities
    auto canSidechainVar = obj->getProperty("canSidechain");
    if (!canSidechainVar.isVoid()) {
        outDevice.canSidechain = static_cast<bool>(canSidechainVar);
    }
    auto canReceiveMidiVar = obj->getProperty("canReceiveMidi");
    if (!canReceiveMidiVar.isVoid()) {
        outDevice.canReceiveMidi = static_cast<bool>(canReceiveMidiVar);
    }

    // Sidechain
    auto sidechainVar = obj->getProperty("sidechain");
    if (sidechainVar.isObject()) {
        auto* scObj = sidechainVar.getDynamicObject();
        outDevice.sidechain.type =
            static_cast<SidechainConfig::Type>(static_cast<int>(scObj->getProperty("type")));
        outDevice.sidechain.sourceTrackId = scObj->getProperty("sourceTrackId");
    }

    return true;
}

juce::var ProjectSerializer::serializeRackInfo(const RackInfo& rack) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", rack.id);
    obj->setProperty("name", rack.name);
    obj->setProperty("bypassed", rack.bypassed);
    obj->setProperty("expanded", rack.expanded);
    obj->setProperty("volume", rack.volume);
    obj->setProperty("pan", rack.pan);

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
    if (rack.sidechain.isActive()) {
        auto* scObj = new juce::DynamicObject();
        scObj->setProperty("type", static_cast<int>(rack.sidechain.type));
        scObj->setProperty("sourceTrackId", rack.sidechain.sourceTrackId);
        obj->setProperty("sidechain", juce::var(scObj));
    }

    return juce::var(obj);
}

bool ProjectSerializer::deserializeRackInfo(const juce::var& json, RackInfo& outRack) {
    if (!json.isObject()) {
        lastError_ = "Rack data is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outRack.id = obj->getProperty("id");
    outRack.name = obj->getProperty("name").toString();
    outRack.bypassed = obj->getProperty("bypassed");
    outRack.expanded = obj->getProperty("expanded");
    outRack.volume = obj->getProperty("volume");
    outRack.pan = obj->getProperty("pan");

    // Chains
    auto chainsVar = obj->getProperty("chains");
    if (chainsVar.isArray()) {
        auto* arr = chainsVar.getArray();
        outRack.chains.clear();
        for (const auto& chainVar : *arr) {
            ChainInfo chain;
            if (!deserializeChainInfo(chainVar, chain)) {
                return false;
            }
            outRack.chains.push_back(std::move(chain));
        }
    }

    // Macros
    auto macrosVar = obj->getProperty("macros");
    if (macrosVar.isArray()) {
        auto* arr = macrosVar.getArray();
        outRack.macros.clear();
        for (const auto& macroVar : *arr) {
            MacroInfo macro;
            if (!deserializeMacroInfo(macroVar, macro)) {
                return false;
            }
            outRack.macros.push_back(macro);
        }
    }

    // Mods
    auto modsVar = obj->getProperty("mods");
    if (modsVar.isArray()) {
        auto* arr = modsVar.getArray();
        outRack.mods.clear();
        for (const auto& modVar : *arr) {
            ModInfo mod;
            if (!deserializeModInfo(modVar, mod)) {
                return false;
            }
            outRack.mods.push_back(mod);
        }
    }

    // Sidechain
    auto sidechainVar = obj->getProperty("sidechain");
    if (sidechainVar.isObject()) {
        auto* scObj = sidechainVar.getDynamicObject();
        outRack.sidechain.type =
            static_cast<SidechainConfig::Type>(static_cast<int>(scObj->getProperty("type")));
        outRack.sidechain.sourceTrackId = scObj->getProperty("sourceTrackId");
    }

    return true;
}

juce::var ProjectSerializer::serializeChainInfo(const ChainInfo& chain) {
    auto* obj = new juce::DynamicObject();

    obj->setProperty("id", chain.id);
    obj->setProperty("name", chain.name);
    obj->setProperty("outputIndex", chain.outputIndex);
    obj->setProperty("muted", chain.muted);
    obj->setProperty("solo", chain.solo);
    obj->setProperty("volume", chain.volume);
    obj->setProperty("pan", chain.pan);
    obj->setProperty("expanded", chain.expanded);

    // Elements
    juce::Array<juce::var> elementsArray;
    for (const auto& element : chain.elements) {
        elementsArray.add(serializeChainElement(element));
    }
    obj->setProperty("elements", juce::var(elementsArray));

    return juce::var(obj);
}

bool ProjectSerializer::deserializeChainInfo(const juce::var& json, ChainInfo& outChain) {
    if (!json.isObject()) {
        lastError_ = "Chain data is not an object";
        return false;
    }

    auto* obj = json.getDynamicObject();

    outChain.id = obj->getProperty("id");
    outChain.name = obj->getProperty("name").toString();
    outChain.outputIndex = obj->getProperty("outputIndex");
    outChain.muted = obj->getProperty("muted");
    outChain.solo = obj->getProperty("solo");
    outChain.volume = obj->getProperty("volume");
    outChain.pan = obj->getProperty("pan");
    outChain.expanded = obj->getProperty("expanded");

    // Elements
    auto elementsVar = obj->getProperty("elements");
    if (elementsVar.isArray()) {
        auto* arr = elementsVar.getArray();
        outChain.elements.clear();
        for (const auto& elementVar : *arr) {
            ChainElement element;
            if (!deserializeChainElement(elementVar, element)) {
                return false;
            }
            outChain.elements.push_back(std::move(element));
        }
    }

    return true;
}

juce::var ProjectSerializer::serializeSendInfo(const SendInfo& data) {
    auto* obj = new juce::DynamicObject();
    SER(busIndex);
    SER(level);
    SER(preFader);
    SER(destTrackId);
    return juce::var(obj);
}

bool ProjectSerializer::deserializeSendInfo(const juce::var& json, SendInfo& data) {
    if (!json.isObject()) {
        lastError_ = "Send info is not an object";
        return false;
    }
    auto* obj = json.getDynamicObject();
    DESER(busIndex);
    DESER(level);
    DESER(preFader);
    DESER(destTrackId);
    return true;
}

}  // namespace magda
