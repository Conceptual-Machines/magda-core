// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ProjectMigration.hpp"

#include <algorithm>
#include <cmath>

#include "../../core/DrumGridPads.hpp"
#include "../../core/SidechainPort.hpp"
#include "../../core/SourcePool.hpp"
#include "../../core/TechnicalText.hpp"
#include "AudioEventWireNames.hpp"
#include "ProjectSerializer.hpp"
#include "SerializationHelpers.hpp"
#include "music/GrooveLibrary.hpp"

namespace magda::project_migration {

namespace {

juce::DynamicObject::Ptr copyOf(const juce::DynamicObject& source) {
    juce::DynamicObject::Ptr copy = new juce::DynamicObject();
    for (const auto& property : source.getProperties())
        copy->setProperty(property.name, property.value);
    return copy;
}

/** @brief A shallow copy of one object, taken the first time an upgrade writes to it. */
class Edit {
  public:
    explicit Edit(const juce::DynamicObject& source) : source_(source) {}

    void set(const juce::Identifier& key, const juce::var& value) {
        if (copy_ == nullptr)
            copy_ = copyOf(source_);
        copy_->setProperty(key, value);
    }

    bool changed() const noexcept {
        return copy_ != nullptr;
    }

    juce::var result(const juce::var& original) const {
        return changed() ? juce::var(copy_.get()) : original;
    }

  private:
    const juce::DynamicObject& source_;
    juce::DynamicObject::Ptr copy_;
};

ser::ObjectReader finish(const ser::ObjectReader& in, const Edit& edit) {
    return edit.changed() ? in.rebased(edit.result({})) : in;
}

/// A key some older build wrote, read with the conversion that build relied on.
double number(const juce::DynamicObject& obj, const char* key) {
    return static_cast<double>(obj.getProperty(key));
}

/// Moves @p from to @p to when only the old name is there.
void rename(const juce::DynamicObject& obj, Edit& edit, const char* from, const char* to) {
    if (!obj.hasProperty(to) && obj.hasProperty(from))
        edit.set(to, obj.getProperty(from));
}

// -- Audio events saved before provenance (#2674) ----------------------------

/// A stored tempo came from detection, a locked beat count from the user, a non-zero region
/// was drawn by hand, and autoTempo on was a request for beat mode.
Provenance legacyBpmProvenance(double interpBpm) {
    return interpBpm > 0.0 ? Provenance::Analysis : Provenance::None;
}
Provenance legacyBeatsProvenance(double interpTotalBeats, bool locked) {
    if (interpTotalBeats <= 0.0)
        return Provenance::None;
    return locked ? Provenance::User : Provenance::Analysis;
}
RegionExtent legacyLoopExtent(int64_t loopLengthSamples) {
    return loopLengthSamples > 0 ? RegionExtent::Explicit : RegionExtent::WholeSource;
}
PlaybackIntent legacyPlaybackIntent(bool autoTempo) {
    return autoTempo ? PlaybackIntent::Beat : PlaybackIntent::Free;
}

juce::var upgradeAudioEvent(const juce::var& value) {
    const auto* event = value.getDynamicObject();
    if (event == nullptr)
        return value;

    using namespace audio_event_wire;
    Edit edit(*event);
    if (!event->hasProperty("bpmFrom"))
        edit.set("bpmFrom", toString(legacyBpmProvenance(number(*event, "interpBpm"))));
    if (!event->hasProperty("beatsFrom"))
        edit.set("beatsFrom", toString(legacyBeatsProvenance(number(*event, "interpTotalBeats"),
                                                             static_cast<bool>(event->getProperty(
                                                                 "interpTotalBeatsLocked")))));
    if (!event->hasProperty("loopExtent"))
        edit.set("loopExtent",
                 toString(legacyLoopExtent(
                     event->getProperty("loopLengthSamples").toString().getLargeIntValue())));
    // "beatWhenKnown" was written while a clip waited for a tempo.
    const auto intent = event->getProperty("playbackIntent").toString();
    if (intent.isEmpty() || intent == "beatWhenKnown")
        edit.set("playbackIntent", toString(legacyPlaybackIntent(
                                       static_cast<bool>(event->getProperty("autoTempo")))));
    return edit.result(value);
}

// -- v1 audio clips ----------------------------------------------------------

/// Audio interpretation that v1 stored on the CLIP rather than on an event.
struct LegacyAudioFields {
    double fadeIn = 0.0;
    double fadeOut = 0.0;
    int fadeInType = 1;
    int fadeOutType = 1;
    int fadeInBehaviour = 0;
    int fadeOutBehaviour = 0;
    float pitchChange = 0.0f;
    int transpose = 0;
    bool autoPitch = false;
    int autoPitchMode = 0;
    bool reversed = false;
    bool autoDetectBeats = false;
    float beatSensitivity = 0.5f;
    bool leftChannelActive = true;
    bool rightChannelActive = true;
    bool autoTempo = false;
};

/// The v1 "one source per clip" record, in whichever of the three historical layouts it was
/// written.
struct LegacyAudioSource {
    juce::String filePath;
    double durationSeconds = 0.0;
    double interpBpm = 0.0;
    double interpTotalBeats = 0.0;
    bool interpTotalBeatsLocked = false;

    double offsetSeconds = 0.0;
    double offsetBeats = 0.0;
    double loopStartSeconds = 0.0;
    double loopLengthSeconds = 0.0;
    double loopStartBeats = 0.0;
    double loopLengthBeats = 0.0;
    double speedRatio = 1.0;

    bool warpEnabled = false;
    bool analogPitch = false;
    int timeStretchMode = 0;
    std::vector<WarpMarker> warpMarkers;
};

LegacyAudioFields readLegacyAudioFields(const ser::ObjectReader& clip) {
    LegacyAudioFields legacy;
    clip.optional("fadeIn", legacy.fadeIn);
    clip.optional("fadeOut", legacy.fadeOut);
    clip.optional("fadeInType", legacy.fadeInType);
    clip.optional("fadeOutType", legacy.fadeOutType);
    clip.optional("fadeInBehaviour", legacy.fadeInBehaviour);
    clip.optional("fadeOutBehaviour", legacy.fadeOutBehaviour);
    clip.optional("pitchChange", legacy.pitchChange);
    clip.optional("transpose", legacy.transpose);
    clip.optional("autoPitch", legacy.autoPitch);
    clip.optional("autoPitchMode", legacy.autoPitchMode);
    clip.optional("isReversed", legacy.reversed);
    clip.optional("autoDetectBeats", legacy.autoDetectBeats);
    clip.optional("beatSensitivity", legacy.beatSensitivity);
    clip.optional("leftChannelActive", legacy.leftChannelActive);
    clip.optional("rightChannelActive", legacy.rightChannelActive);
    clip.optional("autoTempo", legacy.autoTempo);
    return legacy;
}

/// The warp fields every v1 layout but the flat one carried on its audio record.
void readLegacyWarp(const ser::ObjectReader& record, LegacyAudioSource& v1) {
    record.optional("warpEnabled", v1.warpEnabled);
    record.optional("analogPitch", v1.analogPitch);
    record.optional("timeStretchMode", v1.timeStretchMode);
    record.forEachObject("warpMarkers", [&v1](const ser::ObjectReader& marker) {
        WarpMarker wm;
        marker.required("sourceTime", wm.sourceTime);
        marker.required("warpTime", wm.warpTime);
        v1.warpMarkers.push_back(wm);
    });
}

/**
 * @brief Turn a v1 audio clip into a clip holding exactly one event that spans it.
 *
 * Source-domain values were seconds (or beats, for autoTempo clips) and become samples at
 * the source's own rate. An unreadable file reports a nominal rate and is rescaled on its
 * first successful open, so an offline project still round-trips.
 */
void migrateLegacyAudioClip(const LegacyAudioSource& v1, const LegacyAudioFields& legacy,
                            ClipInfo& outClip, std::vector<Source>* legacySources,
                            double projectTempo) {
    // Duration recorded in the project is a fallback for a file we cannot read.
    double fallbackDuration = v1.durationSeconds;
    if (fallbackDuration <= 0.0 && v1.interpTotalBeats > 0.0 && v1.interpBpm > 0.0)
        fallbackDuration = v1.interpTotalBeats * 60.0 / v1.interpBpm;
    fallbackDuration = juce::jmax(0.0, fallbackDuration);

    AudioEvent event;
    if (legacySources != nullptr) {
        // Staging: no pool writes. The anchors below are computed at the
        // nominal rate and rescaled when install acquires the real file.
        event.sourceId =
            ProjectSerializer::stageLegacySource(*legacySources, v1.filePath, fallbackDuration);
    } else {
        event.sourceId = SourcePool::getInstance().acquire(v1.filePath);
        if (auto* source = SourcePool::getInstance().getMutable(event.sourceId);
            source != nullptr && source->durationSeconds <= 0.0) {
            source->durationSeconds = fallbackDuration;
        }
    }

    event.interpBpm = v1.interpBpm;
    event.interpTotalBeats = v1.interpTotalBeats;
    event.bpmFrom = legacyBpmProvenance(v1.interpBpm);
    event.beatsFrom = legacyBeatsProvenance(v1.interpTotalBeats, v1.interpTotalBeatsLocked);
    event.autoTempo = legacy.autoTempo;
    event.playbackIntent = legacyPlaybackIntent(legacy.autoTempo);
    event.speedRatio = v1.speedRatio > 0.0 ? v1.speedRatio : 1.0;
    event.timeStretchMode = v1.timeStretchMode;
    event.warpEnabled = v1.warpEnabled;
    event.warpMarkers = v1.warpMarkers;
    event.analogPitch = v1.analogPitch;

    // v1 kept beats authoritative under autoTempo and seconds otherwise; both resolve to
    // the same source seconds. A clip saved with autoTempo on and interpBpm still 0 played
    // its beat fields through the project tempo, so that is the tempo used here.
    const double interpretationBpm = v1.interpBpm > 0.0 ? v1.interpBpm : projectTempo;
    const bool beatsAuthoritative = legacy.autoTempo && isValidBpm(interpretationBpm);
    const double secondsPerBeat = beatsAuthoritative ? 60.0 / interpretationBpm : 0.0;
    event.setAnchorSeconds(beatsAuthoritative ? v1.offsetBeats * secondsPerBeat : v1.offsetSeconds);

    event.setLoopStartSeconds(beatsAuthoritative ? v1.loopStartBeats * secondsPerBeat
                                                 : v1.loopStartSeconds);
    event.setLoopLengthSeconds(beatsAuthoritative ? v1.loopLengthBeats * secondsPerBeat
                                                  : v1.loopLengthSeconds);
    // The setter tags Explicit; a zero-length v1 region meant the whole file.
    event.loopExtent = legacyLoopExtent(event.loopLengthSamples);

    event.autoPitch = legacy.autoPitch;
    event.autoPitchMode = legacy.autoPitchMode;
    event.pitchChange = legacy.pitchChange;
    event.transpose = legacy.transpose;
    event.reversed = legacy.reversed;
    event.autoDetectBeats = legacy.autoDetectBeats;
    event.beatSensitivity = legacy.beatSensitivity;
    event.leftChannelActive = legacy.leftChannelActive;
    event.rightChannelActive = legacy.rightChannelActive;

    event.fadeInSeconds = legacy.fadeIn;
    event.fadeOutSeconds = legacy.fadeOut;
    event.fadeInType = legacy.fadeInType;
    event.fadeOutType = legacy.fadeOutType;
    event.fadeInBehaviour = legacy.fadeInBehaviour;
    event.fadeOutBehaviour = legacy.fadeOutBehaviour;

    outClip.audio().addEvent(std::move(event));
    outClip.syncSingleEventToClipBounds();
}

// -- Devices -----------------------------------------------------------------

/// Pre-0.16 the Utility's Width was a 0-2 M/S multiplier; it is 0-200 %. The plugin state
/// stores the normalized knob position, which needs no migration.
void migrateUtilityWidthToPercent(DeviceInfo& device) {
    constexpr int kUtilityWidthSlot = 2;  // MagdaUtilityCompiledPlugin::kWidthSlot
    if (!device.pluginId.equalsIgnoreCase("magda_utility"))
        return;

    for (auto& param : device.parameters) {
        if (param.paramIndex != kUtilityWidthSlot || param.maxValue > 2.001f)
            continue;
        param.currentValue *= 100.0f;
        param.minValue = 0.0f;
        param.maxValue = 200.0f;
        param.defaultValue = 100.0f;
        param.unit = technicalText(TechnicalTextToken::Percent);
    }
}

/// The Utility hides its slot gain fader, its own Gain param being the one gain. A project
/// that set the slot gain folds it into that param, or the trim would apply unseen.
void foldUtilitySlotGainIntoGain(DeviceInfo& device) {
    constexpr int kUtilityGainSlot = 0;  // devices::faust::Utility::kGainSlot
    if (!device.pluginId.equalsIgnoreCase("magda_utility") || std::abs(device.gainDb) < 1.0e-4f)
        return;

    auto* gain = device.findParameterByIndex(kUtilityGainSlot);
    if (gain == nullptr)
        return;
    gain->currentValue =
        std::clamp(gain->currentValue + device.gainDb, gain->minValue, gain->maxValue);
    device.gainDb = 0.0f;
    device.gainValue = 1.0f;
}

}  // namespace

ser::ObjectReader upgradeProjectSettings(const ser::ObjectReader& project) {
    const auto* obj = project.dynamicObject();
    if (obj == nullptr)
        return project;
    const auto* defaults = obj->getProperty("defaults").getDynamicObject();
    if (defaults == nullptr)
        return project;
    const auto* palette = defaults->getProperty("colourPalette").getArray();
    if (palette == nullptr ||
        std::ranges::none_of(*palette, [](const juce::var& item) { return item.isString(); }))
        return project;

    juce::Array<juce::var> entries;
    for (const auto& item : *palette) {
        if (!item.isString()) {
            entries.add(item);
            continue;
        }
        auto* entry = new juce::DynamicObject();
        entry->setProperty("colour", item);
        entries.add(juce::var(entry));
    }
    Edit defaultsEdit(*defaults);
    defaultsEdit.set("colourPalette", entries);
    Edit edit(*obj);
    edit.set("defaults", defaultsEdit.result({}));
    return finish(project, edit);
}

juce::var upgradeAutomation(const juce::var& automation) {
    if (!automation.isArray())
        return automation;
    auto* obj = new juce::DynamicObject();
    obj->setProperty("lanes", automation);
    obj->setProperty("clips", juce::Array<juce::var>());
    return juce::var(obj);
}

ser::ObjectReader upgradeClip(const ser::ObjectReader& clip, double projectTempo) {
    const auto* obj = clip.dynamicObject();
    if (obj == nullptr)
        return clip;
    Edit edit(*obj);

    // Placement before the placement object: start in beats or seconds, length in either.
    if (!obj->hasProperty("placement") &&
        (obj->hasProperty("startBeats") || obj->hasProperty("startTime"))) {
        const double tempo = isValidBpm(projectTempo) ? projectTempo : DEFAULT_BPM;
        const double start = obj->hasProperty("startBeats")
                                 ? number(*obj, "startBeats")
                                 : number(*obj, "startTime") * tempo / 60.0;
        double length = obj->hasProperty("lengthBeats") ? number(*obj, "lengthBeats") : 0.0;
        if (length <= 0.0 && obj->hasProperty("length"))
            length = number(*obj, "length") * tempo / 60.0;
        auto* placement = new juce::DynamicObject();
        placement->setProperty("startBeat", start);
        placement->setProperty("lengthBeats", length);
        edit.set("placement", juce::var(placement));
    }

    rename(*obj, edit, "pianoRollNoteHeight", "midiEditorRowHeight");

    // A clip saved with a groove plays it as the bend the groove spells (#3044), in 4/4:
    // the project's meter is not read yet.
    if (!obj->hasProperty("timeBend")) {
        const auto groove = obj->getProperty("grooveTemplate").toString();
        const auto strength = static_cast<float>(number(*obj, "grooveStrength"));
        if (groove.isNotEmpty())
            if (auto bend = GrooveLibrary::getInstance().bend(groove, 4.0, strength))
                edit.set("timeBend", juce::String(sdk::sequencer::timeBendToText(*bend)));
    }

    if (const auto* audio = obj->getProperty("audio").getDynamicObject()) {
        if (const auto* events = audio->getProperty("events").getArray()) {
            juce::Array<juce::var> upgraded;
            bool changed = false;
            for (const auto& event : *events) {
                upgraded.add(upgradeAudioEvent(event));
                changed |= upgraded.getLast().getDynamicObject() != event.getDynamicObject();
            }
            if (changed) {
                Edit audioEdit(*audio);
                audioEdit.set("events", upgraded);
                edit.set("audio", audioEdit.result({}));
            }
        }
    }

    return finish(clip, edit);
}

bool isLegacyAudioClip(const ser::ObjectReader& clip) {
    const auto audio = clip.get("audio");
    if (audio.isObject())
        return !audio["events"].isArray();
    return clip.get("audioSource").isObject() || clip.has("audioFilePath");
}

void readLegacyAudioClip(const ser::ObjectReader& clip, ClipInfo& outClip, double projectTempo,
                         std::vector<Source>* legacySources) {
    const auto legacy = readLegacyAudioFields(clip);
    LegacyAudioSource v1;

    if (const auto audio = clip.optionalObject("audio")) {
        const auto source = audio->object("source");
        const auto interpretation = audio->object("interpretation");
        const auto playback = audio->object("playback");
        if (!source.isValid() || !interpretation.isValid() || !playback.isValid())
            return;

        source.optional("filePath", v1.filePath);
        source.optional("durationSeconds", v1.durationSeconds);
        interpretation.optional("bpm", v1.interpBpm);
        interpretation.optional("totalBeats", v1.interpTotalBeats);
        interpretation.optional("totalBeatsLocked", v1.interpTotalBeatsLocked);
        playback.optional("offsetSeconds", v1.offsetSeconds);
        playback.optional("offsetBeats", v1.offsetBeats);
        playback.optional("loopStartSeconds", v1.loopStartSeconds);
        playback.optional("loopLengthSeconds", v1.loopLengthSeconds);
        playback.optional("loopStartBeats", v1.loopStartBeats);
        playback.optional("loopLengthBeats", v1.loopLengthBeats);
        playback.optional("speedRatio", v1.speedRatio);
        readLegacyWarp(*audio, v1);
    } else if (const auto record = clip.optionalObject("audioSource")) {
        record->optional("filePath", v1.filePath);
        record->optional("offsetSeconds", v1.offsetSeconds);
        record->optional("offsetBeats", v1.offsetBeats);
        record->optional("loopStartSeconds", v1.loopStartSeconds);
        record->optional("loopLengthSeconds", v1.loopLengthSeconds);
        record->optional("loopStartBeats", v1.loopStartBeats);
        record->optional("loopLengthBeats", v1.loopLengthBeats);
        record->optional("speedRatio", v1.speedRatio);
        record->optional("sourceNumBeats", v1.interpTotalBeats);
        record->optional("sourceBPM", v1.interpBpm);
        readLegacyWarp(*record, v1);
    } else {
        clip.optional("audioFilePath", v1.filePath);
        clip.optional("offset", v1.offsetSeconds);
        clip.optional("offsetBeats", v1.offsetBeats);
        clip.optional("loopStart", v1.loopStartSeconds);
        clip.optional("loopLength", v1.loopLengthSeconds);
        clip.optional("loopStartBeats", v1.loopStartBeats);
        clip.optional("loopLengthBeats", v1.loopLengthBeats);
        clip.optional("speedRatio", v1.speedRatio);
        clip.optional("sourceNumBeats", v1.interpTotalBeats);
        clip.optional("sourceBPM", v1.interpBpm);
    }

    migrateLegacyAudioClip(v1, legacy, outClip, legacySources, projectTempo);
}

ser::ObjectReader upgradeDevice(const ser::ObjectReader& device) {
    const auto* obj = device.dynamicObject();
    if (obj == nullptr)
        return device;
    Edit edit(*obj);

    // Hosted parameter arrays were written in the plugin's normalised domain, internal
    // devices' in display values, before the domain was tagged.
    const bool internal =
        static_cast<int>(obj->getProperty("format")) == static_cast<int>(PluginFormat::Internal);
    const auto convention = static_cast<int>(internal ? ParameterValueConvention::Real
                                                      : ParameterValueConvention::Normalized);

    // Builds before the version was wired to the tag wrote every index as -1: the index was
    // the parameter's position in the array.
    std::vector<int> slotAtPosition;
    if (const auto* params = obj->getProperty("parameters").getArray()) {
        juce::Array<juce::var> upgraded;
        bool changed = false;
        for (int position = 0; position < params->size(); ++position) {
            const auto& item = (*params)[position];
            const auto* param = item.getDynamicObject();
            if (param == nullptr) {
                upgraded.add(item);
                slotAtPosition.push_back(-1);
                continue;
            }
            Edit paramEdit(*param);
            auto slot = static_cast<int>(param->getProperty("paramIndex"));
            if (slot < 0) {
                slot = position;
                paramEdit.set("paramIndex", slot);
            }
            if (!param->hasProperty("valueConvention"))
                paramEdit.set("valueConvention", convention);
            slotAtPosition.push_back(slot);
            upgraded.add(paramEdit.result(item));
            changed |= paramEdit.changed();
        }
        if (changed)
            edit.set("parameters", upgraded);
    }

    // Parameter selections were array positions until they became slots (#2638).
    for (const auto& [slotsKey, positionsKey] :
         {std::pair{"visibleParameterSlots", "visibleParameters"},
          std::pair{"miniMixerParameterSlots", "miniMixerParameters"},
          std::pair{"aiSoundDesignerParameterSlots", "aiSoundDesignerParameters"}}) {
        const auto* positions = obj->getProperty(positionsKey).getArray();
        if (obj->hasProperty(slotsKey) || positions == nullptr)
            continue;
        juce::Array<juce::var> slots;
        for (const auto& value : *positions) {
            const auto position = static_cast<int>(value);
            if (position >= 0 && position < static_cast<int>(slotAtPosition.size()) &&
                slotAtPosition[static_cast<std::size_t>(position)] >= 0)
                slots.add(slotAtPosition[static_cast<std::size_t>(position)]);
        }
        edit.set(slotsKey, slots);
    }

    // Before the declared port, the only fact saved was that the device had one (#2329).
    if (!obj->hasProperty("sidechainPortKind") &&
        static_cast<bool>(obj->getProperty("canSidechain"))) {
        edit.set("sidechainPortKind", static_cast<int>(monoAudioSidechain.kind));
        edit.set("sidechainPortChannels", monoAudioSidechain.channels);
    }

    return finish(device, edit);
}

void migrateDevice(DeviceInfo& device) {
    migrateUtilityWidthToPercent(device);
    foldUtilitySlotGainIntoGain(device);
    // A project saved before pads moved into the model (#2207) still carries them inside
    // `pluginState`; one saved since is taken at its word, even with every pad deleted.
    if (!device.pads)
        migrateLegacyPads(device);
}

void liftLegacyTarget(const ser::ObjectReader& macro, std::vector<MacroLink>& links) {
    if (!links.empty())
        return;
    const auto target = macro.optionalObject("target");
    if (!target)
        return;
    ControlTarget legacy;
    ser::readControlTarget(*target, legacy);
    if (!legacy.isValid())
        return;
    MacroLink link;
    link.target = std::move(legacy);
    links.push_back(link);
}

void liftLegacyTarget(const ser::ObjectReader& mod, std::vector<ModLink>& links) {
    if (!links.empty())
        return;
    const auto target = mod.optionalObject("target");
    if (!target)
        return;
    ControlTarget legacy;
    ser::readControlTarget(*target, legacy);
    if (!legacy.isValid())
        return;
    ModLink link;
    link.target = std::move(legacy);
    link.amount = 0.0f;
    mod.optional("amount", link.amount);
    links.push_back(link);
}

ser::ObjectReader upgradeAutomationLane(const ser::ObjectReader& lane) {
    const auto* obj = lane.dynamicObject();
    if (obj == nullptr)
        return lane;
    Edit edit(*obj);
    rename(*obj, edit, "snapToBeatGrid", "snapEditsToBeatGrid");
    rename(*obj, edit, "snapTime", "snapEditsToBeatGrid");
    return finish(lane, edit);
}

ser::ObjectReader upgradeAutomationClip(const ser::ObjectReader& clip) {
    const auto* obj = clip.dynamicObject();
    if (obj == nullptr)
        return clip;
    Edit edit(*obj);
    rename(*obj, edit, "startTime", "startBeats");
    rename(*obj, edit, "length", "lengthBeats");
    rename(*obj, edit, "loopLength", "loopLengthBeats");
    return finish(clip, edit);
}

ser::ObjectReader upgradeAutomationPoint(const ser::ObjectReader& point) {
    const auto* obj = point.dynamicObject();
    if (obj == nullptr)
        return point;
    Edit edit(*obj);
    rename(*obj, edit, "time", "beatPosition");
    for (const char* key : {"inHandle", "outHandle"}) {
        const auto handle = obj->getProperty(key);
        const auto* handleObj = handle.getDynamicObject();
        if (handleObj == nullptr)
            continue;
        Edit handleEdit(*handleObj);
        rename(*handleObj, handleEdit, "time", "beatOffset");
        if (handleEdit.changed())
            edit.set(key, handleEdit.result(handle));
    }
    return finish(point, edit);
}

ser::ObjectReader upgradeAutomationTarget(const ser::ObjectReader& target) {
    const auto* obj = target.dynamicObject();
    if (obj == nullptr)
        return target;
    Edit edit(*obj);

    // The track id lived on the target before the path carried it.
    const auto path = obj->getProperty("devicePath");
    if (const auto* pathObj = path.getDynamicObject();
        pathObj != nullptr && obj->hasProperty("trackId") &&
        static_cast<int>(pathObj->getProperty("trackId")) == INVALID_TRACK_ID) {
        Edit pathEdit(*pathObj);
        pathEdit.set("trackId", obj->getProperty("trackId"));
        edit.set("devicePath", pathEdit.result(path));
    }

    const auto kind = static_cast<ControlTarget::Kind>(static_cast<int>(obj->getProperty("type")));
    if (kind == ControlTarget::Kind::DeviceMacro && obj->hasProperty("macroIndex"))
        edit.set("paramIndex", obj->getProperty("macroIndex"));
    // The separate sync-division lane at mod parameter 1 is the Rate lane at 0 now.
    if (kind == ControlTarget::Kind::ModParam &&
        static_cast<int>(obj->getProperty("modParamIndex")) == 1)
        edit.set("modParamIndex", 0);

    return finish(target, edit);
}

}  // namespace magda::project_migration
