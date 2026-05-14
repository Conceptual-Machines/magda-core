#include "plugins/compiled/MagdaLimiterCompiledPlugin.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/ParameterUtils.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_limiter.generated.cpp"
#include "plugins/FaustMetadataParser.hpp"
#include "plugins/FaustParamInfo.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaLimiterCompiledPlugin::xmlTypeName = "magda_limiter";

namespace {

struct LimiterHarvest {
    struct Control {
        int idx = -1;
        FaustParamSlot::Kind kind = FaustParamSlot::Kind::Continuous;
        FAUSTFLOAT* zone = nullptr;
    };
    std::vector<Control> controls;
};

class LimiterHarvester : public ::UI {
  public:
    LimiterHarvest harvest;

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

        LimiterHarvest::Control c;
        c.idx = merged.slotIndex;
        c.kind = merged.isMenuStyle ? FaustParamSlot::Kind::Discrete : kind;
        c.zone = zone;
        harvest.controls.push_back(std::move(c));
    }

    std::map<FAUSTFLOAT*, ControlMetadata> pendingByZone_;
};

const LimiterHarvest::Control* findByIdx(const LimiterHarvest& h, int idx) {
    for (const auto& c : h.controls)
        if (c.idx == idx)
            return &c;
    return nullptr;
}

}  // namespace

MagdaLimiterCompiledPlugin::MagdaLimiterCompiledPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    dsp_ = std::make_unique<MagdaLimiterDsp>();
    constexpr int kProvisionalSampleRate = 44100;
    rebuildEngineState(kProvisionalSampleRate);
    buildHostParameters();
}

MagdaLimiterCompiledPlugin::~MagdaLimiterCompiledPlugin() {
    notifyListenersOfDeletion();
    for (auto& p : hostParams_)
        if (p)
            p->detachFromCurrentValue();
}

juce::String MagdaLimiterCompiledPlugin::getName() const {
    return "Limiter";
}
juce::String MagdaLimiterCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaLimiterCompiledPlugin::getShortName(int) {
    return "Limiter";
}
juce::String MagdaLimiterCompiledPlugin::getSelectableDescription() {
    return "Limiter";
}

void MagdaLimiterCompiledPlugin::rebuildEngineState(int sampleRate) {
    if (!dsp_)
        return;
    dsp_->init(sampleRate);
    numInputs_ = dsp_->getNumInputs();
    numOutputs_ = dsp_->getNumOutputs();

    LimiterHarvester harvester;
    dsp_->buildUserInterface(&harvester);

    zones_.fill(nullptr);
    for (int i = 0; i < kHostSlotCount; ++i) {
        if (auto* c = findByIdx(harvester.harvest, i))
            zones_[static_cast<size_t>(i)] = c->zone;
    }
}

void MagdaLimiterCompiledPlugin::buildHostParameters() {
    hostSlotInfo_[kThresholdSlot] = {.name = "Threshold",
                                     .unit = "dB",
                                     .scale = magda::ParameterScale::Linear,
                                     .minValue = -24.0f,
                                     .maxValue = 0.0f,
                                     .defaultValue = -1.0f};
    hostSlotInfo_[kAttackSlot] = {.name = "Attack",
                                  .unit = "ms",
                                  .scale = magda::ParameterScale::Logarithmic,
                                  .minValue = 0.1f,
                                  .maxValue = 50.0f,
                                  .defaultValue = 1.0f,
                                  .scaleAnchor = 1.0f};
    hostSlotInfo_[kHoldSlot] = {.name = "Hold",
                                .unit = "ms",
                                .scale = magda::ParameterScale::Logarithmic,
                                .minValue = 1.0f,
                                .maxValue = 500.0f,
                                .defaultValue = 50.0f,
                                .scaleAnchor = 50.0f};
    hostSlotInfo_[kReleaseSlot] = {.name = "Release",
                                   .unit = "ms",
                                   .scale = magda::ParameterScale::Logarithmic,
                                   .minValue = 10.0f,
                                   .maxValue = 2000.0f,
                                   .defaultValue = 200.0f,
                                   .scaleAnchor = 200.0f};
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
    hostSlotInfo_[kAutogainSlot].name = "Autogain";
    hostSlotInfo_[kAutogainSlot].scale = magda::ParameterScale::Discrete;
    hostSlotInfo_[kAutogainSlot].choices = {"Off", "On"};
    hostSlotInfo_[kAutogainSlot].minValue = 0.0f;
    hostSlotInfo_[kAutogainSlot].maxValue = 1.0f;
    hostSlotInfo_[kAutogainSlot].defaultValue = 0.0f;

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
        const juce::String id = "magda_limiter_" + slot.name.toLowerCase().replace(" ", "_");
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

void MagdaLimiterCompiledPlugin::initialise(const te::PluginInitialisationInfo& info) {
    rebuildEngineState(static_cast<int>(info.sampleRate));
    scratchIn_.setSize(numInputs_, info.blockSizeSamples, false, true, true);
    scratchOut_.setSize(numOutputs_, info.blockSizeSamples, false, true, true);
    inPtrs_.assign(static_cast<size_t>(numInputs_), nullptr);
    outPtrs_.assign(static_cast<size_t>(numOutputs_), nullptr);
}

void MagdaLimiterCompiledPlugin::deinitialise() {
    scratchIn_.setSize(0, 0);
    scratchOut_.setSize(0, 0);
    inPtrs_.clear();
    outPtrs_.clear();
}

void MagdaLimiterCompiledPlugin::reset() {
    if (dsp_)
        dsp_->instanceClear();
}

void MagdaLimiterCompiledPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.destBuffer || fc.bufferNumSamples <= 0 || !dsp_)
        return;

    auto realForSlot = [&](int slot) -> float {
        const auto& s = hostSlotInfo_[static_cast<size_t>(slot)];
        magda::ParameterInfo info;
        info.minValue = s.minValue;
        info.maxValue = s.maxValue;
        info.scale = s.scale;
        if (std::isfinite(s.scaleAnchor))
            info.scaleAnchor = s.scaleAnchor;
        // For Discrete params, normalizedToReal returns 0 when choices is
        // empty — must copy the choice list across or the conversion is
        // silently broken for Autogain / Output dropdowns.
        info.choices = s.choices;
        const float norm = hostParams_[static_cast<size_t>(slot)]->getCurrentValue();
        return magda::ParameterUtils::normalizedToReal(norm, info);
    };
    auto writeSlot = [&](int slot) {
        if (auto* zone = zones_[static_cast<size_t>(slot)])
            *zone = static_cast<FAUSTFLOAT>(realForSlot(slot));
    };
    for (int i = 0; i < kHostSlotCount; ++i)
        writeSlot(i);

    // --- DEBUG TRACE ----------------------------------------------------------
    // Throttled to ~once per second. Shows what the autogain slot is doing
    // and whether the zone is being written. Remove once verified.
    if ((++debugTraceCounter_ % 172) == 0) {
        const float thresholdReal = realForSlot(kThresholdSlot);
        const float autogainReal = realForSlot(kAutogainSlot);
        auto* autogainZone = zones_[static_cast<size_t>(kAutogainSlot)];
        const float zoneValue =
            autogainZone != nullptr ? static_cast<float>(*autogainZone) : -999.0f;
        const float expectedPreGainDb = autogainReal * (-thresholdReal);
        DBG("[Lim] thr=" << thresholdReal << " autogain_slot=" << autogainReal
                         << " zone=" << zoneValue << " expected_pregain_dB=" << expectedPreGainDb);
    }
    // --- END DEBUG TRACE ------------------------------------------------------

    const int numSamples = fc.bufferNumSamples;
    const int startSample = fc.bufferStartSample;
    const int hostChannels = fc.destBuffer->getNumChannels();
    if (hostChannels <= 0 || numInputs_ <= 0 || numOutputs_ <= 0)
        return;

    if (scratchIn_.getNumChannels() < numInputs_ || scratchIn_.getNumSamples() < numSamples)
        scratchIn_.setSize(numInputs_, numSamples, false, true, true);
    if (scratchOut_.getNumChannels() < numOutputs_ || scratchOut_.getNumSamples() < numSamples)
        scratchOut_.setSize(numOutputs_, numSamples, false, true, true);
    if (static_cast<int>(inPtrs_.size()) < numInputs_)
        inPtrs_.resize(static_cast<size_t>(numInputs_), nullptr);
    if (static_cast<int>(outPtrs_.size()) < numOutputs_)
        outPtrs_.resize(static_cast<size_t>(numOutputs_), nullptr);

    float inputPeak = 0.0f;
    for (int ch = 0; ch < numInputs_; ++ch) {
        float* dst = scratchIn_.getWritePointer(ch);
        if (ch < hostChannels) {
            const float* src = fc.destBuffer->getReadPointer(ch, startSample);
            std::copy(src, src + numSamples, dst);
            for (int i = 0; i < numSamples; ++i)
                inputPeak = std::max(inputPeak, std::fabs(dst[i]));
        } else {
            std::fill(dst, dst + numSamples, 0.0f);
        }
        inPtrs_[static_cast<size_t>(ch)] = dst;
    }
    for (int ch = 0; ch < numOutputs_; ++ch) {
        outPtrs_[static_cast<size_t>(ch)] = (ch < hostChannels)
                                                ? fc.destBuffer->getWritePointer(ch, startSample)
                                                : scratchOut_.getWritePointer(ch);
    }

    dsp_->compute(numSamples, inPtrs_.data(), outPtrs_.data());

    float outputPeak = 0.0f;
    const int channelsToSanitise = std::min(hostChannels, numOutputs_);
    for (int ch = 0; ch < channelsToSanitise; ++ch) {
        float* out = fc.destBuffer->getWritePointer(ch, startSample);
        for (int i = 0; i < numSamples; ++i) {
            const float sample = out[i];
            out[i] = std::isfinite(sample) ? juce::jlimit(-16.0f, 16.0f, sample) : 0.0f;
            outputPeak = std::max(outputPeak, std::fabs(out[i]));
        }
    }

    // Metering taps for the curve view. GR is the per-block dB difference;
    // the limiter only attenuates, so input - output is non-negative.
    auto ampToDb = [](float amp) { return 20.0f * std::log10(std::max(amp, 1.0e-6f)); };
    const float inDb = ampToDb(inputPeak);
    const float outDb = ampToDb(outputPeak);
    inputPeakDb_.store(inDb, std::memory_order_relaxed);
    outputPeakDb_.store(outDb, std::memory_order_relaxed);
    gainReductionDb_.store(std::max(0.0f, inDb - outDb), std::memory_order_relaxed);
}

te::AutomatableParameter* MagdaLimiterCompiledPlugin::getSlotParameter(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nullptr;
    return hostParams_[static_cast<size_t>(slotIndex)].get();
}

const MagdaLimiterCompiledPlugin::HostSlotInfo& MagdaLimiterCompiledPlugin::getSlotInfo(
    int slotIndex) const {
    static const HostSlotInfo kEmpty;
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return kEmpty;
    return hostSlotInfo_[static_cast<size_t>(slotIndex)];
}

float MagdaLimiterCompiledPlugin::displayValueToNativeValue(int slotIndex,
                                                            float displayValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return displayValue;
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

float MagdaLimiterCompiledPlugin::nativeValueToDisplayValue(int slotIndex,
                                                            float nativeValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nativeValue;
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
    {"threshold", 0, "Threshold"}, {"attack", 1, "Attack"}, {"hold", 2, "Hold"},
    {"release", 3, "Release"},     {"mix", 4, "Mix"},       {"output", 5, "Output"},
    {"autogain", 6, "Autogain"},
};

const CompiledPluginSpec& getMagdaLimiterSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaLimiterCompiledPlugin::xmlTypeName,
        .displayName = "Limiter",
        .browserCategory = "Dynamics",
        .description = "Compiled Faust stereo lookahead brickwall limiter. Sanfilippo design with "
                       "5 ms lookahead, peak-holder, and tau-smoothed attack/release. Threshold "
                       "sets the output ceiling in dB; peaks above it are attenuated with minimal "
                       "coloration, signal below passes through unchanged.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaLimiterCompiledPlugin(info);
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
