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
    // idx -> per-voice zones (encounter order).
    std::map<int, std::vector<FAUSTFLOAT*>> zonesByIdx;
    // Reserved controls carry no [idx]: freq/gain/gate (Faust-reserved) plus our
    // MIDI-driven `bend`. Captured per voice (encounter order) so the wrapper can
    // drive a single voice (mono) or fan bend across all voices.
    std::map<juce::String, std::vector<FAUSTFLOAT*>> reservedByName;

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
        if (zone == nullptr || inProxyGroup())
            return;
        if (merged.slotIndex >= 0) {
            zonesByIdx[merged.slotIndex].push_back(zone);
        } else {
            const auto name = parsed.cleanLabel.toLowerCase();
            if (name == "freq" || name == "gain" || name == "gate" || name == "bend")
                reservedByName[name].push_back(zone);
        }
    }

    std::vector<juce::String> groupLabels_;
    std::map<FAUSTFLOAT*, ControlMetadata> pendingByZone_;
};

inline float midiNoteToHz(int note) {
    return 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
}

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
    sampleRate_ = sampleRate;

    // Wrap a fresh single-voice MagdaPolySynthDsp in the poly allocator.
    // control=true: voices are MIDI-allocated. group=false: each voice keeps its
    // own writable zones (we fan host-macro writes out to all of them).
    poly_ = std::make_unique<mydsp_poly>(new MagdaPolySynthDsp(), kNumVoices, /*control*/ true,
                                         /*group*/ false);
    poly_->init(sampleRate);
    numOutputs_ = poly_->getNumOutputs();

    // Dedicated single voice for Mono/Legato (driven directly, bypassing the
    // poly allocator which skips idle voices).
    monoVoice_.reset(new MagdaPolySynthDsp());
    monoVoice_->init(sampleRate);

    PolyVoiceHarvester polyH;
    poly_->buildUserInterface(&polyH);
    PolyVoiceHarvester monoH;
    monoVoice_->buildUserInterface(&monoH);

    for (auto& slot : voiceZonesBySlot_)
        slot.clear();
    monoZonesBySlot_.fill(nullptr);
    for (int i = 0; i < kHostSlotCount; ++i) {
        if (auto it = polyH.zonesByIdx.find(i); it != polyH.zonesByIdx.end())
            voiceZonesBySlot_[static_cast<size_t>(i)] = it->second;
        if (auto it = monoH.zonesByIdx.find(i); it != monoH.zonesByIdx.end() && !it->second.empty())
            monoZonesBySlot_[static_cast<size_t>(i)] = it->second.front();
    }

    auto first = [](const std::map<juce::String, std::vector<FAUSTFLOAT*>>& m,
                    const char* key) -> FAUSTFLOAT* {
        auto it = m.find(key);
        return (it != m.end() && !it->second.empty()) ? it->second.front() : nullptr;
    };
    voiceBendZones_.clear();
    if (auto it = polyH.reservedByName.find("bend"); it != polyH.reservedByName.end())
        voiceBendZones_ = it->second;
    monoBendZone_ = first(monoH.reservedByName, "bend");
    monoFreqZone_ = first(monoH.reservedByName, "freq");
    monoGainZone_ = first(monoH.reservedByName, "gain");
    monoGateZone_ = first(monoH.reservedByName, "gate");

    heldNotes_.clear();
    currentBend_ = 0.0f;
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
                                   .unit = "dB",
                                   .scale = magda::ParameterScale::FaderDB,
                                   .minValue = -60.0f,
                                   .maxValue = 6.0f,
                                   .defaultValue = (osc == 0) ? 0.0f : -60.0f};
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
    // Envelope times are in milliseconds (the formatter shows ms below 1 s, s
    // above). The DSP divides them back to seconds.
    hostSlotInfo_[kFilterAttackSlot] = {.name = "Filter Attack",
                                        .unit = "ms",
                                        .scale = magda::ParameterScale::Linear,
                                        .minValue = 1.0f,
                                        .maxValue = 2000.0f,
                                        .defaultValue = 5.0f};
    hostSlotInfo_[kFilterDecaySlot] = {.name = "Filter Decay",
                                       .unit = "ms",
                                       .scale = magda::ParameterScale::Linear,
                                       .minValue = 1.0f,
                                       .maxValue = 2000.0f,
                                       .defaultValue = 200.0f};
    hostSlotInfo_[kFilterSustainSlot] = {.name = "Filter Sustain",
                                         .scale = magda::ParameterScale::Linear,
                                         .minValue = 0.0f,
                                         .maxValue = 1.0f,
                                         .defaultValue = 0.7f};
    hostSlotInfo_[kFilterReleaseSlot] = {.name = "Filter Release",
                                         .unit = "ms",
                                         .scale = magda::ParameterScale::Linear,
                                         .minValue = 1.0f,
                                         .maxValue = 4000.0f,
                                         .defaultValue = 400.0f};

    hostSlotInfo_[kAmpAttackSlot] = {.name = "Amp Attack",
                                     .unit = "ms",
                                     .scale = magda::ParameterScale::Linear,
                                     .minValue = 1.0f,
                                     .maxValue = 2000.0f,
                                     .defaultValue = 5.0f};
    hostSlotInfo_[kAmpDecaySlot] = {.name = "Amp Decay",
                                    .unit = "ms",
                                    .scale = magda::ParameterScale::Linear,
                                    .minValue = 1.0f,
                                    .maxValue = 2000.0f,
                                    .defaultValue = 200.0f};
    hostSlotInfo_[kAmpSustainSlot] = {.name = "Amp Sustain",
                                      .scale = magda::ParameterScale::Linear,
                                      .minValue = 0.0f,
                                      .maxValue = 1.0f,
                                      .defaultValue = 0.7f};
    hostSlotInfo_[kAmpReleaseSlot] = {.name = "Amp Release",
                                      .unit = "ms",
                                      .scale = magda::ParameterScale::Linear,
                                      .minValue = 1.0f,
                                      .maxValue = 4000.0f,
                                      .defaultValue = 400.0f};

    hostSlotInfo_[kFilterDriveSlot] = {.name = "Filter Drive",
                                       .scale = magda::ParameterScale::Linear,
                                       .minValue = 0.0f,
                                       .maxValue = 1.0f,
                                       .defaultValue = 0.0f};

    hostSlotInfo_[kFilterSlopeSlot] = {.name = "Filter Slope",
                                       .scale = magda::ParameterScale::Discrete,
                                       .minValue = 0.0f,
                                       .maxValue = 1.0f,
                                       .defaultValue = 0.0f,
                                       .choices = {"12 dB", "24 dB"}};

    hostSlotInfo_[kBendRangeSlot] = {.name = "Bend Range",
                                     .unit =
                                         magda::technicalText(magda::TechnicalTextToken::Semitones),
                                     .scale = magda::ParameterScale::Linear,
                                     .minValue = 0.0f,
                                     .maxValue = 24.0f,
                                     .defaultValue = 2.0f};

    hostSlotInfo_[kVoiceModeSlot] = {.name = "Voice Mode",
                                     .scale = magda::ParameterScale::Discrete,
                                     .minValue = 0.0f,
                                     .maxValue = 2.0f,
                                     .defaultValue = 0.0f,
                                     .choices = {"Poly", "Mono", "Legato"}};

    hostSlotInfo_[kGlideSlot] = {.name = "Glide",
                                 .unit = "ms",
                                 .scale = magda::ParameterScale::Linear,
                                 .minValue = 0.0f,
                                 .maxValue = 2000.0f,
                                 .defaultValue = 0.0f};

    for (int osc = 0; osc < kNumOscillators; ++osc) {
        hostSlotInfo_[kOscResetBaseSlot + osc] = {.name = "Osc " + juce::String(osc + 1) + " Reset",
                                                  .scale = magda::ParameterScale::Discrete,
                                                  .minValue = 0.0f,
                                                  .maxValue = 1.0f,
                                                  .defaultValue = 0.0f,
                                                  .choices = {"Off", "On"}};
    }

    hostSlotInfo_[kVelAmpSlot] = {.name = "Vel Amp",
                                  .scale = magda::ParameterScale::Linear,
                                  .minValue = 0.0f,
                                  .maxValue = 1.0f,
                                  .defaultValue = 1.0f};
    hostSlotInfo_[kVelFilterSlot] = {.name = "Vel Filter",
                                     .unit = "oct",
                                     .scale = magda::ParameterScale::Linear,
                                     .minValue = 0.0f,
                                     .maxValue = 6.0f,
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
    heldNotes_.reserve(128);  // avoid RT allocation as notes are pressed
}

void MagdaPolySynthCompiledPlugin::deinitialise() {
    scratchOut_.setSize(0, 0);
    outPtrs_.clear();
}

void MagdaPolySynthCompiledPlugin::reset() {
    resetAllVoices();
}

int MagdaPolySynthCompiledPlugin::readVoiceModeIndex() const {
    if (!hostParams_[kVoiceModeSlot])
        return Poly;
    const float norm = hostParams_[kVoiceModeSlot]->getCurrentValue();
    return juce::jlimit(0, 2, static_cast<int>(std::lround(juce::jlimit(0.0f, 1.0f, norm) * 2.0f)));
}

void MagdaPolySynthCompiledPlugin::resetAllVoices() {
    if (poly_)
        poly_->ctrlChange(0, 123, 0);  // All Notes Off: release every poly voice
    if (monoVoice_)
        monoVoice_->instanceClear();
    heldNotes_.clear();
    if (monoGateZone_)
        *monoGateZone_ = 0.0f;
}

bool MagdaPolySynthCompiledPlugin::handleMonoNoteOn(int note, int velocity, int mode) {
    const float g = static_cast<float>(velocity) / 127.0f;
    const bool wasEmpty = heldNotes_.empty();
    heldNotes_.push_back({note, g});
    if (monoFreqZone_)
        *monoFreqZone_ = midiNoteToHz(note);
    if (monoGainZone_)
        *monoGainZone_ = g;
    if (wasEmpty) {
        if (monoGateZone_)
            *monoGateZone_ = 1.0f;  // gate 0 -> 1: clean attack from silence
        return false;
    }
    if (mode == Mono) {
        if (monoGateZone_)
            *monoGateZone_ = 0.0f;  // drop; caller raises after one sample to retrigger
        return true;
    }
    return false;  // Legato: change pitch only, envelope keeps running
}

void MagdaPolySynthCompiledPlugin::handleMonoNoteOff(int note) {
    for (auto it = heldNotes_.rbegin(); it != heldNotes_.rend(); ++it) {
        if (it->note == note) {
            heldNotes_.erase(std::next(it).base());
            break;
        }
    }
    if (heldNotes_.empty()) {
        if (monoGateZone_)
            *monoGateZone_ = 0.0f;  // last note released -> envelope release
    } else if (monoFreqZone_) {
        *monoFreqZone_ = midiNoteToHz(heldNotes_.back().note);  // legato return to held note
    }
}

void MagdaPolySynthCompiledPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!poly_ || !monoVoice_ || !fc.destBuffer || fc.bufferNumSamples <= 0)
        return;

    // Fan each host macro out to every poly voice's zone AND the mono voice's
    // zone (RT-safe pointer writes), so both engines track the controls.
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

        // Glide is a Mono/Legato-only control: the poly voices always get 0 so
        // reused voices never portamento from their previous note.
        const FAUSTFLOAT polyReal = (slot == kGlideSlot) ? FAUSTFLOAT(0) : real;
        for (FAUSTFLOAT* zone : voiceZonesBySlot_[static_cast<size_t>(slot)])
            if (zone)
                *zone = polyReal;
        if (monoZonesBySlot_[static_cast<size_t>(slot)])
            *monoZonesBySlot_[static_cast<size_t>(slot)] = real;
    }

    const int mode = readVoiceModeIndex();
    if (mode != lastVoiceMode_) {
        resetAllVoices();  // flush hung notes when switching Poly <-> Mono/Legato
        lastVoiceMode_ = mode;
    }

    // Refresh live pitch-bend into every target zone (persisted across blocks; a
    // fresh engine state or mode switch picks up the current wheel position).
    for (FAUSTFLOAT* z : voiceBendZones_)
        if (z)
            *z = currentBend_;
    if (monoBendZone_)
        *monoBendZone_ = currentBend_;

    const int n = fc.bufferNumSamples;
    const int start = fc.bufferStartSample;
    const int hostChannels = fc.destBuffer->getNumChannels();
    if (hostChannels <= 0 || numOutputs_ <= 0 || scratchOut_.getNumSamples() <= 0)
        return;

    ::dsp* active =
        (mode == Poly) ? static_cast<::dsp*>(poly_.get()) : static_cast<::dsp*>(monoVoice_.get());

    outPtrs_.resize(static_cast<size_t>(numOutputs_));
    const int scratchCh = scratchOut_.getNumChannels();
    const int maxChunk = std::min(MIX_BUFFER_SIZE, scratchOut_.getNumSamples());

    // Render [segStart, segStart+segLen) of this block from the active engine,
    // ADDing into destBuffer. Chunked to MIX_BUFFER_SIZE (the poly mix-buffer cap).
    auto renderSegment = [&](int segStart, int segLen) {
        int done = 0;
        while (done < segLen) {
            const int chunk = std::min(segLen - done, maxChunk);
            for (int ch = 0; ch < numOutputs_; ++ch)
                outPtrs_[static_cast<size_t>(ch)] = scratchOut_.getWritePointer(ch % scratchCh);
            active->compute(chunk, nullptr, outPtrs_.data());
            for (int ch = 0; ch < hostChannels; ++ch) {
                const int srcCh = (numOutputs_ == 1) ? 0 : (ch % numOutputs_);
                fc.destBuffer->addFrom(ch, start + segStart + done, scratchOut_, srcCh, 0, chunk);
            }
            done += chunk;
        }
    };

    // Walk MIDI in time order, rendering the audio between events so note timing
    // (and the mono retrigger gate edge) is sample-accurate within the block.
    int cursor = 0;
    if (fc.bufferForMidiMessages != nullptr) {
        for (auto& m : *fc.bufferForMidiMessages) {
            int evSample = juce::roundToInt(m.getTimeStamp() * sampleRate_);
            evSample = juce::jlimit(cursor, n, evSample);  // clamp + keep monotonic
            renderSegment(cursor, evSample - cursor);
            cursor = evSample;

            if (m.isPitchWheel()) {
                currentBend_ = (m.getPitchWheelValue() - 8192) / 8192.0f;
                for (FAUSTFLOAT* z : voiceBendZones_)
                    if (z)
                        *z = currentBend_;
                if (monoBendZone_)
                    *monoBendZone_ = currentBend_;
            } else if (mode == Poly) {
                if (m.isNoteOn())
                    poly_->keyOn(m.getChannel(), m.getNoteNumber(), m.getVelocity());
                else if (m.isNoteOff())
                    poly_->keyOff(m.getChannel(), m.getNoteNumber(), m.getVelocity());
                else if (m.isController())
                    poly_->ctrlChange(m.getChannel(), m.getControllerNumber(),
                                      m.getControllerValue());
            } else {
                if (m.isNoteOn()) {
                    if (handleMonoNoteOn(m.getNoteNumber(), m.getVelocity(), mode)) {
                        // One-sample gate-low renders the falling edge; raising the
                        // gate after it gives the rising edge that retriggers.
                        const int low = std::min(1, n - cursor);
                        renderSegment(cursor, low);
                        cursor += low;
                        if (monoGateZone_)
                            *monoGateZone_ = 1.0f;
                    }
                } else if (m.isNoteOff()) {
                    handleMonoNoteOff(m.getNoteNumber());
                } else if (m.isController() &&
                           (m.getControllerNumber() == 120 || m.getControllerNumber() == 123)) {
                    heldNotes_.clear();  // all-sound/all-notes-off
                    if (monoGateZone_)
                        *monoGateZone_ = 0.0f;
                }
            }
        }
    }
    renderSegment(cursor, n - cursor);
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
