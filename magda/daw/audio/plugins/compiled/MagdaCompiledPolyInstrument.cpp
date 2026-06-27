#include "plugins/compiled/MagdaCompiledPolyInstrument.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/ParameterUtils.hpp"
#include "faust/dsp/dsp.h"
#include "faust/dsp/poly-dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "plugins/FaustMetadataParser.hpp"

// NOTE: Faust's GUI base statics (GUI::fGuiList / gTimedZoneMap), required by
// poly-dsp.h, are defined once in FaustPolyGuiStatics.cpp — see that file.

namespace magda::daw::audio::compiled {

namespace {

// ---------------------------------------------------------------------------
// Per-voice [idx:N] zone harvester (identical strategy to MagdaPolySynth).
// mydsp_poly built with group=false emits a shared "Voices" proxy box followed
// by one "Voice<n>" box per voice; we keep the per-voice zones and group them
// by idx so a single host slot can fan a write out to every voice.
// ---------------------------------------------------------------------------
class PolyVoiceHarvester : public ::UI {
  public:
    std::map<int, std::vector<FAUSTFLOAT*>> zonesByIdx;
    // Reserved (no [idx]) controls, captured for the dedicated mono voice.
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
            if (name == "freq" || name == "gain" || name == "gate")
                reservedByName[name].push_back(zone);
        }
    }

    std::vector<juce::String> groupLabels_;
    std::map<FAUSTFLOAT*, ControlMetadata> pendingByZone_;
};

constexpr float kCeiling = 0.9f;  // hard output ceiling

inline float midiNoteToHz(int note) {
    return 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
}

}  // namespace

// ===========================================================================

MagdaCompiledPolyInstrument::MagdaCompiledPolyInstrument(const te::PluginCreationInfo& info)
    : te::Plugin(info) {}

MagdaCompiledPolyInstrument::~MagdaCompiledPolyInstrument() {
    notifyListenersOfDeletion();
    for (auto& p : hostParams_)
        if (p)
            p->detachFromCurrentValue();
}

void MagdaCompiledPolyInstrument::initInstrument() {
    voiceSlotInfos_ = voiceSlotInfos();
    constexpr int kProvisionalSampleRate = 44100;
    rebuildEngineState(kProvisionalSampleRate);
    buildHostParameters();
}

void MagdaCompiledPolyInstrument::rebuildEngineState(int sampleRate) {
    sampleRate_ = sampleRate;
    poly_ = std::make_unique<mydsp_poly>(createVoiceDsp(), numVoices(), /*control*/ true,
                                         /*group*/ false);
    poly_->init(sampleRate);
    numOutputs_ = poly_->getNumOutputs();

    PolyVoiceHarvester harvester;
    poly_->buildUserInterface(&harvester);

    voiceZonesBySlot_.assign(static_cast<size_t>(voiceSlotCount()), {});
    for (int i = 0; i < voiceSlotCount(); ++i)
        if (auto it = harvester.zonesByIdx.find(i); it != harvester.zonesByIdx.end())
            voiceZonesBySlot_[static_cast<size_t>(i)] = it->second;

    // Dedicated mono voice (driven directly; the poly allocator skips idle
    // voices). Harvest its reserved freq/gain/gate plus its copy of each voice
    // macro zone.
    monoZonesBySlot_.assign(static_cast<size_t>(voiceSlotCount()), nullptr);
    monoFreqZone_ = monoGainZone_ = monoGateZone_ = nullptr;
    if (hasVoiceModes()) {
        monoVoice_.reset(createVoiceDsp());
        monoVoice_->init(sampleRate);
        PolyVoiceHarvester monoH;
        monoVoice_->buildUserInterface(&monoH);
        for (int i = 0; i < voiceSlotCount(); ++i)
            if (auto it = monoH.zonesByIdx.find(i);
                it != monoH.zonesByIdx.end() && !it->second.empty())
                monoZonesBySlot_[static_cast<size_t>(i)] = it->second.front();
        auto first = [](const std::map<juce::String, std::vector<FAUSTFLOAT*>>& mp,
                        const char* key) -> FAUSTFLOAT* {
            auto it = mp.find(key);
            return (it != mp.end() && !it->second.empty()) ? it->second.front() : nullptr;
        };
        monoFreqZone_ = first(monoH.reservedByName, "freq");
        monoGainZone_ = first(monoH.reservedByName, "gain");
        monoGateZone_ = first(monoH.reservedByName, "gate");
    } else {
        monoVoice_.reset();
    }
    heldNotes_.clear();
}

magda::ParameterInfo MagdaCompiledPolyInstrument::infoForSlot(int slotIndex) const {
    magda::ParameterInfo info;
    const auto& s =
        hostSlotInfo_[static_cast<size_t>(juce::jlimit(0, hostSlotCountValue() - 1, slotIndex))];
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

float MagdaCompiledPolyInstrument::slotRealValue(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= hostSlotCountValue() ||
        !hostParams_[static_cast<size_t>(slotIndex)])
        return 0.0f;
    const float norm = hostParams_[static_cast<size_t>(slotIndex)]->getCurrentValue();
    return magda::ParameterUtils::normalizedToReal(norm, infoForSlot(slotIndex));
}

void MagdaCompiledPolyInstrument::buildHostParameters() {
    using magda::ParameterScale;

    // [voice macros ...] then the control slots: Gain, then Voice Mode (if any).
    hostSlotInfo_ = voiceSlotInfos_;
    hostSlotInfo_.resize(static_cast<size_t>(hostSlotCountValue()));
    hostSlotInfo_[static_cast<size_t>(gainSlot())] = {.name = "Gain",
                                                      .unit = "dB",
                                                      .scale = ParameterScale::Linear,
                                                      .minValue = -24.0f,
                                                      .maxValue = 6.0f,
                                                      .defaultValue = -6.0f};
    if (hasVoiceModes())
        hostSlotInfo_[static_cast<size_t>(voiceModeSlot())] = {
            .name = "Voice Mode",
            .scale = ParameterScale::Discrete,
            .minValue = 0.0f,
            .maxValue = 2.0f,
            .defaultValue = 0.0f,
            .choices = {"Poly", "Mono", "Legato"}};

    juce::NormalisableRange<float> normalisedRange{0.0f, 1.0f};
    auto* undoManager = getUndoManager();

    hostParams_.assign(static_cast<size_t>(hostSlotCountValue()), nullptr);
    hostCached_.clear();
    hostCached_.resize(static_cast<size_t>(hostSlotCountValue()));

    const juce::String prefix = slotIdPrefix();
    for (int i = 0; i < hostSlotCountValue(); ++i) {
        const auto& slot = hostSlotInfo_[static_cast<size_t>(i)];
        const juce::String id = prefix + slot.name.toLowerCase().replace(" ", "_");
        const juce::Identifier identifier(id);
        const auto info = infoForSlot(i);
        const float defaultNormalized =
            magda::ParameterUtils::realToNormalized(slot.defaultValue, info);
        hostCached_[static_cast<size_t>(i)].referTo(state, identifier, undoManager,
                                                    defaultNormalized);

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
        param->attachToCurrentValue(hostCached_[static_cast<size_t>(i)]);
        hostParams_[static_cast<size_t>(i)] = param;
    }
}

void MagdaCompiledPolyInstrument::initialise(const te::PluginInitialisationInfo& info) {
    rebuildEngineState(static_cast<int>(info.sampleRate));
    scratchOut_.setSize(std::max(numOutputs_, 2), info.blockSizeSamples, false, true, true);
    outPtrs_.assign(static_cast<size_t>(std::max(numOutputs_, 2)), nullptr);
    limEnv_ = 0.0f;
}

void MagdaCompiledPolyInstrument::deinitialise() {
    scratchOut_.setSize(0, 0);
    outPtrs_.clear();
}

int MagdaCompiledPolyInstrument::readVoiceModeIndex() const {
    const int slot = voiceModeSlot();
    if (slot < 0)
        return Poly;
    return juce::jlimit(0, 2, static_cast<int>(std::lround(slotRealValue(slot))));
}

void MagdaCompiledPolyInstrument::resetAllVoices() {
    limEnv_ = 0.0f;
    if (poly_)
        poly_->ctrlChange(0, 123, 0);  // All Notes Off
    if (monoVoice_)
        monoVoice_->instanceClear();
    heldNotes_.clear();
    if (monoGateZone_)
        *monoGateZone_ = 0.0f;
}

bool MagdaCompiledPolyInstrument::handleMonoNoteOn(int note, int velocity, int mode) {
    const float g = static_cast<float>(velocity) / 127.0f;
    const bool wasEmpty = heldNotes_.empty();
    heldNotes_.push_back({note, g});
    if (monoFreqZone_)
        *monoFreqZone_ = midiNoteToHz(note);
    if (monoGainZone_)
        *monoGainZone_ = g;
    if (wasEmpty) {
        if (monoGateZone_)
            *monoGateZone_ = 1.0f;  // clean attack from silence
        return false;
    }
    if (mode == Mono) {
        if (monoGateZone_)
            *monoGateZone_ = 0.0f;  // drop; caller raises after one sample to retrigger
        return true;
    }
    return false;  // Legato: change pitch only, envelope keeps running
}

void MagdaCompiledPolyInstrument::handleMonoNoteOff(int note) {
    for (auto it = heldNotes_.rbegin(); it != heldNotes_.rend(); ++it)
        if (it->note == note) {
            heldNotes_.erase(std::next(it).base());
            break;
        }
    if (heldNotes_.empty()) {
        if (monoGateZone_)
            *monoGateZone_ = 0.0f;  // last note released -> envelope release
    } else if (monoFreqZone_) {
        *monoFreqZone_ = midiNoteToHz(heldNotes_.back().note);  // legato return
    }
}

void MagdaCompiledPolyInstrument::reset() {
    resetAllVoices();
}

void MagdaCompiledPolyInstrument::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!poly_ || !fc.destBuffer || fc.bufferNumSamples <= 0)
        return;

    // Fan each voice macro out to every poly voice (Glide forced to 0 so reused
    // voices never portamento) and to the mono voice (real value, incl. Glide).
    for (int slot = 0; slot < voiceSlotCount(); ++slot) {
        const auto real = static_cast<FAUSTFLOAT>(slotRealValue(slot));
        const FAUSTFLOAT polyReal = (slot == glideVoiceSlot()) ? FAUSTFLOAT(0) : real;
        for (FAUSTFLOAT* zone : voiceZonesBySlot_[static_cast<size_t>(slot)])
            if (zone)
                *zone = polyReal;
        if (hasVoiceModes() && monoZonesBySlot_[static_cast<size_t>(slot)])
            *monoZonesBySlot_[static_cast<size_t>(slot)] = real;
    }

    const int n = fc.bufferNumSamples;
    const int start = fc.bufferStartSample;
    const int hostChannels = fc.destBuffer->getNumChannels();
    if (hostChannels <= 0 || numOutputs_ <= 0 || scratchOut_.getNumSamples() <= 0)
        return;

    const int mode = (hasVoiceModes() && monoVoice_) ? readVoiceModeIndex() : Poly;
    if (mode != lastVoiceMode_) {
        resetAllVoices();  // flush hung notes when switching Poly <-> Mono/Legato
        lastVoiceMode_ = mode;
    }
    ::dsp* active =
        (mode == Poly) ? static_cast<::dsp*>(poly_.get()) : static_cast<::dsp*>(monoVoice_.get());

    const float gain = juce::Decibels::decibelsToGain(slotRealValue(gainSlot()));
    outPtrs_.resize(static_cast<size_t>(numOutputs_));
    const int scratchCh = scratchOut_.getNumChannels();
    const int maxChunk = std::min(MIX_BUFFER_SIZE, scratchOut_.getNumSamples());

    // Render [segStart, segStart+segLen) from the active engine, apply gain + peak
    // limiter + NaN panic, and ADD into destBuffer. Chunked to MIX_BUFFER_SIZE.
    auto renderSegment = [&](int segStart, int segLen) {
        int done = 0;
        while (done < segLen) {
            const int chunk = std::min(segLen - done, maxChunk);
            for (int ch = 0; ch < numOutputs_; ++ch)
                outPtrs_[static_cast<size_t>(ch)] = scratchOut_.getWritePointer(ch % scratchCh);
            active->compute(chunk, nullptr, outPtrs_.data());

            for (int i = 0; i < chunk; ++i) {
                float mono = scratchOut_.getSample(0, i) * gain;
                if (!std::isfinite(mono)) {
                    limEnv_ = 0.0f;
                    active->instanceClear();
                    for (int ch = 0; ch < numOutputs_; ++ch)
                        scratchOut_.setSample(ch, i, 0.0f);
                    continue;
                }
                limEnv_ = juce::jmax(std::abs(mono), limEnv_ * 0.9997f);
                const float factor = (limEnv_ > kCeiling) ? (kCeiling / limEnv_) : 1.0f;
                for (int ch = 0; ch < numOutputs_; ++ch) {
                    float v = scratchOut_.getSample(ch, i) * gain * factor;
                    v = juce::jlimit(-kCeiling, kCeiling, v);
                    scratchOut_.setSample(ch, i, v);
                }
            }

            for (int ch = 0; ch < hostChannels; ++ch) {
                const int srcCh = (numOutputs_ == 1) ? 0 : (ch % numOutputs_);
                fc.destBuffer->addFrom(ch, start + segStart + done, scratchOut_, srcCh, 0, chunk);
            }
            done += chunk;
        }
    };

    // Walk MIDI in time order, rendering audio between events so note timing (and
    // the Mono retrigger gate edge) is sample-accurate within the block.
    int cursor = 0;
    if (fc.bufferForMidiMessages != nullptr) {
        for (auto& m : *fc.bufferForMidiMessages) {
            int evSample = juce::roundToInt(m.getTimeStamp() * sampleRate_);
            evSample = juce::jlimit(cursor, n, evSample);  // clamp + keep monotonic
            renderSegment(cursor, evSample - cursor);
            cursor = evSample;

            if (mode == Poly) {
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
                        // One-sample gate-low renders the falling edge; raising it
                        // again gives the rising edge that retriggers.
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
                    heldNotes_.clear();
                    if (monoGateZone_)
                        *monoGateZone_ = 0.0f;
                }
            }
        }
    }
    renderSegment(cursor, n - cursor);
}

te::AutomatableParameter* MagdaCompiledPolyInstrument::getSlotParameter(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= hostSlotCountValue())
        return nullptr;
    return hostParams_[static_cast<size_t>(slotIndex)].get();
}

const MagdaCompiledPolyInstrument::HostSlotInfo& MagdaCompiledPolyInstrument::getSlotInfo(
    int slotIndex) const {
    static const HostSlotInfo kEmpty;
    if (slotIndex < 0 || slotIndex >= hostSlotCountValue())
        return kEmpty;
    return hostSlotInfo_[static_cast<size_t>(slotIndex)];
}

float MagdaCompiledPolyInstrument::displayValueToNativeValue(int slotIndex,
                                                             float displayValue) const {
    if (slotIndex < 0 || slotIndex >= hostSlotCountValue())
        return displayValue;
    return magda::ParameterUtils::realToNormalized(displayValue, infoForSlot(slotIndex));
}

float MagdaCompiledPolyInstrument::nativeValueToDisplayValue(int slotIndex,
                                                             float nativeValue) const {
    if (slotIndex < 0 || slotIndex >= hostSlotCountValue())
        return nativeValue;
    return magda::ParameterUtils::normalizedToReal(nativeValue, infoForSlot(slotIndex));
}

}  // namespace magda::daw::audio::compiled
