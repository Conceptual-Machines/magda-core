#include "plugins/compiled/MagdaStrumInstrument.hpp"

#include <algorithm>
#include <array>
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
        if (inProxyGroup() || merged.slotIndex < 0 || zone == nullptr)
            return;
        zonesByIdx[merged.slotIndex].push_back(zone);
    }

    std::vector<juce::String> groupLabels_;
    std::map<FAUSTFLOAT*, ControlMetadata> pendingByZone_;
};

// ---------------------------------------------------------------------------
// Strum curve (ported from Retrospect PluckProcessor). Cubic-Bezier shape
// presets (some non-monotonic) rasterized into a LUT, then tiled by `cycles`.
// Placeholder for the real drawable StepClock curve editor.
// ---------------------------------------------------------------------------
struct Shape {
    const char* name;
    float c1x, c1y, c2x, c2y;
};

const std::array<Shape, 8>& shapes() {
    static const std::array<Shape, 8> s{{
        {"Linear", 0.33f, 0.33f, 0.66f, 0.66f},
        {"Ease In", 0.62f, 0.03f, 0.96f, 0.40f},
        {"Ease Out", 0.04f, 0.60f, 0.38f, 0.97f},
        {"Snap", 0.02f, 0.85f, 0.18f, 1.00f},
        {"Spike", 0.82f, 0.00f, 0.98f, 0.18f},
        {"S-Curve", 0.70f, 0.00f, 0.30f, 1.00f},
        {"Overshoot", 0.55f, -0.45f, 0.45f, 1.45f},
        {"Bounce", 0.30f, 1.40f, 0.70f, -0.40f},
    }};
    return s;
}

float bez1(float a, float b, float c, float d, float s) noexcept {
    const float m = 1.0f - s;
    return m * m * m * a + 3.0f * m * m * s * b + 3.0f * m * s * s * c + s * s * s * d;
}

void buildLut(int shapeIdx, std::array<float, 1024>& lut) {
    const auto& sh = shapes()[static_cast<size_t>(juce::jlimit(0, 7, shapeIdx))];
    constexpr int K = 512;
    std::array<float, K + 1> xs{}, ys{};
    for (int k = 0; k <= K; ++k) {
        const float s = static_cast<float>(k) / K;
        xs[static_cast<size_t>(k)] = bez1(0.0f, sh.c1x, sh.c2x, 1.0f, s);
        ys[static_cast<size_t>(k)] = bez1(0.0f, sh.c1y, sh.c2y, 1.0f, s);
    }
    int si = 0;
    for (int i = 0; i < 1024; ++i) {
        const float x = static_cast<float>(i) / 1023.0f;
        while (si + 1 <= K && xs[static_cast<size_t>(si + 1)] < x)
            ++si;
        const int j = std::min(si + 1, K);
        const float span = xs[static_cast<size_t>(j)] - xs[static_cast<size_t>(si)];
        const float f = span > 1.0e-6f ? (x - xs[static_cast<size_t>(si)]) / span : 0.0f;
        const float y = ys[static_cast<size_t>(si)] +
                        f * (ys[static_cast<size_t>(j)] - ys[static_cast<size_t>(si)]);
        lut[static_cast<size_t>(i)] = juce::jlimit(-0.5f, 1.5f, y);
    }
}

float sampleLut(const std::array<float, 1024>& lut, float t) noexcept {
    t = juce::jlimit(0.0f, 1.0f, t);
    const float p = t * 1023.0f;
    const int i0 = static_cast<int>(p);
    const int i1 = std::min(i0 + 1, 1023);
    const float fr = p - static_cast<float>(i0);
    return lut[static_cast<size_t>(i0)] * (1.0f - fr) + lut[static_cast<size_t>(i1)] * fr;
}

// `cycles` tiled copies of the curve across [0,1] → repeated mini-strums.
float sampleCycled(const std::array<float, 1024>& lut, float u, int cycles) noexcept {
    cycles = juce::jmax(1, cycles);
    const float t = juce::jlimit(0.0f, 1.0f, u) * static_cast<float>(cycles);
    const int k = std::min(static_cast<int>(t), cycles - 1);
    return (static_cast<float>(k) + sampleLut(lut, t - static_cast<float>(k))) /
           static_cast<float>(cycles);
}

constexpr float kCeiling = 0.9f;  // hard output ceiling

}  // namespace

// ===========================================================================

MagdaStrumInstrument::MagdaStrumInstrument(const te::PluginCreationInfo& info) : te::Plugin(info) {}

MagdaStrumInstrument::~MagdaStrumInstrument() {
    notifyListenersOfDeletion();
    for (auto& p : hostParams_)
        if (p)
            p->detachFromCurrentValue();
}

void MagdaStrumInstrument::initInstrument() {
    voiceSlotInfos_ = voiceSlotInfos();
    constexpr int kProvisionalSampleRate = 44100;
    rebuildEngineState(kProvisionalSampleRate);
    buildHostParameters();
    held_.reserve(16);
    pending_.reserve(128);
    buildLut(0, lut_);
    lutShape_ = 0;
}

void MagdaStrumInstrument::rebuildEngineState(int sampleRate) {
    currentSampleRate_ = sampleRate;
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
}

magda::ParameterInfo MagdaStrumInstrument::infoForSlot(int slotIndex) const {
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

float MagdaStrumInstrument::slotRealValue(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= hostSlotCountValue() ||
        !hostParams_[static_cast<size_t>(slotIndex)])
        return 0.0f;
    const float norm = hostParams_[static_cast<size_t>(slotIndex)]->getCurrentValue();
    return magda::ParameterUtils::normalizedToReal(norm, infoForSlot(slotIndex));
}

void MagdaStrumInstrument::buildHostParameters() {
    using magda::ParameterScale;

    // [voice macros ...] then the shared control slots.
    hostSlotInfo_ = voiceSlotInfos_;
    hostSlotInfo_.resize(static_cast<size_t>(hostSlotCountValue()));

    hostSlotInfo_[static_cast<size_t>(controlSlot(kTrigger))] = {.name = "Trigger",
                                                                 .scale = ParameterScale::Discrete,
                                                                 .minValue = 0.0f,
                                                                 .maxValue = 1.0f,
                                                                 .defaultValue = 0.0f,
                                                                 .choices = {"Chord", "Sync"}};
    hostSlotInfo_[static_cast<size_t>(controlSlot(kOrder))] = {
        .name = "Order",
        .scale = ParameterScale::Discrete,
        .minValue = 0.0f,
        .maxValue = 3.0f,
        .defaultValue = 0.0f,
        .choices = {"Up", "Down", "Up-Down", "As Played"}};
    hostSlotInfo_[static_cast<size_t>(controlSlot(kShape))] = {
        .name = "Shape",
        .scale = ParameterScale::Discrete,
        .minValue = 0.0f,
        .maxValue = 7.0f,
        .defaultValue = 1.0f,
        .choices = {"Linear", "Ease In", "Ease Out", "Snap", "Spike", "S-Curve", "Overshoot",
                    "Bounce"}};
    hostSlotInfo_[static_cast<size_t>(controlSlot(kCycles))] = {
        .name = "Cycles",
        .scale = ParameterScale::Discrete,
        .minValue = 0.0f,
        .maxValue = 7.0f,
        .defaultValue = 0.0f,
        .choices = {"1", "2", "3", "4", "5", "6", "7", "8"}};
    hostSlotInfo_[static_cast<size_t>(controlSlot(kStrumLen))] = {.name = "Strum Length",
                                                                  .unit = "ms",
                                                                  .scale = ParameterScale::Linear,
                                                                  .minValue = 1.0f,
                                                                  .maxValue = 400.0f,
                                                                  .defaultValue = 90.0f,
                                                                  .scaleAnchor = 40.0f};
    hostSlotInfo_[static_cast<size_t>(controlSlot(kSyncInterval))] = {.name = "Sync Interval",
                                                                      .unit = "ms",
                                                                      .scale =
                                                                          ParameterScale::Linear,
                                                                      .minValue = 60.0f,
                                                                      .maxValue = 2000.0f,
                                                                      .defaultValue = 500.0f,
                                                                      .scaleAnchor = 400.0f};
    hostSlotInfo_[static_cast<size_t>(controlSlot(kGain))] = {.name = "Gain",
                                                              .unit = "dB",
                                                              .scale = ParameterScale::Linear,
                                                              .minValue = -24.0f,
                                                              .maxValue = 6.0f,
                                                              .defaultValue = -6.0f};

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

void MagdaStrumInstrument::initialise(const te::PluginInitialisationInfo& info) {
    rebuildEngineState(static_cast<int>(info.sampleRate));
    scratchOut_.setSize(std::max(numOutputs_, 2), info.blockSizeSamples, false, true, true);
    outPtrs_.assign(static_cast<size_t>(std::max(numOutputs_, 2)), nullptr);
    held_.clear();
    pending_.clear();
    clock_ = 0;
    collectLeft_ = -1;
    syncLeft_ = 0;
    limEnv_ = 0.0f;
}

void MagdaStrumInstrument::deinitialise() {
    scratchOut_.setSize(0, 0);
    outPtrs_.clear();
}

void MagdaStrumInstrument::reset() {
    panic();
    if (poly_)
        poly_->instanceClear();
}

void MagdaStrumInstrument::handleMidi(const te::MidiMessageArray& midi) {
    for (const auto& m : midi) {
        if (m.isNoteOn()) {
            const int note = m.getNoteNumber();
            held_.erase(std::remove_if(held_.begin(), held_.end(),
                                       [note](const Held& h) { return h.note == note; }),
                        held_.end());
            held_.push_back({note, m.getFloatVelocity(), noteOrder_++});
            collectLeft_ = juce::jmax(1, static_cast<int>(0.03 * currentSampleRate_));
        } else if (m.isNoteOff()) {
            const int note = m.getNoteNumber();
            held_.erase(std::remove_if(held_.begin(), held_.end(),
                                       [note](const Held& h) { return h.note == note; }),
                        held_.end());
        } else if (m.isAllNotesOff() || m.isAllSoundOff()) {
            held_.clear();
        }
    }
}

void MagdaStrumInstrument::scheduleStrum() {
    if (held_.empty())
        return;
    std::vector<Held> notes = held_;
    const int order = static_cast<int>(slotRealValue(controlSlot(kOrder)));
    if (order == 0)  // Up
        std::sort(notes.begin(), notes.end(),
                  [](const Held& a, const Held& b) { return a.note < b.note; });
    else if (order == 1)  // Down
        std::sort(notes.begin(), notes.end(),
                  [](const Held& a, const Held& b) { return a.note > b.note; });
    else if (order == 2) {  // Up-Down
        std::sort(notes.begin(), notes.end(),
                  [](const Held& a, const Held& b) { return a.note < b.note; });
        for (int i = static_cast<int>(notes.size()) - 2; i >= 1; --i)
            notes.push_back(notes[static_cast<size_t>(i)]);
    } else  // As Played
        std::sort(notes.begin(), notes.end(),
                  [](const Held& a, const Held& b) { return a.order < b.order; });

    const int N = static_cast<int>(notes.size());
    const float W =
        slotRealValue(controlSlot(kStrumLen)) * 0.001f * static_cast<float>(currentSampleRate_);
    const int cyc =
        static_cast<int>(slotRealValue(controlSlot(kCycles))) + 1;  // Discrete index 0..7 → 1..8
    const int gateSamples = juce::jmax(1, static_cast<int>(0.006 * currentSampleRate_));

    for (int i = 0; i < N; ++i) {
        const float u = (N == 1) ? 0.0f : static_cast<float>(i) / static_cast<float>(N - 1);
        const float onset = juce::jlimit(0.0f, W, sampleCycled(lut_, u, cyc) * W);
        const int note = notes[static_cast<size_t>(i)].note;
        const int vel = juce::jlimit(
            1, 127, static_cast<int>(std::round(notes[static_cast<size_t>(i)].vel * 127.0f)));
        const std::int64_t at = clock_ + static_cast<std::int64_t>(onset);
        pending_.push_back({at, note, vel, true});
        pending_.push_back({at + gateSamples, note, 0, false});
    }
}

void MagdaStrumInstrument::fireDuePlucks() {
    if (!poly_)
        return;
    for (size_t i = 0; i < pending_.size();) {
        if (pending_[i].fireAt <= clock_) {
            const auto p = pending_[i];
            if (p.gateOn)
                poly_->keyOn(0, p.note, p.velocity);
            else
                poly_->keyOff(0, p.note, 0);
            pending_[i] = pending_.back();
            pending_.pop_back();
        } else {
            ++i;
        }
    }
}

void MagdaStrumInstrument::panic() {
    pending_.clear();
    limEnv_ = 0.0f;
    if (poly_)
        poly_->instanceClear();
}

void MagdaStrumInstrument::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!poly_ || !fc.destBuffer || fc.bufferNumSamples <= 0)
        return;

    // Rebuild the strum-curve LUT when the Shape slot changes.
    const int shp = static_cast<int>(slotRealValue(controlSlot(kShape)));
    if (shp != lutShape_) {
        buildLut(shp, lut_);
        lutShape_ = shp;
    }

    // Fan each voice macro out to every voice's zone (RT-safe pointer writes).
    for (int slot = 0; slot < voiceSlotCount(); ++slot) {
        const auto real = static_cast<FAUSTFLOAT>(slotRealValue(slot));
        for (FAUSTFLOAT* zone : voiceZonesBySlot_[static_cast<size_t>(slot)])
            if (zone)
                *zone = real;
    }

    const int numSamples = fc.bufferNumSamples;

    // Latch the held chord from this block's MIDI.
    if (fc.bufferForMidiMessages != nullptr)
        handleMidi(*fc.bufferForMidiMessages);

    // Trigger logic (uses clock_ at the block start as the strum base).
    const bool chordMode = static_cast<int>(slotRealValue(controlSlot(kTrigger))) == 0;
    if (chordMode) {
        if (collectLeft_ >= 0) {
            collectLeft_ -= numSamples;
            if (collectLeft_ <= 0) {
                scheduleStrum();
                collectLeft_ = -1;
            }
        }
    } else {
        syncLeft_ -= numSamples;
        if (syncLeft_ <= 0) {
            scheduleStrum();
            const float ms = slotRealValue(controlSlot(kSyncInterval));
            syncLeft_ = juce::jmax(1, static_cast<int>(ms * 0.001f * currentSampleRate_));
        }
    }

    const int start = fc.bufferStartSample;
    const int hostChannels = fc.destBuffer->getNumChannels();
    if (hostChannels <= 0 || numOutputs_ <= 0 || scratchOut_.getNumSamples() <= 0)
        return;

    const float gain = juce::Decibels::decibelsToGain(slotRealValue(controlSlot(kGain)));
    outPtrs_.resize(static_cast<size_t>(numOutputs_));
    const int maxChunk = std::min(MIX_BUFFER_SIZE, scratchOut_.getNumSamples());

    // Render the block in sub-chunks split at scheduled onset boundaries so
    // notes fire sample-accurately. mydsp_poly's mix buffers cap at MIX_BUFFER_SIZE.
    int produced = 0;
    while (produced < numSamples) {
        fireDuePlucks();

        // Samples until the next pending onset (bounds this chunk).
        std::int64_t nextDelta = numSamples - produced;
        for (const auto& p : pending_) {
            const std::int64_t d = p.fireAt - clock_;
            if (d > 0 && d < nextDelta)
                nextDelta = d;
        }
        const int chunk = std::min({static_cast<int>(nextDelta), maxChunk, numSamples - produced});

        for (int ch = 0; ch < numOutputs_; ++ch)
            outPtrs_[static_cast<size_t>(ch)] =
                scratchOut_.getWritePointer(ch % scratchOut_.getNumChannels());

        poly_->compute(chunk, nullptr, outPtrs_.data());

        // Output stage: gain + instantaneous peak limiter + NaN panic, applied
        // to our contribution before it is added into the destination buffer.
        for (int i = 0; i < chunk; ++i) {
            float mono = scratchOut_.getSample(0, i) * gain;
            if (!std::isfinite(mono)) {
                panic();
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
            fc.destBuffer->addFrom(ch, start + produced, scratchOut_, srcCh, 0, chunk);
        }

        clock_ += chunk;
        produced += chunk;
    }
}

te::AutomatableParameter* MagdaStrumInstrument::getSlotParameter(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= hostSlotCountValue())
        return nullptr;
    return hostParams_[static_cast<size_t>(slotIndex)].get();
}

const MagdaStrumInstrument::HostSlotInfo& MagdaStrumInstrument::getSlotInfo(int slotIndex) const {
    static const HostSlotInfo kEmpty;
    if (slotIndex < 0 || slotIndex >= hostSlotCountValue())
        return kEmpty;
    return hostSlotInfo_[static_cast<size_t>(slotIndex)];
}

float MagdaStrumInstrument::displayValueToNativeValue(int slotIndex, float displayValue) const {
    if (slotIndex < 0 || slotIndex >= hostSlotCountValue())
        return displayValue;
    return magda::ParameterUtils::realToNormalized(displayValue, infoForSlot(slotIndex));
}

float MagdaStrumInstrument::nativeValueToDisplayValue(int slotIndex, float nativeValue) const {
    if (slotIndex < 0 || slotIndex >= hostSlotCountValue())
        return nativeValue;
    return magda::ParameterUtils::normalizedToReal(nativeValue, infoForSlot(slotIndex));
}

}  // namespace magda::daw::audio::compiled
