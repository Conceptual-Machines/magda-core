#include "plugins/compiled/MagdaFilterCompiledPlugin.hpp"

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

// All six engine DSPs are #included into THIS translation unit only —
// each generated `.cpp` defines a self-contained class, so co-locating
// them lets us instantiate one of each without conflicts. The per-engine
// wrapper TUs (MagdaSVFCompiled.cpp etc.) are removed; this file is the
// single owner of the generated code from now on.
#include "magda_filter_diode.generated.cpp"
#include "magda_filter_korg35.generated.cpp"
#include "magda_filter_ladder.generated.cpp"
#include "magda_filter_oberheim.generated.cpp"
#include "magda_filter_sk.generated.cpp"
#include "magda_filter_svf.generated.cpp"

namespace magda::daw::audio::compiled {

const char* MagdaFilterCompiledPlugin::xmlTypeName = "magda_filter";

namespace {

// ============================================================================
// HarvestUI — stripped-down version that just records each control's
// idx, kind, zone, range, and choices. We don't need the full
// FaustParamPool harvest because the merged plugin builds host params
// manually; we only need to find the cutoff/res/drive/mode zones per
// engine.
// ============================================================================

struct EngineHarvest {
    struct Control {
        int idx = -1;
        FaustParamSlot::Kind kind = FaustParamSlot::Kind::Continuous;
        FAUSTFLOAT* zone = nullptr;
        std::vector<std::pair<float, juce::String>> choices;
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

    // Faust's compiled C++ output emits `[key:value]` annotations through
    // per-zone `declare()` calls BEFORE the matching addControl() call —
    // not embedded in the displayed label. We collect those declares into
    // `pendingByZone` and merge them in when the control is emitted, so
    // `[idx:N]` and `[style:menu{...}]` end up on the right entry.
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
        c.choices = merged.menuChoices;
        harvest.controls.push_back(std::move(c));
    }

    std::map<FAUSTFLOAT*, ControlMetadata> pendingByZone_;
};

// Find a control in a harvest by its idx — returns nullptr if missing.
const EngineHarvest::Control* findByIdx(const EngineHarvest& h, int idx) {
    for (const auto& c : h.controls) {
        if (c.idx == idx)
            return &c;
    }
    return nullptr;
}

float clampSallenKeyCutoffHz(float cutoffHz, int sampleRate, float minCutoffHz) {
    if (sampleRate <= 0)
        return cutoffHz;

    constexpr float kStableNyquistFraction = 0.84f;
    const float maxCutoffHz = static_cast<float>(sampleRate) * 0.5f * kStableNyquistFraction;
    if (maxCutoffHz <= minCutoffHz)
        return minCutoffHz;
    return juce::jlimit(minCutoffHz, maxCutoffHz, cutoffHz);
}

}  // namespace

// ============================================================================
// MagdaFilterCompiledPlugin
// ============================================================================

MagdaFilterCompiledPlugin::MagdaFilterCompiledPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    // Instantiate all six engines.
    engines_[0].dsp = std::make_unique<MagdaSVFDsp>();
    engines_[1].dsp = std::make_unique<MagdaLadderDsp>();
    engines_[2].dsp = std::make_unique<MagdaKorg35Dsp>();
    engines_[3].dsp = std::make_unique<MagdaOberheimDsp>();
    engines_[4].dsp = std::make_unique<MagdaSallenKeyDsp>();
    engines_[5].dsp = std::make_unique<MagdaDiodeDsp>();

    constexpr int kProvisionalSampleRate = 44100;
    rebuildEngineState(kProvisionalSampleRate);
    buildHostParameters();
}

MagdaFilterCompiledPlugin::~MagdaFilterCompiledPlugin() {
    notifyListenersOfDeletion();
    for (auto& p : hostParams_)
        if (p)
            p->detachFromCurrentValue();
}

juce::String MagdaFilterCompiledPlugin::getName() const {
    return "Filter";
}
juce::String MagdaFilterCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaFilterCompiledPlugin::getShortName(int) {
    return "Filter";
}
juce::String MagdaFilterCompiledPlugin::getSelectableDescription() {
    return "Filter";
}

void MagdaFilterCompiledPlugin::rebuildEngineState(int sampleRate) {
    for (size_t engineIdx = 0; engineIdx < engines_.size(); ++engineIdx) {
        auto& e = engines_[engineIdx];
        if (!e.dsp)
            continue;
        e.sampleRate = sampleRate;
        e.dsp->init(sampleRate);
        e.numInputs = e.dsp->getNumInputs();
        e.numOutputs = e.dsp->getNumOutputs();

        EngineHarvester harvester;
        e.dsp->buildUserInterface(&harvester);

        e.cutoffZone = nullptr;
        e.resZone = nullptr;
        e.driveZone = nullptr;
        e.modeZone = nullptr;
        e.modeChoiceValues.clear();

        if (auto* c = findByIdx(harvester.harvest, 0))
            e.cutoffZone = c->zone;
        if (auto* c = findByIdx(harvester.harvest, 1))
            e.resZone = c->zone;
        if (auto* c = findByIdx(harvester.harvest, 2))
            e.driveZone = c->zone;
        if (auto* c = findByIdx(harvester.harvest, 3)) {
            e.modeZone = c->zone;
            auto sorted = c->choices;
            std::sort(sorted.begin(), sorted.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            e.modeChoiceValues.reserve(sorted.size());
            for (const auto& choice : sorted)
                e.modeChoiceValues.push_back(choice.first);
        }
    }
}

void MagdaFilterCompiledPlugin::buildHostParameters() {
    // Slot 0: Cutoff (continuous, log, anchored at 1 kHz)
    hostSlotInfo_[kCutoffSlot] = {.name = "Cutoff",
                                  .unit = "Hz",
                                  .scale = magda::ParameterScale::Logarithmic,
                                  .minValue = 20.0f,
                                  .maxValue = 20000.0f,
                                  .defaultValue = 1000.0f,
                                  .scaleAnchor = 1000.0f};
    // Slot 1: Resonance (continuous, linear, 0..1)
    hostSlotInfo_[kResonanceSlot] = {.name = "Resonance",
                                     .scale = magda::ParameterScale::Linear,
                                     .minValue = 0.0f,
                                     .maxValue = 1.0f,
                                     .defaultValue = 0.0f};
    // Slot 2: Drive (continuous, linear, 0..1)
    hostSlotInfo_[kDriveSlot] = {.name = "Drive",
                                 .scale = magda::ParameterScale::Linear,
                                 .minValue = 0.0f,
                                 .maxValue = 1.0f,
                                 .defaultValue = 0.0f};
    // Slot 3: Engine (discrete, 6 options)
    hostSlotInfo_[kEngineSlot].name = "Engine";
    hostSlotInfo_[kEngineSlot].scale = magda::ParameterScale::Discrete;
    hostSlotInfo_[kEngineSlot].choices = {"SVF",      "Ladder",     "Korg 35",
                                          "Oberheim", "Sallen-Key", "Diode"};
    hostSlotInfo_[kEngineSlot].minValue = 0.0f;
    hostSlotInfo_[kEngineSlot].maxValue =
        static_cast<float>(hostSlotInfo_[kEngineSlot].choices.size() - 1);
    hostSlotInfo_[kEngineSlot].defaultValue = 0.0f;
    // Slot 4: Mode (discrete, 4 options) — engine-specific fallback at runtime
    hostSlotInfo_[kModeSlot].name = "Mode";
    hostSlotInfo_[kModeSlot].scale = magda::ParameterScale::Discrete;
    hostSlotInfo_[kModeSlot].choices = {"LP", "BP", "HP", "Notch"};
    hostSlotInfo_[kModeSlot].minValue = 0.0f;
    hostSlotInfo_[kModeSlot].maxValue =
        static_cast<float>(hostSlotInfo_[kModeSlot].choices.size() - 1);
    hostSlotInfo_[kModeSlot].defaultValue = 0.0f;
    // Slot 5: Limit blends a post-filter soft limiter into the active engine.
    hostSlotInfo_[kLimitSlot] = {.name = "Limit",
                                 .scale = magda::ParameterScale::Linear,
                                 .minValue = 0.0f,
                                 .maxValue = 1.0f,
                                 .defaultValue = 0.0f};

    juce::NormalisableRange<float> normalisedRange{0.0f, 1.0f};
    auto* undoManager = getUndoManager();

    auto buildInfo = [](const HostSlotInfo& s) {
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
    };

    for (int i = 0; i < kHostSlotCount; ++i) {
        const auto& slot = hostSlotInfo_[i];
        const juce::String id = "magda_filter_" + slot.name.toLowerCase().replace(" ", "_");
        const juce::Identifier identifier(id);
        const auto info = buildInfo(slot);
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

void MagdaFilterCompiledPlugin::initialise(const te::PluginInitialisationInfo& info) {
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

void MagdaFilterCompiledPlugin::deinitialise() {
    scratchIn_.setSize(0, 0);
    scratchOut_.setSize(0, 0);
    inPtrs_.clear();
    outPtrs_.clear();
}

void MagdaFilterCompiledPlugin::reset() {
    for (auto& e : engines_)
        if (e.dsp)
            e.dsp->instanceClear();
}

void MagdaFilterCompiledPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.destBuffer || fc.bufferNumSamples <= 0)
        return;

    // Resolve normalized host param values once per block.
    const float cutoffNorm = hostParams_[kCutoffSlot]->getCurrentValue();
    const float resNorm = hostParams_[kResonanceSlot]->getCurrentValue();
    const float driveNorm = hostParams_[kDriveSlot]->getCurrentValue();
    const float engineNorm = hostParams_[kEngineSlot]->getCurrentValue();
    const float modeNorm = hostParams_[kModeSlot]->getCurrentValue();
    const float limitMix = juce::jlimit(0.0f, 1.0f, hostParams_[kLimitSlot]->getCurrentValue());

    // Denormalize cutoff/res/drive into native units once and write the
    // same value into every engine's matching zone — keeps every engine's
    // params in sync so an engine swap preserves the user's settings.
    const auto cutoffInfo = [this] {
        magda::ParameterInfo i;
        i.minValue = hostSlotInfo_[kCutoffSlot].minValue;
        i.maxValue = hostSlotInfo_[kCutoffSlot].maxValue;
        i.scale = hostSlotInfo_[kCutoffSlot].scale;
        i.scaleAnchor = hostSlotInfo_[kCutoffSlot].scaleAnchor;
        return i;
    }();
    const float cutoffHz = magda::ParameterUtils::normalizedToReal(cutoffNorm, cutoffInfo);
    const float resReal = juce::jlimit(0.0f, 1.0f, resNorm);
    const float driveReal = juce::jlimit(0.0f, 1.0f, driveNorm);

    const int engineCount = static_cast<int>(engines_.size());
    const int engineIndex = juce::jlimit(
        0, engineCount - 1,
        static_cast<int>(std::round(engineNorm * static_cast<float>(engineCount - 1))));
    activeEngine_.store(engineIndex);

    for (int e = 0; e < engineCount; ++e) {
        auto& engine = engines_[e];
        if (engine.cutoffZone) {
            float engineCutoffHz = cutoffHz;
            if (e == static_cast<int>(FilterFamily::SallenKey)) {
                engineCutoffHz = clampSallenKeyCutoffHz(cutoffHz, engine.sampleRate,
                                                        hostSlotInfo_[kCutoffSlot].minValue);
            }
            *engine.cutoffZone = static_cast<FAUSTFLOAT>(engineCutoffHz);
        }
        if (engine.resZone)
            *engine.resZone = static_cast<FAUSTFLOAT>(resReal);
        if (engine.driveZone)
            *engine.driveZone = static_cast<FAUSTFLOAT>(driveReal);
        // Mode is normalized 0..1 in the host param. Each engine has its
        // own mode count (Korg35=2, Ladder=1, SVF=4, …); map the normalized
        // value to this engine's local mode index and write the underlying
        // Faust value. Same engine-aware count is used for the UI dropdown,
        // so what the user sees and what audio does stay aligned.
        if (engine.modeZone && !engine.modeChoiceValues.empty()) {
            const int count = static_cast<int>(engine.modeChoiceValues.size());
            const int idx = juce::jlimit(
                0, count - 1,
                static_cast<int>(std::round(modeNorm * static_cast<float>(count - 1))));
            *engine.modeZone = static_cast<FAUSTFLOAT>(engine.modeChoiceValues[idx]);
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

    // Make sure the scratch buffers can hold this block — and have at least
    // as many channels as the active engine needs. `initialise()` sizes them
    // to the max-channels-across-engines, but if a stale 0-channel buffer
    // ever reaches us we'd otherwise call getWritePointer with an invalid
    // channel and read garbage / silence.
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

    // Optional post-filter soft limiting, then sanitise output.
    const int channelsToSanitise = std::min(hostChannels, numOutputs);
    for (int ch = 0; ch < channelsToSanitise; ++ch) {
        float* out = fc.destBuffer->getWritePointer(ch, startSample);
        for (int i = 0; i < numSamples; ++i) {
            float sample = out[i];
            if (limitMix > 0.0f && std::isfinite(sample)) {
                const float limited = std::tanh(sample);
                sample += (limited - sample) * limitMix;
            }
            out[i] = std::isfinite(sample) ? juce::jlimit(-16.0f, 16.0f, sample) : 0.0f;
        }
    }
}

te::AutomatableParameter* MagdaFilterCompiledPlugin::getSlotParameter(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nullptr;
    return hostParams_[static_cast<size_t>(slotIndex)].get();
}

const MagdaFilterCompiledPlugin::HostSlotInfo& MagdaFilterCompiledPlugin::getSlotInfo(
    int slotIndex) const {
    static const HostSlotInfo kEmpty;
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return kEmpty;
    return hostSlotInfo_[static_cast<size_t>(slotIndex)];
}

int MagdaFilterCompiledPlugin::activeEngineIndex() const {
    if (!hostParams_[kEngineSlot])
        return 0;
    const float norm = hostParams_[kEngineSlot]->getCurrentValue();
    return juce::jlimit(0, kEngineCount - 1,
                        static_cast<int>(std::round(norm * static_cast<float>(kEngineCount - 1))));
}

std::vector<juce::String> MagdaFilterCompiledPlugin::modeChoicesForEngine(int engineIndex) const {
    // Each engine's Faust source declares only the modes it can actually
    // produce; we mirror those sets here so the host's Mode dropdown only
    // offers options that map to a real branch in `mapModeToEngineValue`.
    switch (static_cast<FilterFamily>(engineIndex)) {
        case FilterFamily::SVF:
            return {"LP", "BP", "HP", "Notch"};
        case FilterFamily::Ladder:
            return {"LP"};
        case FilterFamily::Korg35:
            return {"LP", "HP"};
        case FilterFamily::Oberheim:
            return {"LP", "BP", "HP", "Notch"};
        case FilterFamily::SallenKey:
            return {"LP", "BP", "HP"};
        case FilterFamily::Diode:
            return {"LP"};
    }
    return {"LP"};
}

float MagdaFilterCompiledPlugin::displayValueToNativeValue(int slotIndex,
                                                           float displayValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return displayValue;
    // Mode's choice list is engine-dependent, so a slider position (display
    // index 0..N-1 for the active engine's N modes) projects to normalized
    // 0..1 using THIS engine's count, not the static hostSlotInfo. Audio
    // side mirrors the same per-engine count in applyToBuffer.
    if (slotIndex == kModeSlot) {
        const auto choices = modeChoicesForEngine(activeEngineIndex());
        const int count = static_cast<int>(choices.size());
        if (count <= 1)
            return 0.0f;
        const int idx = juce::jlimit(0, count - 1, static_cast<int>(std::round(displayValue)));
        return static_cast<float>(idx) / static_cast<float>(count - 1);
    }
    const auto& s = hostSlotInfo_[static_cast<size_t>(slotIndex)];
    magda::ParameterInfo info;
    info.minValue = s.minValue;
    info.maxValue = s.maxValue;
    info.scale = s.scale;
    if (std::isfinite(s.scaleAnchor))
        info.scaleAnchor = s.scaleAnchor;
    info.choices = s.choices;
    return magda::ParameterUtils::realToNormalized(displayValue, info);
}

float MagdaFilterCompiledPlugin::nativeValueToDisplayValue(int slotIndex, float nativeValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nativeValue;
    if (slotIndex == kModeSlot) {
        const auto choices = modeChoicesForEngine(activeEngineIndex());
        const int count = static_cast<int>(choices.size());
        if (count <= 1)
            return 0.0f;
        const int idx =
            juce::jlimit(0, count - 1,
                         static_cast<int>(std::round(nativeValue * static_cast<float>(count - 1))));
        return static_cast<float>(idx);
    }
    const auto& s = hostSlotInfo_[static_cast<size_t>(slotIndex)];
    magda::ParameterInfo info;
    info.minValue = s.minValue;
    info.maxValue = s.maxValue;
    info.scale = s.scale;
    if (std::isfinite(s.scaleAnchor))
        info.scaleAnchor = s.scaleAnchor;
    info.choices = s.choices;
    return magda::ParameterUtils::normalizedToReal(nativeValue, info);
}

constexpr AliasSpec kAliases[] = {
    {"cutoff", 0, "Cutoff"}, {"resonance", 1, "Resonance"}, {"drive", 2, "Drive"},
    {"engine", 3, "Engine"}, {"mode", 4, "Mode"},           {"limit", 5, "Limit"},
};

const CompiledPluginSpec& getMagdaFilterSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaFilterCompiledPlugin::xmlTypeName,
        .displayName = "Filter",
        .browserCategory = "Filter",
        .description =
            "Compiled Faust multimode filter.\n"
            "<b>SVF</b>: clean 2-pole LP/BP/HP/Notch for precise shaping.\n"
            "<b>Ladder</b>: classic 4-pole low-pass with driven resonance.\n"
            "<b>Korg 35</b>: MS-style LP/HP character with sharper analog bite.\n"
            "<b>Oberheim</b>: SEM-style LP/BP/HP/Notch with broad musical sweeps.\n"
            "<b>Sallen-Key</b>: smooth 2nd-order LP/BP/HP response.\n"
            "<b>Diode</b>: resonant 4-pole diode ladder with input drive.\n"
            "<warning>Warning: high resonance can create very loud peaks or "
            "self-oscillation. "
            "Keep monitoring levels conservative to protect speakers and ears.</warning>",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaFilterCompiledPlugin(info);
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
