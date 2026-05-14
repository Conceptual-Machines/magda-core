#include "plugins/compiled/MagdaReverbCompiledPlugin.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/ParameterUtils.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "plugins/FaustMetadataParser.hpp"
#include "plugins/FaustParamInfo.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

// All three engine DSPs are #included into THIS translation unit only —
// each generated `.cpp` defines a self-contained class, so co-locating
// them lets us instantiate one of each without conflicts. Mirrors the
// MagdaFilter / MagdaCompressor wrappers.
#include "magda_reverb_hall.generated.cpp"
#include "magda_reverb_plate.generated.cpp"
#include "magda_reverb_room.generated.cpp"

namespace magda::daw::audio::compiled {

const char* MagdaReverbCompiledPlugin::xmlTypeName = "magda_reverb";

namespace {

// ============================================================================
// EngineHarvester — stripped-down UI that records each control's idx,
// kind, and zone pointer. Same shape the other multi-engine wrappers use.
// ============================================================================

struct EngineHarvest {
    struct Control {
        int idx = -1;
        FaustParamSlot::Kind kind = FaustParamSlot::Kind::Continuous;
        FAUSTFLOAT* zone = nullptr;
    };
    std::vector<Control> controls;
};

class EngineHarvester : public ::UI {
  public:
    EngineHarvest harvest;

    void openTabBox(const char*) override {}
    void openHorizontalBox(const char*) override {}
    void openVerticalBox(const char*) override {}
    void closeBox() override {}

    void addButton(const char* label, FAUSTFLOAT* zone) override {
        emitControl(FaustParamSlot::Kind::Boolean, label, zone);
    }
    void addCheckButton(const char* label, FAUSTFLOAT* zone) override {
        emitControl(FaustParamSlot::Kind::Boolean, label, zone);
    }
    void addVerticalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT,
                           FAUSTFLOAT) override {
        emitControl(FaustParamSlot::Kind::Continuous, label, zone);
    }
    void addHorizontalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT, FAUSTFLOAT,
                             FAUSTFLOAT, FAUSTFLOAT) override {
        emitControl(FaustParamSlot::Kind::Continuous, label, zone);
    }
    void addNumEntry(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT,
                     FAUSTFLOAT) override {
        emitControl(FaustParamSlot::Kind::Continuous, label, zone);
    }
    void addHorizontalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addVerticalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addSoundfile(const char*, const char*, Soundfile**) override {}

    void declare(FAUSTFLOAT* zone, const char* key, const char* value) override {
        if (zone == nullptr)
            return;
        const auto k = juce::String::fromUTF8(key != nullptr ? key : "").toLowerCase();
        const auto v = juce::String::fromUTF8(value != nullptr ? value : "");
        applyFaustAnnotation(k, v, pendingByZone_[zone]);
    }

  private:
    void emitControl(FaustParamSlot::Kind kind, const char* rawLabel, FAUSTFLOAT* zone) {
        const auto parsed =
            parseFaustLabel(juce::String::fromUTF8(rawLabel != nullptr ? rawLabel : ""));
        ControlMetadata merged = parsed.metadata;
        if (zone != nullptr) {
            if (auto it = pendingByZone_.find(zone); it != pendingByZone_.end()) {
                mergeFaustMetadata(merged, it->second);
                pendingByZone_.erase(it);
            }
        }

        EngineHarvest::Control c;
        c.idx = merged.slotIndex;
        c.kind = merged.isMenuStyle ? FaustParamSlot::Kind::Discrete : kind;
        c.zone = zone;
        harvest.controls.push_back(std::move(c));
    }

    std::map<FAUSTFLOAT*, ControlMetadata> pendingByZone_;
};

const EngineHarvest::Control* findByIdx(const EngineHarvest& h, int idx) {
    for (const auto& c : h.controls)
        if (c.idx == idx)
            return &c;
    return nullptr;
}

magda::ParameterInfo parameterInfoForSlot(const CompiledHostSlotInfo& s) {
    magda::ParameterInfo info;
    info.minValue = s.minValue;
    info.maxValue = s.maxValue;
    info.defaultValue = s.defaultValue;
    info.unit = s.unit;
    info.scale = s.scale;
    if (std::isfinite(s.scaleAnchor))
        info.scaleAnchor = s.scaleAnchor;
    info.choices = s.choices;
    return info;
}

}  // namespace

MagdaReverbCompiledPlugin::MagdaReverbCompiledPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    engines_[static_cast<size_t>(ReverbEngine::Plate)].dsp =
        std::make_unique<MagdaReverbPlateDsp>();
    engines_[static_cast<size_t>(ReverbEngine::Hall)].dsp = std::make_unique<MagdaReverbHallDsp>();
    engines_[static_cast<size_t>(ReverbEngine::Room)].dsp = std::make_unique<MagdaReverbRoomDsp>();

    constexpr int kProvisionalSampleRate = 44100;
    rebuildEngineState(kProvisionalSampleRate);
    buildHostParameters();
}

MagdaReverbCompiledPlugin::~MagdaReverbCompiledPlugin() {
    notifyListenersOfDeletion();
    for (auto& p : hostParams_)
        if (p)
            p->detachFromCurrentValue();
}

juce::String MagdaReverbCompiledPlugin::getName() const {
    return "Reverb";
}
juce::String MagdaReverbCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaReverbCompiledPlugin::getShortName(int) {
    return "Reverb";
}
juce::String MagdaReverbCompiledPlugin::getSelectableDescription() {
    return "Reverb";
}

void MagdaReverbCompiledPlugin::rebuildEngineState(int sampleRate) {
    for (auto& e : engines_) {
        if (!e.dsp)
            continue;
        e.dsp->init(sampleRate);
        e.numInputs = e.dsp->getNumInputs();
        e.numOutputs = e.dsp->getNumOutputs();

        EngineHarvester harvester;
        e.dsp->buildUserInterface(&harvester);

        e.zones.fill(nullptr);

        // Slot 0 (Engine) lives only in the wrapper — no DSP zone. All
        // other slots are looked up by their `[idx:N]` annotation, with
        // N == host slot index. Every engine exposes every slot 1..8.
        for (int i = 1; i < kHostSlotCount; ++i) {
            if (auto* c = findByIdx(harvester.harvest, i))
                e.zones[static_cast<size_t>(i)] = c->zone;
        }
    }
}

void MagdaReverbCompiledPlugin::buildHostParameters() {
    hostSlotInfo_[kEngineSlot].name = "Engine";
    hostSlotInfo_[kEngineSlot].scale = magda::ParameterScale::Discrete;
    hostSlotInfo_[kEngineSlot].choices = {"Plate", "Hall", "Room"};
    hostSlotInfo_[kEngineSlot].minValue = 0.0f;
    hostSlotInfo_[kEngineSlot].maxValue =
        static_cast<float>(hostSlotInfo_[kEngineSlot].choices.size() - 1);
    hostSlotInfo_[kEngineSlot].defaultValue = 0.0f;

    hostSlotInfo_[kMixSlot] = {.name = "Mix",
                               .scale = magda::ParameterScale::Linear,
                               .minValue = 0.0f,
                               .maxValue = 1.0f,
                               .defaultValue = 0.3f};
    hostSlotInfo_[kPredelaySlot] = {.name = "Predelay",
                                    .unit = "ms",
                                    .scale = magda::ParameterScale::Linear,
                                    .minValue = 0.0f,
                                    .maxValue = 250.0f,
                                    .defaultValue = 20.0f};
    hostSlotInfo_[kDecaySlot] = {.name = "Decay",
                                 .scale = magda::ParameterScale::Linear,
                                 .minValue = 0.0f,
                                 .maxValue = 100.0f,
                                 .defaultValue = 50.0f};
    hostSlotInfo_[kDampingSlot] = {.name = "Damping",
                                   .scale = magda::ParameterScale::Linear,
                                   .minValue = 0.0f,
                                   .maxValue = 100.0f,
                                   .defaultValue = 30.0f};
    hostSlotInfo_[kLowCutSlot] = {.name = "Low Cut",
                                  .unit = "Hz",
                                  .scale = magda::ParameterScale::Logarithmic,
                                  .minValue = 20.0f,
                                  .maxValue = 500.0f,
                                  .defaultValue = 40.0f,
                                  .scaleAnchor = 80.0f};
    hostSlotInfo_[kHighCutSlot] = {.name = "High Cut",
                                   .unit = "Hz",
                                   .scale = magda::ParameterScale::Logarithmic,
                                   .minValue = 1000.0f,
                                   .maxValue = 18000.0f,
                                   .defaultValue = 12000.0f,
                                   .scaleAnchor = 8000.0f};
    hostSlotInfo_[kWidthSlot] = {.name = "Width",
                                 .scale = magda::ParameterScale::Linear,
                                 .minValue = 0.0f,
                                 .maxValue = 200.0f,
                                 .defaultValue = 100.0f};
    hostSlotInfo_[kOutputSlot] = {.name = "Output",
                                  .unit = "dB",
                                  .scale = magda::ParameterScale::Linear,
                                  .minValue = -24.0f,
                                  .maxValue = 12.0f,
                                  .defaultValue = 0.0f};

    juce::NormalisableRange<float> normalisedRange{0.0f, 1.0f};
    auto* undoManager = getUndoManager();

    for (int i = 0; i < kHostSlotCount; ++i) {
        const auto& slot = hostSlotInfo_[i];
        const juce::String id = "magda_reverb_" + slot.name.toLowerCase().replace(" ", "_");
        const juce::Identifier identifier(id);
        const auto info = parameterInfoForSlot(slot);
        const float defaultNormalized =
            magda::ParameterUtils::realToNormalized(slot.defaultValue, info);
        hostCached_[i].referTo(state, identifier, undoManager, defaultNormalized);

        auto param = addParam(
            id, slot.name, normalisedRange,
            [info](float normalized) {
                const float real = magda::ParameterUtils::normalizedToReal(normalized, info);
                return magda::ParameterUtils::formatValue(real, info);
            },
            [info](const juce::String& text) {
                auto parsed = magda::ParameterUtils::parseValue(text, info);
                if (parsed)
                    return magda::ParameterUtils::realToNormalized(*parsed, info);
                return 0.0f;
            });
        param->attachToCurrentValue(hostCached_[i]);
        hostParams_[i] = param;
    }
}

void MagdaReverbCompiledPlugin::initialise(const te::PluginInitialisationInfo& info) {
    rebuildEngineState(static_cast<int>(info.sampleRate));

    int maxIn = 0, maxOut = 0;
    for (const auto& e : engines_) {
        maxIn = std::max(maxIn, e.numInputs);
        maxOut = std::max(maxOut, e.numOutputs);
    }
    scratchIn_.setSize(maxIn, info.blockSizeSamples, false, true, true);
    scratchOut_.setSize(maxOut, info.blockSizeSamples, false, true, true);
    inPtrs_.assign(static_cast<size_t>(maxIn), nullptr);
    outPtrs_.assign(static_cast<size_t>(maxOut), nullptr);
}

void MagdaReverbCompiledPlugin::deinitialise() {
    scratchIn_.setSize(0, 0);
    scratchOut_.setSize(0, 0);
    inPtrs_.clear();
    outPtrs_.clear();
}

void MagdaReverbCompiledPlugin::reset() {
    for (auto& e : engines_)
        if (e.dsp)
            e.dsp->instanceClear();
}

void MagdaReverbCompiledPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.destBuffer || fc.bufferNumSamples <= 0)
        return;

    auto realForSlot = [&](int slot) -> float {
        const auto& s = hostSlotInfo_[static_cast<size_t>(slot)];
        const auto info = parameterInfoForSlot(s);
        const float norm = hostParams_[static_cast<size_t>(slot)]->getCurrentValue();
        return magda::ParameterUtils::normalizedToReal(norm, info);
    };

    const float engineNorm = hostParams_[kEngineSlot]->getCurrentValue();
    const int engineIndex = juce::jlimit(
        0, kEngineCount - 1,
        static_cast<int>(std::round(engineNorm * static_cast<float>(kEngineCount - 1))));
    activeEngine_.store(engineIndex);

    // Write shared zones into ALL engines so an engine swap preserves the
    // user's settings on the first sample of the new engine.
    for (int slot = 1; slot < kHostSlotCount; ++slot) {
        const float real = realForSlot(slot);
        for (auto& e : engines_) {
            if (auto* zone = e.zones[static_cast<size_t>(slot)])
                *zone = static_cast<FAUSTFLOAT>(real);
        }
    }

    auto& active = engines_[static_cast<size_t>(engineIndex)];
    if (!active.dsp)
        return;

    const int numSamples = fc.bufferNumSamples;
    const int startSample = fc.bufferStartSample;
    const int hostChannels = fc.destBuffer->getNumChannels();
    const int numInputs = active.numInputs;
    const int numOutputs = active.numOutputs;
    if (hostChannels <= 0 || numInputs <= 0 || numOutputs <= 0)
        return;

    if (scratchIn_.getNumChannels() < numInputs || scratchIn_.getNumSamples() < numSamples)
        scratchIn_.setSize(numInputs, numSamples, false, true, true);
    if (scratchOut_.getNumChannels() < numOutputs || scratchOut_.getNumSamples() < numSamples)
        scratchOut_.setSize(numOutputs, numSamples, false, true, true);
    if (static_cast<int>(inPtrs_.size()) < numInputs)
        inPtrs_.resize(static_cast<size_t>(numInputs), nullptr);
    if (static_cast<int>(outPtrs_.size()) < numOutputs)
        outPtrs_.resize(static_cast<size_t>(numOutputs), nullptr);

    for (int ch = 0; ch < numInputs; ++ch) {
        float* dst = scratchIn_.getWritePointer(ch);
        if (ch < hostChannels) {
            const float* src = fc.destBuffer->getReadPointer(ch, startSample);
            std::copy(src, src + numSamples, dst);
        } else {
            std::fill(dst, dst + numSamples, 0.0f);
        }
        inPtrs_[static_cast<size_t>(ch)] = dst;
    }
    for (int ch = 0; ch < numOutputs; ++ch) {
        outPtrs_[static_cast<size_t>(ch)] = (ch < hostChannels)
                                                ? fc.destBuffer->getWritePointer(ch, startSample)
                                                : scratchOut_.getWritePointer(ch);
    }

    active.dsp->compute(numSamples, inPtrs_.data(), outPtrs_.data());

    // Sanitise output — clamp and replace non-finite samples. Reverb
    // networks can occasionally diverge if Decay is automated past safe
    // limits or if a denormal slips through; the clamp is a safety net.
    const int channelsToSanitise = std::min(hostChannels, numOutputs);
    for (int ch = 0; ch < channelsToSanitise; ++ch) {
        float* out = fc.destBuffer->getWritePointer(ch, startSample);
        for (int i = 0; i < numSamples; ++i) {
            const float sample = out[i];
            out[i] = std::isfinite(sample) ? juce::jlimit(-16.0f, 16.0f, sample) : 0.0f;
        }
    }
    for (int ch = numOutputs; ch < hostChannels; ++ch)
        fc.destBuffer->clear(ch, startSample, numSamples);
}

te::AutomatableParameter* MagdaReverbCompiledPlugin::getSlotParameter(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nullptr;
    return hostParams_[static_cast<size_t>(slotIndex)].get();
}

const MagdaReverbCompiledPlugin::HostSlotInfo& MagdaReverbCompiledPlugin::getSlotInfo(
    int slotIndex) const {
    static const HostSlotInfo kEmpty;
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return kEmpty;
    return hostSlotInfo_[static_cast<size_t>(slotIndex)];
}

int MagdaReverbCompiledPlugin::activeEngineIndex() const {
    return activeEngine_.load(std::memory_order_relaxed);
}

float MagdaReverbCompiledPlugin::displayValueToNativeValue(int slotIndex,
                                                           float displayValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return displayValue;
    const auto info = parameterInfoForSlot(hostSlotInfo_[static_cast<size_t>(slotIndex)]);
    return magda::ParameterUtils::realToNormalized(displayValue, info);
}

float MagdaReverbCompiledPlugin::nativeValueToDisplayValue(int slotIndex, float nativeValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nativeValue;
    const auto info = parameterInfoForSlot(hostSlotInfo_[static_cast<size_t>(slotIndex)]);
    return magda::ParameterUtils::normalizedToReal(nativeValue, info);
}

constexpr AliasSpec kAliases[] = {
    {"engine", 0, "Engine"},     {"mix", 1, "Mix"},         {"predelay", 2, "Predelay"},
    {"decay", 3, "Decay"},       {"damping", 4, "Damping"}, {"low_cut", 5, "Low Cut"},
    {"high_cut", 6, "High Cut"}, {"width", 7, "Width"},     {"output", 8, "Output"},
};

const CompiledPluginSpec& getMagdaReverbSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaReverbCompiledPlugin::xmlTypeName,
        .displayName = "Reverb",
        .browserCategory = "Reverb",
        .description = "Compiled Faust reverb with three selectable engines.\n"
                       "Plate: Dattorro diffusion network for studio-plate ambience.\n"
                       "Hall: Zita 8-tap FDN for smooth large-space tails.\n"
                       "Room: Freeverb Schroeder/Moorer network for small-space ambience.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaReverbCompiledPlugin(info);
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
