#include "AutomationInfo.hpp"

#include "DeviceInfo.hpp"
#include "ModInfo.hpp"
#include "RackInfo.hpp"
#include "TrackManager.hpp"

namespace magda {

namespace {

// Resolve the live ModInfo for a ModParam target. The lane's interpretation
// of the rate curve depends on the modifier's current tempoSync flag, so we
// have to look at the underlying mod state — same scope walk as the playback
// resolver in PluginManager (rack scope → device scope → track scope).
const ModInfo* resolveModInfoForTarget(const AutomationTarget& target) {
    auto& tm = TrackManager::getInstance();
    const auto* track = tm.getTrack(target.devicePath.trackId);
    if (!track)
        return nullptr;

    if (target.devicePath.isValid()) {
        auto resolved = tm.resolvePath(target.devicePath);
        if (resolved.valid && resolved.rack) {
            for (const auto& m : resolved.rack->mods)
                if (m.id == target.modId)
                    return &m;
        } else if (resolved.valid && resolved.device) {
            for (const auto& m : resolved.device->mods)
                if (m.id == target.modId)
                    return &m;
        }
    }
    for (const auto& m : track->mods)
        if (m.id == target.modId)
            return &m;
    return nullptr;
}

ParameterInfo makeHzRateInfo(const juce::String& name) {
    // Logarithmic Hz scale, range chosen so the geometric centre
    // (sqrt(min*max)) lands on 1 Hz — the natural musical mid for
    // an LFO. 0.05..20 means lane mid = 1 Hz; the slider uses the
    // same range so the slider position and lane curve agree.
    ParameterInfo info = ParameterPresets::frequency(0, name, 0.05f, 20.0f);
    info.teMinValue = 0.05f;
    info.teMaxValue = 20.0f;
    info.defaultValue = 1.0f;
    return info;
}

ParameterInfo makeSyncDivisionInfo(const juce::String& name) {
    // TE's RateType enum is ordered slow → fast (hertz=0, sixteenBars=1, ...,
    // sixtyFourthT=23). MAGDA's slider exposes 16 Bars..1/32T (TE ordinals
    // 1..20) and doesn't expose Hertz as a sync division — Hz is the rate
    // parameter (modParamIndex 0), not a sync setting. The lane therefore
    // drops the TE-ordinal-0 ("Hertz") slot and stores a 0-based display
    // index instead; the manager / playback writeback shift by ±1 at the TE
    // boundary so the lane never resolves "Hertz" at the bottom edge.
    // labelTicks names only the plain divisions to keep the axis legible —
    // dotted / triplet still appear in value→string lookup via `choices`.
    ParameterInfo info;
    info.paramIndex = 1;
    info.name = name;
    info.unit = "";
    info.minValue = 0.0f;
    info.maxValue = 19.0f;
    info.teMinValue = 1.0f;
    info.teMaxValue = 20.0f;
    info.defaultValue = 9.0f;  // quarter (TE ordinal 10 - 1)
    info.scale = ParameterScale::Discrete;
    info.displayFormat = DisplayFormat::Default;
    info.choices = {"16 Bars", "8 Bars", "4 Bars", "2 Bars", "1 Bar", "1/2.", "1/2",
                    "1/2T",    "1/4.",   "1/4",    "1/4T",   "1/8.",  "1/8",  "1/8T",
                    "1/16.",   "1/16",   "1/16T",  "1/32.",  "1/32",  "1/32T"};
    info.labelTicks = {{0.0f, "16 Bars"}, {2.0f, "4 Bars"}, {4.0f, "1 Bar"}, {6.0f, "1/2"},
                       {9.0f, "1/4"},     {12.0f, "1/8"},   {15.0f, "1/16"}, {18.0f, "1/32"}};
    return info;
}

}  // namespace

ParameterInfo getParameterInfoForTarget(const AutomationTarget& target) {
    switch (target.kind) {
        case ControlTarget::Kind::TrackVolume:
            return ParameterPresets::faderVolume(-1, "Volume");

        case ControlTarget::Kind::TrackPan:
            return ParameterPresets::pan(-1, "Pan");

        case ControlTarget::Kind::SendLevel: {
            juce::String name = juce::String("Send ") + juce::String(target.sendBusIndex + 1);
            return ParameterPresets::faderVolume(-1, name);
        }

        case ControlTarget::Kind::PluginParam: {
            // Look up the real ParameterInfo populated by the owning
            // DeviceProcessor so labels/units/ranges come from the actual
            // plugin.
            if (target.paramIndex < 0)
                break;
            auto* device = TrackManager::getInstance().getDeviceInChainByPath(target.devicePath);
            if (!device)
                break;
            if (target.paramIndex >= static_cast<int>(device->parameters.size()))
                break;
            // Backstop: ensure every PluginParam info carries a working
            // DisplayTextProvider so the lane scale labels and tooltips route
            // display through the plugin's own valueToString.
            auto& stored = device->parameters[static_cast<size_t>(target.paramIndex)];
            if (!stored.displayText && target.devicePath.getDeviceId() != INVALID_DEVICE_ID) {
                auto provider = std::make_shared<ParameterInfo::DisplayTextProvider>();
                provider->deviceId = target.devicePath.getDeviceId();
                provider->paramIndex = target.paramIndex;
                stored.displayText = std::move(provider);
            }
            return stored;
        }

        case ControlTarget::Kind::ModParam: {
            // Single "Rate" lane per modifier (modParamIndex == 0). Its scale
            // switches between logarithmic Hz and discrete sync divisions
            // based on the modifier's tempoSync flag.
            if (target.modParamIndex == 0) {
                juce::String name("Rate");
                if (auto* mod = resolveModInfoForTarget(target); mod && mod->tempoSync)
                    return makeSyncDivisionInfo(name);
                return makeHzRateInfo(name);
            }
            break;
        }

        case ControlTarget::Kind::DeviceMacro:
            break;
    }

    // Fallback for unresolved targets.
    return ParameterPresets::percent(-1, getDisplayNameForTarget(target));
}

juce::String getDisplayNameForTarget(const AutomationTarget& target) {
    switch (target.kind) {
        case ControlTarget::Kind::TrackVolume:
            return "Track Volume";
        case ControlTarget::Kind::TrackPan:
            return "Track Pan";
        case ControlTarget::Kind::SendLevel:
            return "Send " + juce::String(target.sendBusIndex + 1);
        case ControlTarget::Kind::PluginParam:
            return "Param " + juce::String(target.paramIndex);
        case ControlTarget::Kind::DeviceMacro:
            return "Macro " + juce::String(target.paramIndex + 1);
        case ControlTarget::Kind::ModParam:
            return "Mod " + juce::String(target.modId) + " Param " +
                   juce::String(target.modParamIndex);
    }
    return "Unknown";
}

}  // namespace magda
