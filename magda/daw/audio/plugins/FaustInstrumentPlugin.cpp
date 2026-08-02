#include "plugins/FaustInstrumentPlugin.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>

#include "FaustBackend.hpp"
#include "FaustResources.hpp"
#include "FaustUIHarvester.hpp"
#include "faust/dsp/poly-dsp.h"

// NOTE: Faust's GUI base statics (GUI::fGuiList / gTimedZoneMap), required by
// poly-dsp.h, are defined once in FaustPolyGuiStatics.cpp so multiple poly TUs
// link without duplicate symbols.

namespace magda::daw::audio {

const char* FaustInstrumentPlugin::xmlTypeName = "faust-instrument";

namespace {

inline float midiNoteToHz(int note) {
    return 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
}

// 14-bit wheel to -1..+1. 8192 is centre, so the two halves have a different
// span (8192 below, 8191 above); scaling each side by its own span keeps the
// extremes at exactly -1 and +1 instead of leaving the top a hair short.
inline float bendFromWheel(int wheelValue) {
    const int offset = wheelValue - 8192;
    return offset < 0 ? static_cast<float>(offset) / 8192.0f : static_cast<float>(offset) / 8191.0f;
}

// Glide is stepped once per sub-block rather than per sample: the mono voice's
// freq zone is a plain float the DSP reads at the top of compute(), so the only
// way to move it mid-block is to break the block up. 32 samples is 0.7 ms at
// 48 kHz - far finer than the ear resolves on a portamento, and cheap enough
// that chopping a block into that many compute() calls does not show up.
constexpr int kGlideChunkSamples = 32;

// Below this the glide is over: a target within a cent of the current pitch is
// inaudible, and stopping there keeps the ramp from creeping forever.
constexpr float kGlideDoneRatio = 1.0006f;  // ~1 cent

// Reserved Faust control labels: the polyphonic voice allocator drives these
// per-voice from MIDI note/velocity/gate, so they are never exposed as
// user-editable pool parameters.
bool isReservedVoiceControl(const juce::String& cleanLabel) {
    const auto l = cleanLabel.trim().toLowerCase();
    return l == "freq" || l == "gain" || l == "gate";
}

// Default polyphonic synth used when a fresh instrument is created without a
// saved .dsp. Follows the Faust poly convention: freq/gain/gate are the
// reserved per-voice MIDI controls; cutoff/resonance/attack/release are the
// user-editable tone controls harvested into the pool. Mono voice fanned to
// stereo with `<: _,_`.
// The user controls are wrapped in vgroup() boxes (Osc / Filter / Env) so the
// instrument's tabbed UI gets one tab per group. Each group's controls are
// declared inside the box via `with{}` so Faust composes them under that group.
//
// Mirrored by faust_dsp/runtime/instruments/synth/simple_synth.dsp, which
// is what puts it in the Load menu - keep the two in step. This copy stays
// compiled in rather than being read from that file, so a device still comes up
// making sound when nothing is staged (a dev build, a broken install).
constexpr const char* kDefaultDspSource = R"FAUST(
import("stdfaust.lib");
declare name "Simple Synth";
declare description "Detuned saw pair through a resonant lowpass. Replace this with your own DSP.";
declare author "MAGDA";
declare license "GPL-3.0";
declare version "1.0";

freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 0.5, 0, 1, 0.01);
gate = button("gate");

// Osc tab: detuned sub-saw mixed under the main saw.
oscSection = vgroup("Osc", os.sawtooth(freq) + sub * os.sawtooth(freq * 0.5))
with {
    sub = hslider("sub [idx:4]", 0.0, 0.0, 1.0, 0.01);
};

// Filter tab: resonant lowpass.
filterSection(x) = vgroup("Filter", x : fi.resonlp(cutoff, res, 1))
with {
    cutoff = hslider("cutoff [unit:Hz] [scale:log] [idx:0]", 3000, 50, 18000, 1);
    res    = hslider("resonance [idx:1]", 0.3, 0, 0.95, 0.01);
};

// Env tab: ADSR amplitude envelope.
envSection = vgroup("Env", en.adsr(att, 0.2, 0.7, rel, gate))
with {
    att = hslider("attack [unit:s] [idx:2]", 0.005, 0.001, 2, 0.001);
    rel = hslider("release [unit:s] [idx:3]", 0.4, 0.001, 4, 0.001);
};

voice = oscSection * envSection * gain : filterSection;
process = voice <: _, _;
)FAUST";

// ---- Helpers copied from FaustPlugin (share a base later) ------------------

// Map a normalized 0..1 value back to the real units the live zone expects,
// using the binding's frozen metadata. Audio-thread hot path — no allocation.
float denormalizeForBinding(const FaustParamPool::ActiveBindingDescriptor& b, float normalized) {
    const float n = juce::jlimit(0.0f, 1.0f, normalized);
    switch (b.kind) {
        case FaustParamSlot::Kind::Boolean:
        case FaustParamSlot::Kind::Trigger:
            return n >= 0.5f ? 1.0f : 0.0f;
        case FaustParamSlot::Kind::Discrete: {
            if (b.discreteValues.empty())
                return 0.0f;
            const int count = static_cast<int>(b.discreteValues.size());
            const int idx =
                juce::jlimit(0, count - 1, static_cast<int>(std::round(n * (count - 1))));
            return b.discreteValues[static_cast<size_t>(idx)];
        }
        case FaustParamSlot::Kind::Continuous: {
            float skewed = n;
            if (std::isfinite(b.scaleAnchor) && b.scaleAnchor > b.minValue &&
                b.scaleAnchor < b.maxValue) {
                float anchorRatio = 0.0f;
                if (b.logScale && b.minValue > 0.0f && b.maxValue > b.minValue) {
                    anchorRatio =
                        std::log(b.scaleAnchor / b.minValue) / std::log(b.maxValue / b.minValue);
                } else if (b.maxValue > b.minValue) {
                    anchorRatio = (b.scaleAnchor - b.minValue) / (b.maxValue - b.minValue);
                }
                if (anchorRatio > 0.0f && anchorRatio < 1.0f &&
                    std::abs(anchorRatio - 0.5f) > 1e-6f) {
                    const float skew = std::log(anchorRatio) / std::log(0.5f);
                    skewed = std::pow(juce::jlimit(0.0f, 1.0f, n), skew);
                }
            }
            if (b.logScale && b.minValue > 0.0f && b.maxValue > b.minValue)
                return b.minValue * std::pow(b.maxValue / b.minValue, skewed);
            return b.minValue + skewed * (b.maxValue - b.minValue);
        }
    }
    return 0.0f;
}

float normaliseDefaultForSlot(const FaustParamSlot& slot) {
    switch (slot.kind) {
        case FaustParamSlot::Kind::Boolean:
        case FaustParamSlot::Kind::Trigger:
            return slot.defaultValue >= 0.5f ? 1.0f : 0.0f;
        case FaustParamSlot::Kind::Discrete: {
            if (slot.choices.empty())
                return 0.0f;
            auto sorted = slot.choices;
            std::sort(sorted.begin(), sorted.end(),
                      [](const std::pair<float, juce::String>& a,
                         const std::pair<float, juce::String>& b) { return a.first < b.first; });
            int best = 0;
            float bestDistance = std::abs(sorted.front().first - slot.defaultValue);
            for (size_t i = 1; i < sorted.size(); ++i) {
                const float distance = std::abs(sorted[i].first - slot.defaultValue);
                if (distance < bestDistance) {
                    best = static_cast<int>(i);
                    bestDistance = distance;
                }
            }
            return sorted.size() > 1
                       ? static_cast<float>(best) / static_cast<float>(sorted.size() - 1)
                       : 0.0f;
        }
        case FaustParamSlot::Kind::Continuous:
            if (slot.logScale && slot.minValue > 0.0f && slot.maxValue > slot.minValue &&
                slot.defaultValue > 0.0f) {
                return juce::jlimit(0.0f, 1.0f,
                                    std::log(slot.defaultValue / slot.minValue) /
                                        std::log(slot.maxValue / slot.minValue));
            }
            if (slot.maxValue != slot.minValue)
                return juce::jlimit(0.0f, 1.0f,
                                    (slot.defaultValue - slot.minValue) /
                                        (slot.maxValue - slot.minValue));
            return 0.0f;
    }
    return 0.0f;
}

bool nearlyEqual(float a, float b) {
    return std::abs(a - b) <= 1.0e-5f;
}

bool sameControlIdentity(const FaustParamSlot& previous, const FaustParamSlot& current) {
    if (!previous.active || !current.active)
        return false;
    return previous.label == current.label && previous.unit == current.unit &&
           previous.kind == current.kind && nearlyEqual(previous.minValue, current.minValue) &&
           nearlyEqual(previous.maxValue, current.maxValue) &&
           nearlyEqual(previous.stepValue, current.stepValue) &&
           previous.logScale == current.logScale && previous.choices == current.choices;
}

juce::String poolParamId(int index) {
    return juce::String("param_") + juce::String(index + 1).paddedLeft('0', 2);
}

juce::String ensureStdfaustImport(const juce::String& source) {
    if (source.contains("stdfaust.lib"))
        return source;
    return juce::String("import(\"stdfaust.lib\");\n") + source;
}

}  // namespace

FaustInstrumentPlugin::FaustState::~FaustState() {
    // Every DSP instance has to die before the factory that made it: the
    // interpreter/wasm factory owns the code and vtables its instances run on,
    // so deleting it first turns the next ~dsp into a jump through freed
    // memory. monoVoice is a second instance off the same factory, and as a
    // member it would otherwise be destroyed after this body — reset it here.
    monoVoice.reset();
    poly.reset();  // deletes the per-voice DSPs it owns
    if (factory)
        magda::faust::deleteFactory(factory);
}

std::shared_ptr<FaustInstrumentPlugin::FaustState> FaustInstrumentPlugin::compile(
    const juce::String& source, int sampleRate, juce::String& errorOut) {
    const auto libsPath = getFaustLibrariesPath().getFullPathName().toStdString();
    const juce::String normalised = ensureStdfaustImport(source);
    const auto src = normalised.toStdString();

    std::vector<const char*> argv;
    argv.push_back("-I");
    argv.push_back(libsPath.c_str());

    std::string err;
    auto* factory = magda::faust::createFactoryFromString(
        "magda_faust_instrument", src, static_cast<int>(argv.size()), argv.data(), err);
    if (!factory) {
        errorOut = juce::String(err);
        return nullptr;
    }

    auto state = std::make_shared<FaustState>();
    state->factory = factory;

    // group=false: each voice keeps its own writable zones (we fan param writes
    // out to all of them). control=true: voices are dynamically allocated and
    // driven by keyOn/keyOff. mydsp_poly takes ownership of the template DSP.
    ::dsp* voiceTemplate = factory->createDSPInstance();
    if (!voiceTemplate) {
        errorOut = "createDSPInstance returned null";
        return nullptr;  // ~FaustState deletes the factory
    }
    state->poly = std::make_unique<mydsp_poly>(voiceTemplate, FaustInstrumentPlugin::kNumVoices,
                                               /*control*/ true, /*group*/ false);
    state->poly->init(sampleRate);
    state->dspIn = state->poly->getNumInputs();
    state->dspOut = state->poly->getNumOutputs();

    // Resolve each voice's freq zone once, here on the message thread, so pitch
    // bend can write it later without a by-string lookup. fFreqPath is what the
    // allocator itself uses on keyOn, so this stays in step with whatever the
    // patch called its pitch control.
    {
        auto* impl = static_cast<mydsp_poly*>(state->poly.get());
        state->voiceFreqZones.reserve(impl->fVoiceTable.size());
        for (auto* voice : impl->fVoiceTable) {
            std::vector<FAUSTFLOAT*> zones;
            if (voice != nullptr) {
                for (const auto& path : voice->fFreqPath) {
                    if (auto* zone = voice->getParamZone(path))
                        zones.push_back(zone);
                }
            }
            state->voiceFreqZones.push_back(std::move(zones));
        }
    }

    // A second, independent instance for Mono/Legato. Built here rather than
    // lazily on first use: allocating a DSP is not something to do from the
    // audio thread, and the mode can change between any two blocks.
    if (auto* monoInstance = factory->createDSPInstance()) {
        state->monoVoice.reset(monoInstance);
        state->monoVoice->init(sampleRate);
    }
    return state;
}

std::shared_ptr<FaustInstrumentPlugin::FaustState> FaustInstrumentPlugin::compileAndRebind(
    const juce::String& source, juce::String& errorOut) {
    auto state = compile(source, currentSampleRate_, errorOut);
    if (!state) {
        DBG("[FaustInstrument] compileAndRebind: compile FAILED: " << errorOut);
        return nullptr;
    }

    std::array<FaustParamSlot, FaustParamPool::kSize> previousSlots{};
    for (int i = 0; i < FaustParamPool::kSize; ++i)
        previousSlots[static_cast<size_t>(i)] = pool_.slot(i);

    FaustUIHarvester harvester(FaustUIHarvester::Layout::PolyphonicVoices);
    state->poly->buildUserInterface(&harvester);

    // Group the per-voice controls by label. The shared harvester has already
    // removed the poly proxy controls; reserved MIDI controls (freq/gain/gate)
    // are filtered here. The first occurrence supplies range/metadata; every
    // occurrence contributes a voice zone.
    struct Grouped {
        HarvestedControl rep;
        std::vector<FAUSTFLOAT*> zones;
    };
    std::vector<Grouped> groups;
    std::map<juce::String, size_t> byLabel;
    for (const auto& c : harvester.controls()) {
        if (isReservedVoiceControl(c.label))
            continue;
        auto it = byLabel.find(c.label);
        if (it == byLabel.end()) {
            Grouped g;
            g.rep = c;
            g.zones.push_back(c.zone);
            byLabel.emplace(c.label, groups.size());
            groups.push_back(std::move(g));
        } else {
            groups[it->second].zones.push_back(c.zone);
        }
    }

    std::vector<HarvestedControl> reps;
    std::map<FAUSTFLOAT*, std::vector<FAUSTFLOAT*>> zonesByRep;
    reps.reserve(groups.size());
    for (auto& g : groups) {
        zonesByRep.emplace(g.rep.zone, g.zones);
        reps.push_back(std::move(g.rep));
    }

    DBG("[FaustInstrument] harvested " << static_cast<int>(harvester.controls().size())
                                       << " voice controls -> " << static_cast<int>(reps.size())
                                       << " user params (excl. freq/gain/gate)");

    // Bargraphs are per-voice too. Merge the occurrences of one author
    // bargraph into a single output carrying every voice zone, so the meter
    // reads the whole instrument rather than whichever voice Faust emitted
    // first.
    std::vector<HarvestedOutput> mergedOutputs;
    std::map<juce::String, size_t> outputByLabel;
    for (const auto& o : harvester.outputs()) {
        auto it = outputByLabel.find(o.label);
        if (it == outputByLabel.end()) {
            outputByLabel.emplace(o.label, mergedOutputs.size());
            mergedOutputs.push_back(o);
        } else {
            auto& target = mergedOutputs[it->second];
            target.zones.insert(target.zones.end(), o.zones.begin(), o.zones.end());
        }
    }

    auto report = pool_.rebindFromHarvest(reps, mergedOutputs);
    state->activeBindings = std::move(report.activeBindings);
    lastDiagnostics_ = std::move(report.diagnostics);

    // Map each active slot to the full per-voice zone list for its control.
    for (const auto& b : state->activeBindings) {
        if (b.slotIndex < 0 || b.slotIndex >= FaustParamPool::kSize || !b.zone)
            continue;
        if (auto it = zonesByRep.find(b.zone); it != zonesByRep.end())
            state->voiceZonesBySlot[static_cast<size_t>(b.slotIndex)] = it->second;
    }

    // Do the same for the mono voice. Harvested separately with the Standard
    // layout: it is a bare DSP instance, with none of the proxy/voice boxes the
    // poly wrapper adds, so PolyphonicVoices has nothing to strip. Its labels
    // are the author's own, which is what lets the same label match the pool
    // slot the poly voices bound to.
    if (state->monoVoice) {
        FaustUIHarvester monoHarvester(FaustUIHarvester::Layout::Standard);
        state->monoVoice->buildUserInterface(&monoHarvester);

        std::map<juce::String, FAUSTFLOAT*> monoByLabel;
        for (const auto& c : monoHarvester.controls())
            monoByLabel.emplace(c.label, c.zone);

        const auto findMono = [&monoByLabel](const char* label) -> FAUSTFLOAT* {
            auto it = monoByLabel.find(juce::String(label));
            return it != monoByLabel.end() ? it->second : nullptr;
        };
        state->monoFreqZone = findMono("freq");
        state->monoGainZone = findMono("gain");
        state->monoGateZone = findMono("gate");

        // Slot zones are matched by label, the same key the poly path groups
        // its per-voice occurrences under, so the two stay in step by
        // construction rather than by both happening to walk the UI in order.
        for (const auto& rep : reps) {
            auto it = monoByLabel.find(rep.label);
            if (it == monoByLabel.end())
                continue;
            for (const auto& b : state->activeBindings)
                if (b.zone == rep.zone && b.slotIndex >= 0 && b.slotIndex < FaustParamPool::kSize) {
                    state->monoZoneBySlot[static_cast<size_t>(b.slotIndex)] = it->second;
                    break;
                }
        }
    }

    initialiseUnsetPoolValues(state->activeBindings, previousSlots);

    DBG("[FaustInstrument] pool active=" << pool_.activeCount() << " bindings="
                                         << static_cast<int>(state->activeBindings.size()));
    for (const auto& d : lastDiagnostics_)
        DBG("  diagnostic: " << d);
    return state;
}

int FaustInstrumentPlugin::readVoiceMode() const {
    if (!voiceModeParam_)
        return Poly;
    // The parameter is normalised 0..1 over three modes.
    const float real = voiceModeParam_->getCurrentValue() * 2.0f;
    return juce::jlimit(0, 2, static_cast<int>(std::lround(real)));
}

void FaustInstrumentPlugin::resetAllVoices(const std::shared_ptr<FaustState>& state) {
    if (!state)
        return;
    if (state->poly)
        state->poly->ctrlChange(0, 123, 0);  // All Notes Off
    if (state->monoVoice)
        state->monoVoice->instanceClear();
    heldNotes_.clear();
    if (state->monoGateZone)
        *state->monoGateZone = 0.0f;
    // Drop the glide target too, so the next note starts from itself rather
    // than sliding in from whatever was last played.
    glideCurrentHz_ = 0.0f;
    glideTargetHz_ = 0.0f;
    // Recentre the wheel. A panic has to leave the instrument at concert pitch:
    // the reset paths are transport stops and mode changes, where no note-off
    // arrives either, so a wheel left off-centre would silently detune every
    // note played afterwards with nothing on screen to explain it.
    bendNormalised_ = 0.0f;
    lastPolyBendRatio_ = 1.0f;
}

float FaustInstrumentPlugin::readBendRatio() const {
    if (bendNormalised_ == 0.0f)
        return 1.0f;
    const float semitones =
        (bendRangeParam_ ? bendRangeParam_->getCurrentValue() : 0.0f) * kMaxBendSemitones;
    return std::pow(2.0f, bendNormalised_ * semitones / 12.0f);
}

void FaustInstrumentPlugin::applyBendToPolyVoices(const std::shared_ptr<FaustState>& state,
                                                  float ratio) {
    if (!state || !state->poly)
        return;
    auto* impl = static_cast<mydsp_poly*>(state->poly.get());
    const size_t count =
        std::min(state->voiceFreqZones.size(), static_cast<size_t>(impl->fVoiceTable.size()));
    for (size_t i = 0; i < count; ++i) {
        auto* voice = impl->fVoiceTable[i];
        if (voice == nullptr)
            continue;
        // fCurNote is a real pitch only while the voice is sounding; the free
        // and legato states are negative sentinels. In legato the voice is
        // already committed to fNextNote, so bend that instead.
        const int note = voice->fCurNote >= 0 ? voice->fCurNote : voice->fNextNote;
        if (note < 0)
            continue;
        const auto hz = static_cast<FAUSTFLOAT>(midiNoteToHz(note) * ratio);
        for (auto* zone : state->voiceFreqZones[i])
            *zone = hz;
    }
}

void FaustInstrumentPlugin::releasePolyVoicesForPitch(const std::shared_ptr<FaustState>& state,
                                                      int pitch) {
    if (!state || !state->poly)
        return;
    // compile() always builds this as mydsp_poly.
    auto* impl = static_cast<mydsp_poly*>(state->poly.get());
    for (auto* voice : impl->fVoiceTable) {
        if (voice == nullptr)
            continue;
        if (voice->fCurNote == pitch ||
            (voice->fCurNote == kLegatoVoice && voice->fNextNote == pitch))
            voice->keyOff(/*hard*/ false);
    }
}

bool FaustInstrumentPlugin::handleMonoNoteOn(const std::shared_ptr<FaustState>& state, int note,
                                             int velocity, int mode) {
    const float g = static_cast<float>(velocity) / 127.0f;
    const bool wasEmpty = heldNotes_.empty();
    heldNotes_.push_back({note, g});

    glideTargetHz_ = midiNoteToHz(note);
    // From silence there is nothing to glide from, so land on the note. This is
    // also what keeps the first note of a phrase from swooping in.
    if (wasEmpty || glideCurrentHz_ <= 0.0f)
        glideCurrentHz_ = glideTargetHz_;

    if (state->monoGainZone)
        *state->monoGainZone = g;

    if (wasEmpty) {
        if (state->monoGateZone)
            *state->monoGateZone = 1.0f;  // clean attack from silence
        return false;
    }
    if (mode == Mono) {
        if (state->monoGateZone)
            *state->monoGateZone = 0.0f;  // caller raises it after one sample
        return true;
    }
    return false;  // Legato: pitch changes, the envelope keeps running
}

void FaustInstrumentPlugin::handleMonoNoteOff(const std::shared_ptr<FaustState>& state, int note) {
    // Search from the top so releasing one of a repeated pitch drops the most
    // recent, leaving any earlier hold of the same note intact.
    for (auto it = heldNotes_.rbegin(); it != heldNotes_.rend(); ++it)
        if (it->note == note) {
            heldNotes_.erase(std::next(it).base());
            break;
        }

    if (heldNotes_.empty()) {
        if (state->monoGateZone)
            *state->monoGateZone = 0.0f;  // last note up -> envelope release
    } else {
        glideTargetHz_ = midiNoteToHz(heldNotes_.back().note);  // legato return
    }
}

void FaustInstrumentPlugin::initialiseUnsetPoolValues(
    const std::vector<FaustParamPool::ActiveBindingDescriptor>& bindings,
    const std::array<FaustParamSlot, FaustParamPool::kSize>& previousSlots) {
    auto* um = getUndoManager();
    for (const auto& binding : bindings) {
        const int slotIndex = binding.slotIndex;
        if (slotIndex < 0 || slotIndex >= FaustParamPool::kSize)
            continue;

        const auto& previous = previousSlots[static_cast<size_t>(slotIndex)];
        const bool sameControl = sameControlIdentity(previous, pool_.slot(slotIndex));
        const bool restoredBeforeFirstBind =
            !previous.active && poolValueWasRestored_[static_cast<size_t>(slotIndex)];
        if (sameControl || restoredBeforeFirstBind)
            continue;

        const float normalisedDefault = normaliseDefaultForSlot(pool_.slot(slotIndex));
        auto& cached = poolCached_[static_cast<size_t>(slotIndex)];
        cached.setValue(normalisedDefault, um);

        auto& param = poolParams_[static_cast<size_t>(slotIndex)];
        if (param)
            param->updateFromAttachedValue();
    }
}

FaustInstrumentPlugin::FaustInstrumentPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    // Mono/legato note-ons push onto this from applyToBuffer. MIDI cannot hold
    // more than 128 notes down at once, so reserving here means the audio
    // thread never grows it.
    heldNotes_.reserve(128);
    poolParams_.resize(FaustParamPool::kSize);
    auto* um = getUndoManager();
    juce::NormalisableRange<float> normalisedRange{0.0f, 1.0f};
    for (int i = 0; i < FaustParamPool::kSize; ++i) {
        const auto id = poolParamId(i);
        poolValueWasRestored_[static_cast<size_t>(i)] = state.hasProperty(juce::Identifier(id));
        poolCached_[static_cast<size_t>(i)].referTo(this->state, juce::Identifier(id), um, 0.0f);
        poolParams_[static_cast<size_t>(i)] = addParam(id, id, normalisedRange);
        poolParams_[static_cast<size_t>(i)]->attachToCurrentValue(
            poolCached_[static_cast<size_t>(i)]);
    }

    // Host-owned voice allocation, added after the pool so the pool's parameter
    // indices stay put. Both are normalised 0..1 like every other TE parameter;
    // faustInstrumentHostParamInfo() carries the real ranges for display.
    voiceModeCached_.referTo(this->state, juce::Identifier("voiceMode"), um, 0.0f);
    voiceModeParam_ = addParam("voiceMode", "Voice Mode", normalisedRange);
    voiceModeParam_->attachToCurrentValue(voiceModeCached_);

    glideCached_.referTo(this->state, juce::Identifier("glide"), um, 0.0f);
    glideParam_ = addParam("glide", "Glide", normalisedRange);
    glideParam_->attachToCurrentValue(glideCached_);

    // Default 2 semitones, the near-universal synth default, stored normalised
    // against kMaxBendSemitones like every other parameter here.
    constexpr float kDefaultBendNorm = 2.0f / kMaxBendSemitones;
    bendRangeCached_.referTo(this->state, juce::Identifier("bendRange"), um, kDefaultBendNorm);
    bendRangeParam_ = addParam("bendRange", "Bend Range", normalisedRange);
    bendRangeParam_->attachToCurrentValue(bendRangeCached_);

    const auto savedSource = state.getProperty("dspSource", juce::String()).toString();
    const auto savedName = state.getProperty("dspName", juce::String()).toString();

    juce::String err;
    auto compiled = compileAndRebind(
        savedSource.isNotEmpty() ? savedSource : juce::String(kDefaultDspSource), err);
    if (!compiled) {
        DBG("FaustInstrumentPlugin: failed to compile saved source: " << err << " — using default");
        compiled = compileAndRebind(kDefaultDspSource, err);
    }

    dspSource_ = savedSource.isNotEmpty() ? savedSource : juce::String(kDefaultDspSource);
    dspName_ = savedName.isNotEmpty() ? savedName : juce::String("Simple Synth");
    // Derived, not restored: the source is the only thing that has to persist.
    viewName_ = readCustomViewName(dspSource_);

    reservePointerScratch(compiled);
    std::atomic_store(&active_, compiled);

    state.setProperty("dspSource", dspSource_, nullptr);
    state.setProperty("dspName", dspName_, nullptr);

    retireTimer_.startTimer(100);

    DBG("FaustInstrumentPlugin ctor: name="
        << dspName_ << " in=" << (compiled ? compiled->dspIn : -1)
        << " out=" << (compiled ? compiled->dspOut : -1) << " active=" << pool_.activeCount());
}

FaustInstrumentPlugin::~FaustInstrumentPlugin() {
    notifyListenersOfDeletion();
    retireTimer_.stopTimer();
    for (auto& p : poolParams_) {
        if (p)
            p->detachFromCurrentValue();
    }
    if (voiceModeParam_)
        voiceModeParam_->detachFromCurrentValue();
    if (glideParam_)
        glideParam_->detachFromCurrentValue();
    std::atomic_store(&active_, std::shared_ptr<FaustState>{});
    {
        const juce::ScopedLock lk(retiredLock_);
        retired_.clear();
    }
}

void FaustInstrumentPlugin::drainRetired() {
    const auto now = juce::Time::getMillisecondCounter();
    std::vector<RetiredItem> toDelete;
    {
        const juce::ScopedLock lk(retiredLock_);
        for (auto it = retired_.begin(); it != retired_.end();) {
            if (now - it->retiredAtMs >= 200) {
                toDelete.push_back(std::move(*it));
                it = retired_.erase(it);
            } else {
                ++it;
            }
        }
    }
    // toDelete dtors fire here, off the lock and on the message thread.
}

bool FaustInstrumentPlugin::loadDspSource(const juce::String& name, const juce::String& source,
                                          juce::String& errorOut) {
    auto compiled = compileAndRebind(source, errorOut);
    if (!compiled)
        return false;

    auto previous = std::atomic_load(&active_);
    reservePointerScratch(compiled);
    std::atomic_store(&active_, compiled);

    if (previous) {
        const juce::ScopedLock lk(retiredLock_);
        retired_.push_back({previous, juce::Time::getMillisecondCounter()});
    }

    dspName_ = name;
    dspSource_ = source;
    viewName_ = readCustomViewName(source);
    state.setProperty("dspName", dspName_, getUndoManager());
    state.setProperty("dspSource", dspSource_, getUndoManager());

    DBG("FaustInstrumentPlugin::loadDspSource ok name=" << name << " out=" << compiled->dspOut
                                                        << " active=" << pool_.activeCount());
    return true;
}

void FaustInstrumentPlugin::stageSourceForEditing(const juce::String& name,
                                                  const juce::String& source) {
    // Match the effect's staging semantics: edit/persist the proposed source
    // without replacing the audible polyphonic DSP or its parameter pool.
    dspName_ = name;
    dspSource_ = source;
    state.setProperty("dspName", dspName_, getUndoManager());
    state.setProperty("dspSource", dspSource_, getUndoManager());
}

void FaustInstrumentPlugin::initialise(const te::PluginInitialisationInfo& info) {
    currentSampleRate_ = static_cast<int>(info.sampleRate);

    if (auto state = std::atomic_load(&active_)) {
        if (state->poly)
            state->poly->instanceInit(currentSampleRate_);
        // The mono voice is a separate instance the allocator knows nothing
        // about, so it needs its own re-init. The constructor compiles at a
        // provisional 44.1 kHz, and without this Mono and Legato kept those
        // constants on a device running at anything else: oscillators detuned
        // by the rate ratio and envelope times off by the same factor, until
        // something happened to trigger a recompile.
        if (state->monoVoice)
            state->monoVoice->instanceInit(currentSampleRate_);
    }

    const int dspOut = std::atomic_load(&active_) ? std::atomic_load(&active_)->dspOut : 2;
    scratchOut_.setSize(std::max(dspOut, 2), info.blockSizeSamples, false, true, false);

    DBG("FaustInstrumentPlugin::initialise sr=" << currentSampleRate_
                                                << " blockSize=" << info.blockSizeSamples);
}

void FaustInstrumentPlugin::deinitialise() {}

void FaustInstrumentPlugin::reset() {
    // Called from the message thread (TE's plugin API, and AudioBridge's
    // resetSynthsOnTrack after a record pass) while the audio thread may be
    // inside compute(). Only raise the flag; the flush runs at the top of the
    // next applyToBuffer.
    pendingVoiceFlush_.store(true, std::memory_order_release);
}

void FaustInstrumentPlugin::reservePointerScratch(const std::shared_ptr<FaustState>& state) {
    if (!state)
        return;
    outPtrs_.reserve(static_cast<size_t>(std::max(0, state->dspOut)));
}

void FaustInstrumentPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.destBuffer || fc.bufferNumSamples <= 0)
        return;

    auto active = std::atomic_load(&active_);
    if (!active || !active->poly)
        return;

    // A flush requested from the message thread runs here, on the audio thread,
    // where nothing else is walking the voice table.
    if (pendingVoiceFlush_.exchange(false, std::memory_order_acq_rel))
        resetAllVoices(active);

    // Stopping mid-note delivers no note-offs, so a sounding clip voice would
    // hang gated on. Flush on the playing -> stopped edge.
    if (wasPlaying_ && !fc.isPlaying)
        resetAllVoices(active);
    wasPlaying_ = fc.isPlaying;

    // Apply user parameter values: denormalize once per slot, then fan the
    // value out to every voice's zone (plain pointer writes — RT-safe). The
    // mono voice gets the same value: it is a peer of the poly voices, just one
    // the allocator does not own.
    for (const auto& b : active->activeBindings) {
        if (b.role != FaustControlRole::User)
            continue;
        if (b.slotIndex < 0 || b.slotIndex >= FaustParamPool::kSize)
            continue;
        const auto& param = poolParams_[static_cast<size_t>(b.slotIndex)];
        if (!param)
            continue;
        const float value = static_cast<float>(denormalizeForBinding(b, param->getCurrentValue()));
        for (FAUSTFLOAT* zone : active->voiceZonesBySlot[static_cast<size_t>(b.slotIndex)]) {
            if (zone)
                *zone = static_cast<FAUSTFLOAT>(value);
        }
        if (auto* monoZone = active->monoZoneBySlot[static_cast<size_t>(b.slotIndex)])
            *monoZone = static_cast<FAUSTFLOAT>(value);
    }

    // Host-supplied controls. Sample the edit tempo once per block, then fan
    // the BPM out to the matching zone in every voice. Runtime instruments use
    // group=false, so unlike the effect host there is no single shared zone.
    double cachedBpm = -1.0;
    for (const auto& b : active->activeBindings) {
        if (b.role != FaustControlRole::ProjectTempo)
            continue;
        if (b.slotIndex < 0 || b.slotIndex >= FaustParamPool::kSize)
            continue;
        const auto& zones = active->voiceZonesBySlot[static_cast<size_t>(b.slotIndex)];
        if (zones.empty())
            continue;
        if (cachedBpm < 0.0)
            cachedBpm = edit.tempoSequence.getBpmAt(fc.editTime.getStart());
        for (FAUSTFLOAT* zone : zones) {
            if (zone)
                *zone = static_cast<FAUSTFLOAT>(cachedBpm);
        }
        if (auto* monoZone = active->monoZoneBySlot[static_cast<size_t>(b.slotIndex)])
            *monoZone = static_cast<FAUSTFLOAT>(cachedBpm);
    }

    const int hostChannels = fc.destBuffer->getNumChannels();
    const int n = fc.bufferNumSamples;
    const int start = fc.bufferStartSample;
    const int dspOut = active->dspOut;

    if (hostChannels <= 0 || dspOut <= 0 || scratchOut_.getNumSamples() <= 0)
        return;

    // Mono and Legato play a voice the allocator knows nothing about, so a mode
    // change has to silence whichever engine is being left behind - otherwise
    // its held notes keep sounding under the new one with no way to release
    // them.
    const int mode = active->monoVoice ? readVoiceMode() : Poly;
    if (mode != lastVoiceMode_) {
        resetAllVoices(active);
        lastVoiceMode_ = mode;
    }
    const bool monophonic = (mode != Poly) && active->monoVoice;

    ::dsp* engine = monophonic ? active->monoVoice.get() : static_cast<::dsp*>(active->poly.get());

    // Glide time as a one-pole time constant, matching what the compiled synths
    // get from si.smooth(ba.tau2pole(glide)) inside their DSP. Here the host
    // owns the ramp instead, because a runtime patch reads `freq` directly and
    // cannot be assumed to smooth anything itself.
    const float glideMs = glideParam_ ? glideParam_->getCurrentValue() * 2000.0f : 0.0f;
    const float glideTau = glideMs * 0.001f;

    outPtrs_.resize(static_cast<size_t>(dspOut));
    // mydsp_poly's internal mix buffers cap at MIX_BUFFER_SIZE.
    const int maxChunk = std::min(MIX_BUFFER_SIZE, scratchOut_.getNumSamples());

    // Render [segStart, segStart + segLen) from the active engine and ADD it
    // into destBuffer (compute() overwrites its outputs, and the buffer may
    // already carry signal we must not clobber). While a glide is in flight the
    // segment is further chopped into kGlideChunkSamples so the freq zone can
    // move within it.
    const auto renderSegment = [&](int segStart, int segLen) {
        int done = 0;
        while (done < segLen) {
            const bool gliding = monophonic && glideTau > 0.0f && glideTargetHz_ > 0.0f &&
                                 std::abs(glideCurrentHz_ - glideTargetHz_) >
                                     glideTargetHz_ * (kGlideDoneRatio - 1.0f);
            const int limit = gliding ? std::min(maxChunk, kGlideChunkSamples) : maxChunk;
            const int chunk = std::min(limit, segLen - done);

            if (monophonic && active->monoFreqZone) {
                if (gliding) {
                    // One-pole step over this chunk's worth of samples.
                    const float pole =
                        std::exp(-static_cast<float>(chunk) /
                                 (glideTau * static_cast<float>(currentSampleRate_)));
                    glideCurrentHz_ += (1.0f - pole) * (glideTargetHz_ - glideCurrentHz_);
                } else {
                    glideCurrentHz_ = glideTargetHz_;
                }
                // Bend multiplies the ramp's output rather than its state, so
                // moving the wheel mid-glide does not disturb where the glide
                // is heading. The block is split at each MIDI event, so this
                // picks up a wheel move at the sample it arrived on.
                if (glideCurrentHz_ > 0.0f)
                    *active->monoFreqZone =
                        static_cast<FAUSTFLOAT>(glideCurrentHz_ * readBendRatio());
            }

            for (int ch = 0; ch < dspOut; ++ch)
                outPtrs_[static_cast<size_t>(ch)] =
                    scratchOut_.getWritePointer(ch % scratchOut_.getNumChannels());

            engine->compute(chunk, nullptr, outPtrs_.data());

            for (int ch = 0; ch < hostChannels; ++ch) {
                // One-output ("mono") DSP drives both channels; else channel-map.
                const int srcCh = (dspOut == 1) ? 0 : (ch % dspOut);
                fc.destBuffer->addFrom(ch, start + segStart + done, scratchOut_, srcCh, 0, chunk);
            }
            done += chunk;
        }
    };

    // Walk the block, rendering up to each MIDI event before applying it. The
    // Mono retrigger depends on this: its envelope restarts on a gate edge, and
    // an edge only exists if samples are rendered either side of it.
    int cursor = 0;
    if (fc.bufferForMidiMessages != nullptr && !fc.bufferForMidiMessages->isEmpty()) {
        for (auto& m : *fc.bufferForMidiMessages) {
            int evSample = juce::roundToInt(m.getTimeStamp() * currentSampleRate_);
            evSample = juce::jlimit(cursor, n, evSample);  // clamp + keep monotonic
            renderSegment(cursor, evSample - cursor);
            cursor = evSample;

            if (!monophonic) {
                if (m.isNoteOn()) {
                    // Release anything still sounding this pitch first, so the
                    // allocator never accumulates orphans that hang.
                    releasePolyVoicesForPitch(active, m.getNoteNumber());
                    active->poly->keyOn(m.getChannel(), m.getNoteNumber(), m.getVelocity());
                    // keyOn writes an unbent freq, so a note started while the
                    // wheel is held would otherwise sound at concert pitch.
                    if (lastPolyBendRatio_ != 1.0f)
                        applyBendToPolyVoices(active, lastPolyBendRatio_);
                } else if (m.isNoteOff()) {
                    releasePolyVoicesForPitch(active, m.getNoteNumber());
                } else if (m.isPitchWheel()) {
                    // Not forwarded to the allocator: dsp_poly::pitchWheel
                    // bottoms out in an empty midi::pitchWheel, so it only ever
                    // moved controls a patch tagged [midi:pitchwheel] itself.
                    // Driving freq here makes the wheel work for every patch.
                    bendNormalised_ = bendFromWheel(m.getPitchWheelValue());
                    lastPolyBendRatio_ = readBendRatio();
                    applyBendToPolyVoices(active, lastPolyBendRatio_);
                } else if (m.isController()) {
                    // Includes CC 120/123 (all sound/notes off), which the Faust
                    // MIDI handler turns into keyOff across active voices.
                    active->poly->ctrlChange(m.getChannel(), m.getControllerNumber(),
                                             m.getControllerValue());
                }
            } else {
                if (m.isNoteOn()) {
                    if (handleMonoNoteOn(active, m.getNoteNumber(), m.getVelocity(), mode)) {
                        // One sample of gate-low renders the falling edge;
                        // raising it again gives the rising edge that
                        // retriggers the envelope.
                        const int low = std::min(1, n - cursor);
                        renderSegment(cursor, low);
                        cursor += low;
                        if (active->monoGateZone)
                            *active->monoGateZone = 1.0f;
                    }
                } else if (m.isNoteOff()) {
                    handleMonoNoteOff(active, m.getNoteNumber());
                } else if (m.isPitchWheel()) {
                    // The mono branch handled no wheel at all. The glide ramp
                    // owns monoFreqZone, so bend is folded in where that is
                    // written rather than poked in here.
                    bendNormalised_ = bendFromWheel(m.getPitchWheelValue());
                } else if (m.isController() &&
                           (m.getControllerNumber() == 120 || m.getControllerNumber() == 123)) {
                    heldNotes_.clear();
                    if (active->monoGateZone)
                        *active->monoGateZone = 0.0f;
                }
            }
        }
    }
    renderSegment(cursor, n - cursor);
}

void FaustInstrumentPlugin::restorePluginStateFromValueTree(const juce::ValueTree& v) {
    const auto savedSource = v.getProperty("dspSource", juce::String()).toString();
    const auto savedName = v.getProperty("dspName", juce::String()).toString();

    if (savedSource.isNotEmpty() && savedSource != dspSource_) {
        juce::String err;
        if (!loadDspSource(savedName.isNotEmpty() ? savedName : juce::String("Loaded"), savedSource,
                           err)) {
            DBG("FaustInstrumentPlugin::restore: compile failed: " << err);
        }
    }

    for (size_t i = 0; i < poolCached_.size(); ++i) {
        const auto id = poolParamId(static_cast<int>(i));
        if (auto p = v.getPropertyPointer(juce::Identifier(id)))
            poolCached_[i] = static_cast<float>(*p);
        else
            poolCached_[i].resetToDefault();
    }
    for (auto& p : poolParams_) {
        if (p)
            p->updateFromAttachedValue();
    }
}

}  // namespace magda::daw::audio
