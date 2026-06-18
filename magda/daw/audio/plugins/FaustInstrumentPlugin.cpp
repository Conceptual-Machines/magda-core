#include "plugins/FaustInstrumentPlugin.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>

#include "FaustMetadataParser.hpp"
#include "FaustResources.hpp"
#include "faust/dsp/interpreter-dsp.h"
#include "faust/dsp/poly-dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"

// NOTE: Faust's GUI base statics (GUI::fGuiList / gTimedZoneMap), required by
// poly-dsp.h, are defined once in FaustPolyGuiStatics.cpp so multiple poly TUs
// link without duplicate symbols.

namespace magda::daw::audio {

const char* FaustInstrumentPlugin::xmlTypeName = "faustinstrument";

namespace {

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
constexpr const char* kDefaultDspSource = R"FAUST(
import("stdfaust.lib");
declare name "Faust Poly Synth";

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

// ---- Helpers copied from FaustPlugin (POC; share a base later) -------------

// Map a normalized 0..1 value back to the real units the live zone expects,
// using the binding's frozen metadata. Audio-thread hot path — no allocation.
float denormalizeForBinding(const FaustParamPool::ActiveBindingDescriptor& b, float normalized) {
    const float n = juce::jlimit(0.0f, 1.0f, normalized);
    switch (b.kind) {
        case FaustParamSlot::Kind::Boolean:
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

// ---- Voice-aware harvester -------------------------------------------------
//
// mydsp_poly with group=false emits, under a "Polyphonic" tab: a grouped
// "Voices" proxy box (whose zones only propagate via the global
// GUI::updateAllGuis(), which we avoid), followed by one "Voice<n>" box per
// voice carrying that voice's own directly-writable zones. We harvest the
// individual voice boxes and group their zones by control label so a single
// pool slot can fan a write out to every voice.
struct VoiceHarvester : public ::UI {
    struct RawControl {
        FaustParamSlot::Kind kind = FaustParamSlot::Kind::Continuous;
        juce::String label;
        float minValue = 0.0f;
        float maxValue = 1.0f;
        float stepValue = 0.0f;
        float defaultValue = 0.0f;
        FAUSTFLOAT* zone = nullptr;
        ControlMetadata metadata;
        bool fromProxyGroup = false;  // under the shared "Voices" box → skip
        juce::String group;           // top-level author group (tab name), may be empty
    };

    std::vector<RawControl> raw;
    std::vector<ControlMetadata> groupStack;
    std::vector<juce::String> groupLabelStack;  // cleaned labels, parallel to groupStack
    std::map<FAUSTFLOAT*, ControlMetadata> pendingByZone;

    void pushGroup(const char* label) {
        auto parsed = parseFaustLabel(juce::String::fromUTF8(label != nullptr ? label : ""));
        groupStack.push_back(parsed.metadata);
        groupLabelStack.push_back(parsed.cleanLabel);
    }
    void popGroup() {
        if (!groupStack.empty())
            groupStack.pop_back();
        if (!groupLabelStack.empty())
            groupLabelStack.pop_back();
    }

    // The shared proxy box is labelled exactly "Voices"; individual voices are
    // "Voice1".."VoiceN" (or "V1".. for >=8 voices). Treat a control as proxy
    // if its nearest matching ancestor is the "Voices" box.
    bool inProxyGroup() const {
        for (auto it = groupLabelStack.rbegin(); it != groupLabelStack.rend(); ++it) {
            if (*it == "Voices")
                return true;
            if (it->startsWith("Voice") || (it->length() >= 2 && (*it)[0] == 'V' &&
                                            juce::CharacterFunctions::isDigit((*it)[1])))
                return false;
        }
        return false;
    }

    // The author's intended tab group: the OUTERMOST group label that isn't a
    // structural poly box ("Polyphonic" / "Voices" / "Voice<n>" / "V<n>").
    // Empty when the control is declared flat (no author group) → "Params" tab.
    juce::String topLevelAuthorGroup() const {
        for (const auto& label : groupLabelStack) {  // outermost → innermost
            if (label == "Polyphonic" || label == "Voices")
                continue;
            if (label.startsWith("Voice") || (label.length() >= 2 && label[0] == 'V' &&
                                              juce::CharacterFunctions::isDigit(label[1])))
                continue;
            return label;
        }
        return {};
    }

    ControlMetadata mergedFor(FAUSTFLOAT* zone) {
        ControlMetadata merged;
        for (const auto& g : groupStack)
            mergeFaustMetadata(merged, g);
        if (auto it = pendingByZone.find(zone); it != pendingByZone.end()) {
            mergeFaustMetadata(merged, it->second);
            pendingByZone.erase(it);
        }
        return merged;
    }

    void emitControl(FaustParamSlot::Kind kind, const char* rawLabel, FAUSTFLOAT* zone,
                     FAUSTFLOAT init, FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step) {
        auto parsed = parseFaustLabel(juce::String::fromUTF8(rawLabel != nullptr ? rawLabel : ""));
        ControlMetadata merged = mergedFor(zone);
        mergeFaustMetadata(merged, parsed.metadata);

        RawControl c;
        c.kind = kind;
        c.label = parsed.cleanLabel;
        c.minValue = static_cast<float>(min);
        c.maxValue = static_cast<float>(max);
        c.stepValue = static_cast<float>(step);
        c.defaultValue = static_cast<float>(init);
        c.zone = zone;
        c.metadata = std::move(merged);
        c.fromProxyGroup = inProxyGroup();
        c.group = topLevelAuthorGroup();
        raw.push_back(std::move(c));
    }

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
        popGroup();
    }

    void addButton(const char* label, FAUSTFLOAT* zone) override {
        emitControl(FaustParamSlot::Kind::Boolean, label, zone, 0, 0, 1, 1);
    }
    void addCheckButton(const char* label, FAUSTFLOAT* zone) override {
        emitControl(FaustParamSlot::Kind::Boolean, label, zone, 0, 0, 1, 1);
    }
    void addVerticalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min,
                           FAUSTFLOAT max, FAUSTFLOAT step) override {
        emitControl(FaustParamSlot::Kind::Continuous, label, zone, init, min, max, step);
    }
    void addHorizontalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min,
                             FAUSTFLOAT max, FAUSTFLOAT step) override {
        emitControl(FaustParamSlot::Kind::Continuous, label, zone, init, min, max, step);
    }
    void addNumEntry(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min,
                     FAUSTFLOAT max, FAUSTFLOAT step) override {
        emitControl(FaustParamSlot::Kind::Continuous, label, zone, init, min, max, step);
    }

    void addHorizontalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addVerticalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addSoundfile(const char*, const char*, Soundfile**) override {}

    void declare(FAUSTFLOAT* zone, const char* key, const char* value) override {
        const auto k = juce::String::fromUTF8(key != nullptr ? key : "").toLowerCase();
        const auto v = juce::String::fromUTF8(value != nullptr ? value : "");
        ControlMetadata m;
        applyFaustAnnotation(k, v, m);
        if (zone == nullptr) {
            if (!groupStack.empty())
                mergeFaustMetadata(groupStack.back(), m);
        } else {
            mergeFaustMetadata(pendingByZone[zone], m);
        }
    }
};

}  // namespace

FaustInstrumentPlugin::FaustState::~FaustState() {
    poly.reset();  // deletes the per-voice DSPs it owns
    if (factory)
        deleteInterpreterDSPFactory(factory);
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
    auto* factory = createInterpreterDSPFactoryFromString(
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

    VoiceHarvester harvester;
    state->poly->buildUserInterface(&harvester);

    // Group the per-voice controls by label. Skip the shared proxy box and the
    // reserved MIDI controls (freq/gain/gate). The first occurrence supplies
    // range/metadata; every occurrence contributes a voice zone.
    struct Grouped {
        HarvestedControl rep;
        std::vector<FAUSTFLOAT*> zones;
    };
    std::vector<Grouped> groups;
    std::map<juce::String, size_t> byLabel;
    for (const auto& c : harvester.raw) {
        if (c.fromProxyGroup || isReservedVoiceControl(c.label))
            continue;
        auto it = byLabel.find(c.label);
        if (it == byLabel.end()) {
            Grouped g;
            g.rep.kind = c.kind;
            g.rep.label = c.label;
            g.rep.minValue = c.minValue;
            g.rep.maxValue = c.maxValue;
            g.rep.stepValue = c.stepValue;
            g.rep.defaultValue = c.defaultValue;
            g.rep.zone = c.zone;  // representative = first voice's zone
            g.rep.metadata = c.metadata;
            g.rep.group = c.group;
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

    DBG("[FaustInstrument] harvested " << static_cast<int>(harvester.raw.size())
                                       << " raw controls -> " << static_cast<int>(reps.size())
                                       << " user params (excl. freq/gain/gate)");

    auto report = pool_.rebindFromHarvest(reps);
    state->activeBindings = std::move(report.activeBindings);
    lastDiagnostics_ = std::move(report.diagnostics);

    // Map each active slot to the full per-voice zone list for its control.
    for (const auto& b : state->activeBindings) {
        if (b.slotIndex < 0 || b.slotIndex >= FaustParamPool::kSize || !b.zone)
            continue;
        if (auto it = zonesByRep.find(b.zone); it != zonesByRep.end())
            state->voiceZonesBySlot[static_cast<size_t>(b.slotIndex)] = it->second;
    }

    initialiseUnsetPoolValues(state->activeBindings, previousSlots);

    DBG("[FaustInstrument] pool active=" << pool_.activeCount() << " bindings="
                                         << static_cast<int>(state->activeBindings.size()));
    for (const auto& d : lastDiagnostics_)
        DBG("  diagnostic: " << d);
    return state;
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

    const auto savedSource = state.getProperty("dspSource", juce::String()).toString();
    const auto savedName = state.getProperty("dspName", juce::String()).toString();
    const auto savedViewKindRaw = static_cast<int>(
        state.getProperty("dspViewKind", static_cast<int>(FaustCustomViewKind::None)));

    juce::String err;
    auto compiled = compileAndRebind(
        savedSource.isNotEmpty() ? savedSource : juce::String(kDefaultDspSource), err);
    if (!compiled) {
        DBG("FaustInstrumentPlugin: failed to compile saved source: " << err << " — using default");
        compiled = compileAndRebind(kDefaultDspSource, err);
    }

    dspSource_ = savedSource.isNotEmpty() ? savedSource : juce::String(kDefaultDspSource);
    dspName_ = savedName.isNotEmpty() ? savedName : juce::String("Faust Poly Synth");
    viewKind_ = static_cast<FaustCustomViewKind>(savedViewKindRaw);

    std::atomic_store(&active_, compiled);

    state.setProperty("dspSource", dspSource_, nullptr);
    state.setProperty("dspName", dspName_, nullptr);
    state.setProperty("dspViewKind", static_cast<int>(viewKind_), nullptr);

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
                                          juce::String& errorOut, FaustCustomViewKind viewKind) {
    auto compiled = compileAndRebind(source, errorOut);
    if (!compiled)
        return false;

    auto previous = std::atomic_load(&active_);
    std::atomic_store(&active_, compiled);

    if (previous) {
        const juce::ScopedLock lk(retiredLock_);
        retired_.push_back({previous, juce::Time::getMillisecondCounter()});
    }

    dspName_ = name;
    dspSource_ = source;
    viewKind_ = viewKind;
    state.setProperty("dspName", dspName_, getUndoManager());
    state.setProperty("dspSource", dspSource_, getUndoManager());
    state.setProperty("dspViewKind", static_cast<int>(viewKind_), getUndoManager());

    DBG("FaustInstrumentPlugin::loadDspSource ok name=" << name << " out=" << compiled->dspOut
                                                        << " active=" << pool_.activeCount());
    return true;
}

void FaustInstrumentPlugin::initialise(const te::PluginInitialisationInfo& info) {
    currentSampleRate_ = static_cast<int>(info.sampleRate);

    if (auto state = std::atomic_load(&active_)) {
        if (state->poly)
            state->poly->instanceInit(currentSampleRate_);
    }

    const int dspOut = std::atomic_load(&active_) ? std::atomic_load(&active_)->dspOut : 2;
    scratchOut_.setSize(std::max(dspOut, 2), info.blockSizeSamples, false, true, false);

    DBG("FaustInstrumentPlugin::initialise sr=" << currentSampleRate_
                                                << " blockSize=" << info.blockSizeSamples);
}

void FaustInstrumentPlugin::deinitialise() {}

void FaustInstrumentPlugin::reset() {
    if (auto state = std::atomic_load(&active_)) {
        if (state->poly)
            state->poly->instanceClear();
    }
}

void FaustInstrumentPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.destBuffer || fc.bufferNumSamples <= 0)
        return;

    auto active = std::atomic_load(&active_);
    if (!active || !active->poly)
        return;

    // Apply user parameter values: denormalize once per slot, then fan the
    // value out to every voice's zone (plain pointer writes — RT-safe).
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
    }

    // Drive voice allocation from this block's MIDI. Timing offsets within the
    // block are ignored for the POC (all events applied before compute).
    if (fc.bufferForMidiMessages != nullptr && !fc.bufferForMidiMessages->isEmpty()) {
        for (auto& m : *fc.bufferForMidiMessages) {
            if (m.isNoteOn()) {
                active->poly->keyOn(m.getChannel(), m.getNoteNumber(), m.getVelocity());
            } else if (m.isNoteOff()) {
                active->poly->keyOff(m.getChannel(), m.getNoteNumber(), m.getVelocity());
            } else if (m.isPitchWheel()) {
                active->poly->pitchWheel(m.getChannel(), m.getPitchWheelValue());
            } else if (m.isController()) {
                // Includes CC 120/123 (all sound/notes off), which the Faust
                // MIDI handler turns into keyOff across active voices.
                active->poly->ctrlChange(m.getChannel(), m.getControllerNumber(),
                                         m.getControllerValue());
            }
        }
    }

    const int hostChannels = fc.destBuffer->getNumChannels();
    const int n = fc.bufferNumSamples;
    const int start = fc.bufferStartSample;
    const int dspOut = active->dspOut;

    if (hostChannels <= 0 || dspOut <= 0 || scratchOut_.getNumSamples() <= 0)
        return;

    // Render the poly synth into scratch (compute() overwrites its outputs),
    // then ADD into destBuffer so we don't clobber any existing signal.
    // Chunk to MIX_BUFFER_SIZE — mydsp_poly's internal mix buffers cap there.
    outPtrs_.resize(static_cast<size_t>(dspOut));
    const int maxChunk = std::min(MIX_BUFFER_SIZE, scratchOut_.getNumSamples());

    for (int offset = 0; offset < n; offset += maxChunk) {
        const int chunk = std::min(maxChunk, n - offset);
        for (int ch = 0; ch < dspOut; ++ch)
            outPtrs_[static_cast<size_t>(ch)] =
                scratchOut_.getWritePointer(ch % scratchOut_.getNumChannels());

        active->poly->compute(chunk, nullptr, outPtrs_.data());

        for (int ch = 0; ch < hostChannels; ++ch) {
            // One-output ("mono") DSP drives both channels; otherwise channel-map.
            const int srcCh = (dspOut == 1) ? 0 : (ch % dspOut);
            fc.destBuffer->addFrom(ch, start + offset, scratchOut_, srcCh, 0, chunk);
        }
    }
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
