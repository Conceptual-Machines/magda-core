#include "plugins/compiled/MagdaCompressorCompiledPlugin.hpp"

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

// Both engine DSPs are #included into THIS translation unit only — each
// generated `.cpp` defines a self-contained class, so co-locating them lets
// us instantiate both without conflicts. Mirrors the MagdaFilter wrapper.
#include "magda_compressor.generated.cpp"
#include "magda_compressor_glue.generated.cpp"

namespace magda::daw::audio::compiled {

const char* MagdaCompressorCompiledPlugin::xmlTypeName = "magda_compressor";

namespace {

// ============================================================================
// EngineHarvester — stripped-down UI that records each control's idx, kind,
// and zone pointer. Same shape the MagdaFilter wrapper uses.
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

float ampToDb(float amp) {
    return 20.0f * std::log10(std::max(amp, 1.0e-6f));
}

// Static transfer-curve gain reduction for the curve view. Both engines
// respond similarly to threshold/ratio/knee at steady state, so a single
// formula is good enough for the visualisation.
float gainReductionForLevel(float levelDb, float thresholdDb, float ratio, float kneeDb) {
    ratio = std::max(1.0f, ratio);
    kneeDb = std::max(0.0f, kneeDb);

    const float over = levelDb - thresholdDb;
    float compressedOver = over;
    if (kneeDb > 0.0f) {
        const float halfKnee = kneeDb * 0.5f;
        if (over <= -halfKnee) {
            compressedOver = over;
        } else if (over >= halfKnee) {
            compressedOver = over / ratio;
        } else {
            const float x = over + halfKnee;
            compressedOver = over + (1.0f / ratio - 1.0f) * x * x / (2.0f * kneeDb);
        }
    } else if (over > 0.0f) {
        compressedOver = over / ratio;
    }

    return std::max(0.0f, over - compressedOver);
}

}  // namespace

MagdaCompressorCompiledPlugin::MagdaCompressorCompiledPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    engines_[static_cast<size_t>(CompressorEngine::Clean)].dsp =
        std::make_unique<MagdaCompressorDsp>();
    engines_[static_cast<size_t>(CompressorEngine::Glue)].dsp =
        std::make_unique<MagdaCompressorGlueDsp>();

    constexpr int kProvisionalSampleRate = 44100;
    rebuildEngineState(kProvisionalSampleRate);
    buildHostParameters();
}

MagdaCompressorCompiledPlugin::~MagdaCompressorCompiledPlugin() {
    notifyListenersOfDeletion();
    for (auto& p : hostParams_)
        if (p)
            p->detachFromCurrentValue();
}

juce::String MagdaCompressorCompiledPlugin::getName() const {
    return "Compressor";
}
juce::String MagdaCompressorCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaCompressorCompiledPlugin::getShortName(int) {
    return "Comp";
}
juce::String MagdaCompressorCompiledPlugin::getSelectableDescription() {
    return "Compressor";
}

void MagdaCompressorCompiledPlugin::getChannelNames(juce::StringArray* ins,
                                                    juce::StringArray* outs) {
    if (ins)
        ins->addArray({"Left", "Right", "Sidechain"});
    if (outs)
        outs->addArray({"Left", "Right"});
}

void MagdaCompressorCompiledPlugin::rebuildEngineState(int sampleRate) {
    for (size_t engineIdx = 0; engineIdx < engines_.size(); ++engineIdx) {
        auto& e = engines_[engineIdx];
        if (!e.dsp)
            continue;
        e.dsp->init(sampleRate);
        e.numInputs = e.dsp->getNumInputs();
        e.numOutputs = e.dsp->getNumOutputs();

        EngineHarvester harvester;
        e.dsp->buildUserInterface(&harvester);

        e.zones.fill(nullptr);
        e.useSidechainZone = nullptr;

        // Slot 0 (Engine) lives only in the wrapper — no DSP zone. All other
        // slots are looked up by their `[idx:N]` annotation, with N == host
        // slot index. Engines that don't expose a given slot simply yield
        // nullptr, and the audio path skips writing it.
        for (int i = 1; i < kHostSlotCount; ++i) {
            if (auto* c = findByIdx(harvester.harvest, i))
                e.zones[static_cast<size_t>(i)] = c->zone;
        }
        if (auto* c = findByIdx(harvester.harvest, kUseSidechainHiddenSlot))
            e.useSidechainZone = c->zone;
    }
}

void MagdaCompressorCompiledPlugin::buildHostParameters() {
    hostSlotInfo_[kEngineSlot].name = "Engine";
    hostSlotInfo_[kEngineSlot].scale = magda::ParameterScale::Discrete;
    hostSlotInfo_[kEngineSlot].choices = {"Clean", "Glue"};
    hostSlotInfo_[kEngineSlot].minValue = 0.0f;
    hostSlotInfo_[kEngineSlot].maxValue =
        static_cast<float>(hostSlotInfo_[kEngineSlot].choices.size() - 1);
    hostSlotInfo_[kEngineSlot].defaultValue = 0.0f;

    hostSlotInfo_[kThresholdSlot] = {.name = "Threshold",
                                     .unit = "dB",
                                     .scale = magda::ParameterScale::Linear,
                                     .minValue = -60.0f,
                                     .maxValue = 0.0f,
                                     .defaultValue = -18.0f};
    hostSlotInfo_[kRatioSlot] = {.name = "Ratio",
                                 .scale = magda::ParameterScale::Logarithmic,
                                 .minValue = 1.0f,
                                 .maxValue = 50.0f,
                                 .defaultValue = 4.0f,
                                 .scaleAnchor = 4.0f};
    hostSlotInfo_[kAttackSlot] = {.name = "Attack",
                                  .unit = "ms",
                                  .scale = magda::ParameterScale::Logarithmic,
                                  .minValue = 0.1f,
                                  .maxValue = 200.0f,
                                  .defaultValue = 10.0f,
                                  .scaleAnchor = 10.0f};
    hostSlotInfo_[kReleaseSlot] = {.name = "Release",
                                   .unit = "ms",
                                   .scale = magda::ParameterScale::Logarithmic,
                                   .minValue = 5.0f,
                                   .maxValue = 1000.0f,
                                   .defaultValue = 120.0f,
                                   .scaleAnchor = 100.0f};
    hostSlotInfo_[kKneeSlot] = {.name = "Knee",
                                .unit = "dB",
                                .scale = magda::ParameterScale::Linear,
                                .minValue = 0.0f,
                                .maxValue = 24.0f,
                                .defaultValue = 6.0f};
    hostSlotInfo_[kMakeupSlot] = {.name = "Makeup",
                                  .unit = "dB",
                                  .scale = magda::ParameterScale::Linear,
                                  .minValue = 0.0f,
                                  .maxValue = 24.0f,
                                  .defaultValue = 0.0f};
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

    hostSlotInfo_[kDetectorSlot].name = "Detector";
    hostSlotInfo_[kDetectorSlot].scale = magda::ParameterScale::Discrete;
    hostSlotInfo_[kDetectorSlot].choices = {"Peak", "RMS"};
    hostSlotInfo_[kDetectorSlot].minValue = 0.0f;
    hostSlotInfo_[kDetectorSlot].maxValue = 1.0f;
    hostSlotInfo_[kDetectorSlot].defaultValue = 0.0f;

    hostSlotInfo_[kLinkSlot] = {.name = "Link",
                                .scale = magda::ParameterScale::Linear,
                                .minValue = 0.0f,
                                .maxValue = 1.0f,
                                .defaultValue = 1.0f};
    hostSlotInfo_[kSidechainHpfSlot] = {.name = "SC HPF",
                                        .unit = "Hz",
                                        .scale = magda::ParameterScale::Logarithmic,
                                        .minValue = 20.0f,
                                        .maxValue = 500.0f,
                                        .defaultValue = 20.0f,
                                        .scaleAnchor = 120.0f};
    hostSlotInfo_[kFbffSlot] = {.name = "FBFF",
                                .scale = magda::ParameterScale::Linear,
                                .minValue = 0.0f,
                                .maxValue = 1.0f,
                                .defaultValue = 0.5f};
    hostSlotInfo_[kStyleSlot].name = "Style";
    hostSlotInfo_[kStyleSlot].scale = magda::ParameterScale::Discrete;
    hostSlotInfo_[kStyleSlot].choices = {"Pre", "Post"};
    hostSlotInfo_[kStyleSlot].minValue = 0.0f;
    hostSlotInfo_[kStyleSlot].maxValue = 1.0f;
    hostSlotInfo_[kStyleSlot].defaultValue = 0.0f;
    hostSlotInfo_[kAutogainSlot].name = "Autogain";
    hostSlotInfo_[kAutogainSlot].scale = magda::ParameterScale::Discrete;
    hostSlotInfo_[kAutogainSlot].choices = {"Off", "On"};
    hostSlotInfo_[kAutogainSlot].minValue = 0.0f;
    hostSlotInfo_[kAutogainSlot].maxValue = 1.0f;
    hostSlotInfo_[kAutogainSlot].defaultValue = 0.0f;

    juce::NormalisableRange<float> normalisedRange{0.0f, 1.0f};
    auto* undoManager = getUndoManager();

    for (int i = 0; i < kHostSlotCount; ++i) {
        const auto& slot = hostSlotInfo_[i];
        const juce::String id = "magda_compressor_" + slot.name.toLowerCase().replace(" ", "_");
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

void MagdaCompressorCompiledPlugin::initialise(const te::PluginInitialisationInfo& info) {
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

void MagdaCompressorCompiledPlugin::deinitialise() {
    scratchIn_.setSize(0, 0);
    scratchOut_.setSize(0, 0);
    inPtrs_.clear();
    outPtrs_.clear();
}

void MagdaCompressorCompiledPlugin::reset() {
    for (auto& e : engines_)
        if (e.dsp)
            e.dsp->instanceClear();
}

void MagdaCompressorCompiledPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.destBuffer || fc.bufferNumSamples <= 0)
        return;

    // Read all slot values up front (normalized 0..1, denormalized once).
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

    // Write shared zones into BOTH engines so an engine swap preserves the
    // user's settings on first sample of the new engine.
    for (int slot = 1; slot < kHostSlotCount; ++slot) {
        const float real = realForSlot(slot);
        for (auto& e : engines_) {
            if (auto* zone = e.zones[static_cast<size_t>(slot)])
                *zone = static_cast<FAUSTFLOAT>(real);
        }
    }

    auto& active = engines_[static_cast<size_t>(engineIndex)];
    if (active.useSidechainZone != nullptr)
        *active.useSidechainZone = getSidechainSourceID().isValid() ? FAUSTFLOAT(1) : FAUSTFLOAT(0);

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

    float inputPeak = 0.0f;
    float keyPeak = 0.0f;
    const bool externalSidechain = getSidechainSourceID().isValid();

    for (int ch = 0; ch < numInputs; ++ch) {
        float* dst = scratchIn_.getWritePointer(ch);
        if (ch < hostChannels) {
            const float* src = fc.destBuffer->getReadPointer(ch, startSample);
            std::copy(src, src + numSamples, dst);
        } else {
            std::fill(dst, dst + numSamples, 0.0f);
        }
        inPtrs_[static_cast<size_t>(ch)] = dst;

        if (ch < 2) {
            for (int i = 0; i < numSamples; ++i)
                inputPeak = std::max(inputPeak, std::fabs(dst[i]));
        }
        if ((externalSidechain && ch == 2) || (!externalSidechain && ch < 2)) {
            for (int i = 0; i < numSamples; ++i)
                keyPeak = std::max(keyPeak, std::fabs(dst[i]));
        }
    }
    for (int ch = 0; ch < numOutputs; ++ch) {
        outPtrs_[static_cast<size_t>(ch)] = (ch < hostChannels)
                                                ? fc.destBuffer->getWritePointer(ch, startSample)
                                                : scratchOut_.getWritePointer(ch);
    }

    active.dsp->compute(numSamples, inPtrs_.data(), outPtrs_.data());

    float outputPeak = 0.0f;
    const int channelsToSanitise = std::min(hostChannels, numOutputs);
    for (int ch = 0; ch < channelsToSanitise; ++ch) {
        float* out = fc.destBuffer->getWritePointer(ch, startSample);
        for (int i = 0; i < numSamples; ++i) {
            const float sample = out[i];
            out[i] = std::isfinite(sample) ? juce::jlimit(-16.0f, 16.0f, sample) : 0.0f;
            outputPeak = std::max(outputPeak, std::fabs(out[i]));
        }
    }
    for (int ch = numOutputs; ch < hostChannels; ++ch)
        fc.destBuffer->clear(ch, startSample, numSamples);

    const float thresholdDb = realForSlot(kThresholdSlot);
    const float ratio = realForSlot(kRatioSlot);
    const float kneeDb = realForSlot(kKneeSlot);
    const float keyDb = ampToDb(keyPeak);

    inputPeakDb_.store(ampToDb(inputPeak), std::memory_order_relaxed);
    keyPeakDb_.store(keyDb, std::memory_order_relaxed);
    outputPeakDb_.store(ampToDb(outputPeak), std::memory_order_relaxed);
    gainReductionDb_.store(gainReductionForLevel(keyDb, thresholdDb, ratio, kneeDb),
                           std::memory_order_relaxed);
    usingExternalSidechain_.store(externalSidechain, std::memory_order_relaxed);
}

te::AutomatableParameter* MagdaCompressorCompiledPlugin::getSlotParameter(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nullptr;
    return hostParams_[static_cast<size_t>(slotIndex)].get();
}

const MagdaCompressorCompiledPlugin::HostSlotInfo& MagdaCompressorCompiledPlugin::getSlotInfo(
    int slotIndex) const {
    static const HostSlotInfo kEmpty;
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return kEmpty;
    return hostSlotInfo_[static_cast<size_t>(slotIndex)];
}

int MagdaCompressorCompiledPlugin::activeEngineIndex() const {
    return activeEngine_.load(std::memory_order_relaxed);
}

float MagdaCompressorCompiledPlugin::displayValueToNativeValue(int slotIndex,
                                                               float displayValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return displayValue;
    const auto info = parameterInfoForSlot(hostSlotInfo_[static_cast<size_t>(slotIndex)]);
    return magda::ParameterUtils::realToNormalized(displayValue, info);
}

float MagdaCompressorCompiledPlugin::nativeValueToDisplayValue(int slotIndex,
                                                               float nativeValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nativeValue;
    const auto info = parameterInfoForSlot(hostSlotInfo_[static_cast<size_t>(slotIndex)]);
    return magda::ParameterUtils::normalizedToReal(nativeValue, info);
}

constexpr AliasSpec kAliases[] = {
    {"engine", 0, "Engine"},      {"threshold", 1, "Threshold"},
    {"ratio", 2, "Ratio"},        {"attack", 3, "Attack"},
    {"release", 4, "Release"},    {"knee", 5, "Knee"},
    {"makeup", 6, "Makeup"},      {"mix", 7, "Mix"},
    {"output", 8, "Output"},      {"detector", 9, "Detector"},
    {"link", 10, "Link"},         {"sc_hpf", 11, "SC HPF"},
    {"fbff", 12, "FBFF"},         {"style", 13, "Style"},
    {"autogain", 14, "Autogain"},
};

const CompiledPluginSpec& getMagdaCompressorSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaCompressorCompiledPlugin::xmlTypeName,
        .displayName = "Compressor",
        .browserCategory = "Dynamics",
        .description =
            "Compiled Faust compressor with selectable engines.\n"
            "<b>Clean</b>: feed-forward, peak/RMS detection, soft knee, stereo link, "
            "sidechain HPF, external audio sidechain, parallel mix, output safety limiting.\n"
            "<b>Glue</b>: Brouns FBFF compressor with exposed character controls "
            "(Detector Peak/RMS, Style Pre/Post, FBFF blend). No external sidechain.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaCompressorCompiledPlugin(info);
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
