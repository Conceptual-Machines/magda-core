#include "plugins/compiled/MagdaPolySynthCompiledPlugin.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/ParameterUtils.hpp"
#include "core/TechnicalText.hpp"
#include "faust/dsp/dsp.h"
#include "faust/dsp/poly-dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_polysynth.generated.cpp"
#include "plugins/FaustMetadataParser.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

// NOTE: Faust's GUI base statics (GUI::fGuiList / gTimedZoneMap), required by
// poly-dsp.h, are defined once in FaustPolyGuiStatics.cpp — see that file.

namespace magda::daw::audio::compiled {

const char* MagdaPolySynthCompiledPlugin::xmlTypeName = "magda_polysynth";

namespace {

// Harvest the per-voice [idx:N] zones from a mydsp_poly built with group=false.
// That layout emits, under a "Polyphonic" tab, a shared "Voices" proxy box
// (whose zones only propagate via the global GUI::updateAllGuis(), which we
// avoid) followed by one "Voice<n>" box per voice carrying that voice's own
// directly-writable zones. We keep the per-voice zones and group them by idx so
// a single host slot can fan a write out to every voice.
class PolyVoiceHarvester : public ::UI {
  public:
    // idx -> per-voice zones (encounter order). Reserved freq/gain/gate carry no
    // [idx] (slotIndex == -1) and are skipped automatically.
    std::map<int, std::vector<FAUSTFLOAT*>> zonesByIdx;

    void openTabBox(const char* label) override {
        pushGroup(label);
    }
    void openHorizontalBox(const char* label) override {
        pushGroup(label);
    }
    void openVerticalBox(const char* label) override {
        pushGroup(label);
    }
    void closeBox() override {
        if (!groupLabels_.empty())
            groupLabels_.pop_back();
    }

    void addButton(const char* label, FAUSTFLOAT* zone) override {
        emit(label, zone);
    }
    void addCheckButton(const char* label, FAUSTFLOAT* zone) override {
        emit(label, zone);
    }
    void addVerticalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT,
                           FAUSTFLOAT) override {
        emit(label, zone);
    }
    void addHorizontalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT, FAUSTFLOAT,
                             FAUSTFLOAT, FAUSTFLOAT) override {
        emit(label, zone);
    }
    void addNumEntry(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT,
                     FAUSTFLOAT) override {
        emit(label, zone);
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
    void pushGroup(const char* label) {
        groupLabels_.push_back(
            parseFaustLabel(juce::String::fromUTF8(label != nullptr ? label : "")).cleanLabel);
    }

    // True when the nearest voice-level ancestor is the shared proxy box
    // ("Voices"), false when it's an individual voice ("Voice1".. / "V1"..).
    bool inProxyGroup() const {
        for (auto it = groupLabels_.rbegin(); it != groupLabels_.rend(); ++it) {
            if (*it == "Voices")
                return true;
            if (it->startsWith("Voice") || (it->length() >= 2 && (*it)[0] == 'V' &&
                                            juce::CharacterFunctions::isDigit((*it)[1])))
                return false;
        }
        return false;
    }

    void emit(const char* rawLabel, FAUSTFLOAT* zone) {
        const auto parsed =
            parseFaustLabel(juce::String::fromUTF8(rawLabel != nullptr ? rawLabel : ""));
        ControlMetadata merged = parsed.metadata;
        if (zone != nullptr) {
            if (auto it = pendingByZone_.find(zone); it != pendingByZone_.end()) {
                mergeFaustMetadata(merged, it->second);
                pendingByZone_.erase(it);
            }
        }
        if (inProxyGroup() || merged.slotIndex < 0 || zone == nullptr)
            return;
        zonesByIdx[merged.slotIndex].push_back(zone);
    }

    std::vector<juce::String> groupLabels_;
    std::map<FAUSTFLOAT*, ControlMetadata> pendingByZone_;
};

}  // namespace

MagdaPolySynthCompiledPlugin::MagdaPolySynthCompiledPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    constexpr int kProvisionalSampleRate = 44100;
    rebuildEngineState(kProvisionalSampleRate);
    buildHostParameters();
}

MagdaPolySynthCompiledPlugin::~MagdaPolySynthCompiledPlugin() {
    notifyListenersOfDeletion();
    for (auto& p : hostParams_)
        if (p)
            p->detachFromCurrentValue();
}

juce::String MagdaPolySynthCompiledPlugin::getName() const {
    return "Poly Synth";
}
juce::String MagdaPolySynthCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaPolySynthCompiledPlugin::getShortName(int) {
    return "PolySynth";
}
juce::String MagdaPolySynthCompiledPlugin::getSelectableDescription() {
    return "Poly Synth";
}

void MagdaPolySynthCompiledPlugin::rebuildEngineState(int sampleRate) {
    // Wrap a fresh single-voice MagdaPolySynthDsp in the poly allocator.
    // control=true: voices are MIDI-allocated. group=false: each voice keeps its
    // own writable zones (we fan host-macro writes out to all of them).
    poly_ = std::make_unique<mydsp_poly>(new MagdaPolySynthDsp(), kNumVoices, /*control*/ true,
                                         /*group*/ false);
    poly_->init(sampleRate);
    numOutputs_ = poly_->getNumOutputs();

    PolyVoiceHarvester harvester;
    poly_->buildUserInterface(&harvester);

    for (auto& slot : voiceZonesBySlot_)
        slot.clear();
    for (int i = 0; i < kHostSlotCount; ++i) {
        if (auto it = harvester.zonesByIdx.find(i); it != harvester.zonesByIdx.end())
            voiceZonesBySlot_[static_cast<size_t>(i)] = it->second;
    }
}

void MagdaPolySynthCompiledPlugin::buildHostParameters() {
    const std::vector<juce::String> waveChoices{"Sine", "Saw", "Square", "Triangle"};

    // Four contiguous slots per oscillator (wave / level / coarse / fine).
    // Osc 1 is audible by default; the rest start silent.
    for (int osc = 0; osc < kNumOscillators; ++osc) {
        const int base = kOscBaseSlot + osc * kOscSlotCount;
        const juce::String prefix = "Osc " + juce::String(osc + 1) + " ";

        hostSlotInfo_[base + 0] = {.name = prefix + "Wave",
                                   .scale = magda::ParameterScale::Discrete,
                                   .minValue = 0.0f,
                                   .maxValue = static_cast<float>(waveChoices.size() - 1),
                                   .defaultValue = 1.0f,  // Saw
                                   .choices = waveChoices};
        hostSlotInfo_[base + 1] = {.name = prefix + "Level",
                                   .scale = magda::ParameterScale::Linear,
                                   .minValue = 0.0f,
                                   .maxValue = 1.0f,
                                   .defaultValue = (osc == 0) ? 0.8f : 0.0f};
        hostSlotInfo_[base + 2] = {.name = prefix + "Coarse",
                                   .unit =
                                       magda::technicalText(magda::TechnicalTextToken::Semitones),
                                   .scale = magda::ParameterScale::Linear,
                                   .minValue = -24.0f,
                                   .maxValue = 24.0f,
                                   .defaultValue = 0.0f};
        hostSlotInfo_[base + 3] = {.name = prefix + "Fine",
                                   .unit = magda::technicalText(magda::TechnicalTextToken::Cents),
                                   .scale = magda::ParameterScale::Linear,
                                   .minValue = -100.0f,
                                   .maxValue = 100.0f,
                                   .defaultValue = 0.0f};
    }

    hostSlotInfo_[kFilterTypeSlot] = {.name = "Filter Type",
                                      .scale = magda::ParameterScale::Discrete,
                                      .minValue = 0.0f,
                                      .maxValue = 3.0f,
                                      .defaultValue = 0.0f,
                                      .choices = {"Lowpass", "Highpass", "Bandpass", "Notch"}};
    hostSlotInfo_[kCutoffSlot] = {.name = "Cutoff",
                                  .unit = "Hz",
                                  .scale = magda::ParameterScale::Logarithmic,
                                  .minValue = 50.0f,
                                  .maxValue = 18000.0f,
                                  .defaultValue = 3000.0f};
    hostSlotInfo_[kResonanceSlot] = {.name = "Resonance",
                                     .scale = magda::ParameterScale::Linear,
                                     .minValue = 0.0f,
                                     .maxValue = 0.95f,
                                     .defaultValue = 0.3f};
    hostSlotInfo_[kFilterEnvAmtSlot] = {.name = "Filter Env",
                                        .unit = "oct",
                                        .scale = magda::ParameterScale::Linear,
                                        .minValue = -4.0f,
                                        .maxValue = 4.0f,
                                        .defaultValue = 0.0f};
    hostSlotInfo_[kFilterAttackSlot] = {.name = "Filter Attack",
                                        .unit = "s",
                                        .scale = magda::ParameterScale::Linear,
                                        .minValue = 0.001f,
                                        .maxValue = 2.0f,
                                        .defaultValue = 0.005f};
    hostSlotInfo_[kFilterDecaySlot] = {.name = "Filter Decay",
                                       .unit = "s",
                                       .scale = magda::ParameterScale::Linear,
                                       .minValue = 0.001f,
                                       .maxValue = 2.0f,
                                       .defaultValue = 0.2f};
    hostSlotInfo_[kFilterSustainSlot] = {.name = "Filter Sustain",
                                         .scale = magda::ParameterScale::Linear,
                                         .minValue = 0.0f,
                                         .maxValue = 1.0f,
                                         .defaultValue = 0.7f};
    hostSlotInfo_[kFilterReleaseSlot] = {.name = "Filter Release",
                                         .unit = "s",
                                         .scale = magda::ParameterScale::Linear,
                                         .minValue = 0.001f,
                                         .maxValue = 4.0f,
                                         .defaultValue = 0.4f};

    hostSlotInfo_[kAmpAttackSlot] = {.name = "Amp Attack",
                                     .unit = "s",
                                     .scale = magda::ParameterScale::Linear,
                                     .minValue = 0.001f,
                                     .maxValue = 2.0f,
                                     .defaultValue = 0.005f};
    hostSlotInfo_[kAmpDecaySlot] = {.name = "Amp Decay",
                                    .unit = "s",
                                    .scale = magda::ParameterScale::Linear,
                                    .minValue = 0.001f,
                                    .maxValue = 2.0f,
                                    .defaultValue = 0.2f};
    hostSlotInfo_[kAmpSustainSlot] = {.name = "Amp Sustain",
                                      .scale = magda::ParameterScale::Linear,
                                      .minValue = 0.0f,
                                      .maxValue = 1.0f,
                                      .defaultValue = 0.7f};
    hostSlotInfo_[kAmpReleaseSlot] = {.name = "Amp Release",
                                      .unit = "s",
                                      .scale = magda::ParameterScale::Linear,
                                      .minValue = 0.001f,
                                      .maxValue = 4.0f,
                                      .defaultValue = 0.4f};

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
        const juce::String id = "magda_polysynth_" + slot.name.toLowerCase().replace(" ", "_");
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

void MagdaPolySynthCompiledPlugin::initialise(const te::PluginInitialisationInfo& info) {
    rebuildEngineState(static_cast<int>(info.sampleRate));
    scratchOut_.setSize(std::max(numOutputs_, 2), info.blockSizeSamples, false, true, true);
    outPtrs_.assign(static_cast<size_t>(std::max(numOutputs_, 2)), nullptr);
}

void MagdaPolySynthCompiledPlugin::deinitialise() {
    scratchOut_.setSize(0, 0);
    outPtrs_.clear();
}

void MagdaPolySynthCompiledPlugin::reset() {
    if (poly_)
        poly_->instanceClear();
}

void MagdaPolySynthCompiledPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!poly_ || !fc.destBuffer || fc.bufferNumSamples <= 0)
        return;

    // Fan each host macro out to every voice's zone (RT-safe pointer writes).
    for (int slot = 0; slot < kHostSlotCount; ++slot) {
        const auto& s = hostSlotInfo_[static_cast<size_t>(slot)];
        const float norm = hostParams_[static_cast<size_t>(slot)]->getCurrentValue();

        FAUSTFLOAT real;
        if (s.scale == magda::ParameterScale::Discrete) {
            // Discrete index = round(norm * (count - 1)); maxValue already holds
            // count - 1, so we avoid copying the choices vector (which would
            // allocate) onto the audio thread just to call normalizedToReal.
            real = static_cast<FAUSTFLOAT>(std::round(juce::jlimit(0.0f, 1.0f, norm) * s.maxValue));
        } else {
            magda::ParameterInfo info;
            info.minValue = s.minValue;
            info.maxValue = s.maxValue;
            info.scale = s.scale;
            if (std::isfinite(s.scaleAnchor))
                info.scaleAnchor = s.scaleAnchor;
            real = static_cast<FAUSTFLOAT>(magda::ParameterUtils::normalizedToReal(norm, info));
        }

        for (FAUSTFLOAT* zone : voiceZonesBySlot_[static_cast<size_t>(slot)])
            if (zone)
                *zone = real;
    }

    // Drive voice allocation from this block's MIDI. Sub-block timing offsets are
    // ignored for now (all events applied before compute).
    if (fc.bufferForMidiMessages != nullptr && !fc.bufferForMidiMessages->isEmpty()) {
        auto* polyMidi = poly_.get();
        for (auto& m : *fc.bufferForMidiMessages) {
            if (m.isNoteOn())
                polyMidi->keyOn(m.getChannel(), m.getNoteNumber(), m.getVelocity());
            else if (m.isNoteOff())
                polyMidi->keyOff(m.getChannel(), m.getNoteNumber(), m.getVelocity());
            else if (m.isPitchWheel())
                polyMidi->pitchWheel(m.getChannel(), m.getPitchWheelValue());
            else if (m.isController())
                polyMidi->ctrlChange(m.getChannel(), m.getControllerNumber(),
                                     m.getControllerValue());
        }
    }

    const int n = fc.bufferNumSamples;
    const int start = fc.bufferStartSample;
    const int hostChannels = fc.destBuffer->getNumChannels();
    if (hostChannels <= 0 || numOutputs_ <= 0 || scratchOut_.getNumSamples() <= 0)
        return;

    // Render into scratch (compute() overwrites its outputs), then ADD into
    // destBuffer. Chunk to MIX_BUFFER_SIZE — mydsp_poly's mix buffers cap there.
    outPtrs_.resize(static_cast<size_t>(numOutputs_));
    const int maxChunk = std::min(MIX_BUFFER_SIZE, scratchOut_.getNumSamples());

    for (int offset = 0; offset < n; offset += maxChunk) {
        const int chunk = std::min(maxChunk, n - offset);
        for (int ch = 0; ch < numOutputs_; ++ch)
            outPtrs_[static_cast<size_t>(ch)] =
                scratchOut_.getWritePointer(ch % scratchOut_.getNumChannels());

        poly_->compute(chunk, nullptr, outPtrs_.data());

        for (int ch = 0; ch < hostChannels; ++ch) {
            const int srcCh = (numOutputs_ == 1) ? 0 : (ch % numOutputs_);
            fc.destBuffer->addFrom(ch, start + offset, scratchOut_, srcCh, 0, chunk);
        }
    }
}

te::AutomatableParameter* MagdaPolySynthCompiledPlugin::getSlotParameter(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nullptr;
    return hostParams_[static_cast<size_t>(slotIndex)].get();
}

const MagdaPolySynthCompiledPlugin::HostSlotInfo& MagdaPolySynthCompiledPlugin::getSlotInfo(
    int slotIndex) const {
    static const HostSlotInfo kEmpty;
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return kEmpty;
    return hostSlotInfo_[static_cast<size_t>(slotIndex)];
}

float MagdaPolySynthCompiledPlugin::displayValueToNativeValue(int slotIndex,
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

float MagdaPolySynthCompiledPlugin::nativeValueToDisplayValue(int slotIndex,
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

const CompiledPluginSpec& getMagdaPolySynthSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaPolySynthCompiledPlugin::xmlTypeName,
        .displayName = "Poly Synth",
        .browserCategory = "Synth",
        .description = "Compiled Faust polyphonic synth: four detunable oscillators "
                       "(sine/saw/square/triangle) into a multimode filter with its own "
                       "envelope, plus an ADSR amp envelope. 16-voice, MIDI-driven.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaPolySynthCompiledPlugin(info);
        },
        .isInstrument = true,
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
