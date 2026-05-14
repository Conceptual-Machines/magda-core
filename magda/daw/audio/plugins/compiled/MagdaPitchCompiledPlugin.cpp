#include "plugins/compiled/MagdaPitchCompiledPlugin.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/ParameterUtils.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_pitch_detuner.generated.cpp"
#include "magda_pitch_harmonizer.generated.cpp"
#include "magda_pitch_shifter.generated.cpp"
#include "plugins/FaustMetadataParser.hpp"
#include "plugins/FaustParamInfo.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaPitchCompiledPlugin::xmlTypeName = "magda_pitch";

namespace {

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

MagdaPitchCompiledPlugin::MagdaPitchCompiledPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    engines_[static_cast<size_t>(PitchEngine::Shifter)].dsp =
        std::make_unique<MagdaPitchShifterDsp>();
    engines_[static_cast<size_t>(PitchEngine::Detuner)].dsp =
        std::make_unique<MagdaPitchDetunerDsp>();
    engines_[static_cast<size_t>(PitchEngine::Harmonizer)].dsp =
        std::make_unique<MagdaPitchHarmonizerDsp>();

    constexpr int kProvisionalSampleRate = 44100;
    rebuildEngineState(kProvisionalSampleRate);
    buildHostParameters();
}

MagdaPitchCompiledPlugin::~MagdaPitchCompiledPlugin() {
    notifyListenersOfDeletion();
    for (auto& p : hostParams_)
        if (p)
            p->detachFromCurrentValue();
}

juce::String MagdaPitchCompiledPlugin::getName() const {
    return "Pitch";
}
juce::String MagdaPitchCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaPitchCompiledPlugin::getShortName(int) {
    return "Pitch";
}
juce::String MagdaPitchCompiledPlugin::getSelectableDescription() {
    return "Pitch";
}

void MagdaPitchCompiledPlugin::rebuildEngineState(int sampleRate) {
    for (auto& e : engines_) {
        if (!e.dsp)
            continue;
        e.dsp->init(sampleRate);
        e.numInputs = e.dsp->getNumInputs();
        e.numOutputs = e.dsp->getNumOutputs();

        EngineHarvester harvester;
        e.dsp->buildUserInterface(&harvester);

        e.zones.fill(nullptr);
        // Slot 0 (Engine) is wrapper-only. Slots 1..5 come from the DSP.
        for (int i = 1; i < kHostSlotCount; ++i) {
            if (auto* c = findByIdx(harvester.harvest, i))
                e.zones[static_cast<size_t>(i)] = c->zone;
        }
    }
}

void MagdaPitchCompiledPlugin::buildHostParameters() {
    hostSlotInfo_[kEngineSlot].name = "Engine";
    hostSlotInfo_[kEngineSlot].scale = magda::ParameterScale::Discrete;
    hostSlotInfo_[kEngineSlot].choices = {"Shifter", "Detuner", "Harmonizer"};
    hostSlotInfo_[kEngineSlot].minValue = 0.0f;
    hostSlotInfo_[kEngineSlot].maxValue =
        static_cast<float>(hostSlotInfo_[kEngineSlot].choices.size() - 1);
    hostSlotInfo_[kEngineSlot].defaultValue = 0.0f;

    hostSlotInfo_[kPitchSlot] = {.name = "Pitch",
                                 .unit = "st",
                                 .scale = magda::ParameterScale::Linear,
                                 .minValue = -24.0f,
                                 .maxValue = 24.0f,
                                 .defaultValue = 0.0f};
    hostSlotInfo_[kFineSlot] = {.name = "Fine",
                                .unit = "cents",
                                .scale = magda::ParameterScale::Linear,
                                .minValue = -100.0f,
                                .maxValue = 100.0f,
                                .defaultValue = 0.0f};
    hostSlotInfo_[kTextureSlot] = {.name = "Texture",
                                   .unit = "ms",
                                   .scale = magda::ParameterScale::Logarithmic,
                                   .minValue = 8.0f,
                                   .maxValue = 200.0f,
                                   .defaultValue = 50.0f,
                                   .scaleAnchor = 50.0f};
    hostSlotInfo_[kMixSlot] = {.name = "Mix",
                               .scale = magda::ParameterScale::Linear,
                               .minValue = 0.0f,
                               .maxValue = 1.0f,
                               .defaultValue = 1.0f};
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
        const juce::String id = "magda_pitch_" + slot.name.toLowerCase().replace(" ", "_");
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

void MagdaPitchCompiledPlugin::initialise(const te::PluginInitialisationInfo& info) {
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

void MagdaPitchCompiledPlugin::deinitialise() {
    scratchIn_.setSize(0, 0);
    scratchOut_.setSize(0, 0);
    inPtrs_.clear();
    outPtrs_.clear();
}

void MagdaPitchCompiledPlugin::reset() {
    for (auto& e : engines_)
        if (e.dsp)
            e.dsp->instanceClear();
}

void MagdaPitchCompiledPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
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

te::AutomatableParameter* MagdaPitchCompiledPlugin::getSlotParameter(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nullptr;
    return hostParams_[static_cast<size_t>(slotIndex)].get();
}

const MagdaPitchCompiledPlugin::HostSlotInfo& MagdaPitchCompiledPlugin::getSlotInfo(
    int slotIndex) const {
    static const HostSlotInfo kEmpty;
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return kEmpty;
    return hostSlotInfo_[static_cast<size_t>(slotIndex)];
}

int MagdaPitchCompiledPlugin::activeEngineIndex() const {
    if (!hostParams_[kEngineSlot])
        return 0;
    const float norm = hostParams_[kEngineSlot]->getCurrentValue();
    return juce::jlimit(0, kEngineCount - 1,
                        static_cast<int>(std::round(norm * static_cast<float>(kEngineCount - 1))));
}

float MagdaPitchCompiledPlugin::displayValueToNativeValue(int slotIndex, float displayValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return displayValue;
    const auto info = parameterInfoForSlot(hostSlotInfo_[static_cast<size_t>(slotIndex)]);
    return magda::ParameterUtils::realToNormalized(displayValue, info);
}

float MagdaPitchCompiledPlugin::nativeValueToDisplayValue(int slotIndex, float nativeValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nativeValue;
    const auto info = parameterInfoForSlot(hostSlotInfo_[static_cast<size_t>(slotIndex)]);
    return magda::ParameterUtils::normalizedToReal(nativeValue, info);
}

constexpr AliasSpec kAliases[] = {
    {"engine", 0, "Engine"},   {"pitch", 1, "Pitch"}, {"fine", 2, "Fine"},
    {"texture", 3, "Texture"}, {"mix", 4, "Mix"},     {"output", 5, "Output"},
};

const CompiledPluginSpec& getMagdaPitchSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaPitchCompiledPlugin::xmlTypeName,
        .displayName = "Pitch",
        .browserCategory = "Pitch",
        .description = "Compiled Faust pitch shifter with three selectable engines.\n"
                       "Shifter: single voice, full plus/minus 24 semitones.\n"
                       "Detuner: two voices hard-panned L/R for chorus-style thickening.\n"
                       "Harmonizer: shifted voice summed with dry at a chosen interval.\n"
                       "All three use ef.transpose; transient smear and grain are by design.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaPitchCompiledPlugin(info);
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
