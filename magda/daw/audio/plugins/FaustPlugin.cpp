#include "plugins/FaustPlugin.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <regex>

#include "FaustMetadataParser.hpp"
#include "FaustResources.hpp"
#include "faust/dsp/dsp.h"
#include "faust/dsp/interpreter-dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"

namespace magda::daw::audio {

const char* FaustPlugin::xmlTypeName = "faust";

namespace {

// Default DSP source used when a fresh FaustPlugin is created without a saved
// .dsp source. Stereo passthrough — no stdfaust.lib import so it compiles even
// before bundled libraries are wired into the search path.
constexpr const char* kDefaultDspSource = R"FAUST(
declare name "Passthrough";
process = _, _;
)FAUST";

// Faust UI subclass that walks the live DSP's control tree and produces
// HarvestedControls with already-cleaned labels and merged metadata.
//
// Faust's metadata model: declare(zone, key, value) calls precede the
// add* call for that zone. When zone is null the declare is at group
// scope (between the surrounding open*Box / closeBox). Group labels can
// also carry annotations directly.
struct UIHarvester : public ::UI {
    std::vector<HarvestedControl> harvested;

    // Stack of group-scope ControlMetadata, one frame per open box.
    std::vector<ControlMetadata> groupStack;
    // Pending control-level declares for the next addXxx, keyed by zone.
    std::map<FAUSTFLOAT*, ControlMetadata> pendingByZone;

    void pushGroup(const char* label) {
        auto parsed = parseFaustLabel(juce::String::fromUTF8(label != nullptr ? label : ""));
        groupStack.push_back(parsed.metadata);
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

        HarvestedControl h;
        h.kind = kind;
        h.label = parsed.cleanLabel;
        h.minValue = static_cast<float>(min);
        h.maxValue = static_cast<float>(max);
        h.stepValue = static_cast<float>(step);
        h.defaultValue = static_cast<float>(init);
        h.zone = zone;
        h.metadata = std::move(merged);
        harvested.push_back(std::move(h));
    }

    // Layout
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
        if (!groupStack.empty())
            groupStack.pop_back();
    }

    // Active widgets
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

    // Passive — bargraphs and soundfiles are out of scope; ignored.
    void addHorizontalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addVerticalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addSoundfile(const char*, const char*, Soundfile**) override {}

    // Metadata
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

// Map a normalized 0..1 value from the AutomatableParameter back to
// the real units the live zone expects, using the binding's frozen
// metadata. Audio-thread hot path — no allocation.
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
            // Apply the anchor skew here to match
            // ParameterUtils::realToNormalized's inverse on the host
            // side. Without it the slider→AutomatableParameter→zone
            // round-trip squashes mid-range values (a 1 kHz cutoff with
            // a 1 kHz anchor lands at ~632 Hz on the audio thread).
            // pow(0.5, skew) == anchorRatio at slider midpoint, so we
            // pre-skew n by `skew` before projecting. NaN anchor (or
            // out-of-range) skips the skew.
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

// Guarantee the standard library is available without relying on the source (or
// the LLM prompt) to include it. Prepend the import only if it isn't there, so
// we never produce a duplicate.
juce::String ensureStdfaustImport(const juce::String& source) {
    if (source.contains("stdfaust.lib"))
        return source;
    return juce::String("import(\"stdfaust.lib\");\n") + source;
}

// Wrap a one-output ("mono") DSP so it runs as genuine dual-mono: each channel
// processed independently (separate state), giving real 2-out behaviour instead
// of a silent right channel. Renames the user's `process` and re-defines it via
// `par`. Returns an empty string if there's no top-level `process` to wrap, in
// which case the caller keeps the original source.
juce::String wrapDualMono(const juce::String& source) {
    const std::string s = source.toStdString();
    // Match the top-level `process =` definition (at the start of a line, after
    // optional indentation). After ensureStdfaustImport, process is never at the
    // very start of the string, so requiring a preceding newline is safe.
    static const std::regex procDef(R"((\n[ \t]*)process([ \t]*=))");
    const std::string renamed =
        std::regex_replace(s, procDef, "$1__magda_user$2", std::regex_constants::format_first_only);
    if (renamed == s)
        return {};  // no process definition found; don't attempt the wrap
    return juce::String(renamed) + "\nprocess = par(i, 2, __magda_user);\n";
}

}  // namespace

FaustPlugin::FaustState::~FaustState() {
    dsp.reset();
    if (factory)
        deleteInterpreterDSPFactory(factory);
}

std::shared_ptr<FaustPlugin::FaustState> FaustPlugin::compile(const juce::String& source,
                                                              int sampleRate,
                                                              juce::String& errorOut) {
    // Pass the bundled faustlibraries dir as `-I` so `import("stdfaust.lib")`
    // and friends resolve at compile time. The path may not exist when running
    // outside the installed bundle (e.g. unit tests) — libfaust falls back to
    // its built-in search paths and a DSP that doesn't import any libs still
    // compiles.
    const auto libsPath = getFaustLibrariesPath().getFullPathName().toStdString();

    // Compile one source string into a fully-initialised FaustState (or null).
    auto compileSource = [&](const juce::String& s,
                             juce::String& e) -> std::shared_ptr<FaustState> {
        std::string err;
        const auto src = s.toStdString();
        std::vector<const char*> argv;
        argv.push_back("-I");
        argv.push_back(libsPath.c_str());

        auto* factory = createInterpreterDSPFactoryFromString(
            "magda_faust", src, static_cast<int>(argv.size()), argv.data(), err);
        if (!factory) {
            e = juce::String(err);
            return nullptr;
        }
        auto state = std::make_shared<FaustState>();
        state->factory = factory;
        state->dsp.reset(factory->createDSPInstance());
        if (!state->dsp) {
            e = "createDSPInstance returned null";
            return nullptr;  // ~FaustState will deleteInterpreterDSPFactory
        }
        state->dsp->init(sampleRate);
        state->dspIn = state->dsp->getNumInputs();
        state->dspOut = state->dsp->getNumOutputs();
        return state;
    };

    const juce::String normalised = ensureStdfaustImport(source);

    auto state = compileSource(normalised, errorOut);
    if (!state)
        return nullptr;

    // A one-output DSP would only drive the left channel and leave the right
    // silent. Re-wrap it as dual-mono so both channels are processed. Keep the
    // original if the wrap can't be built or doesn't compile (never a regression).
    if (state->dspOut == 1 && state->dspIn <= 1) {
        const juce::String wrapped = wrapDualMono(normalised);
        if (wrapped.isNotEmpty()) {
            juce::String wrapErr;
            auto wrappedState = compileSource(wrapped, wrapErr);
            if (wrappedState && wrappedState->dspOut >= 2)
                return wrappedState;
        }
    }
    return state;
}

std::shared_ptr<FaustPlugin::FaustState> FaustPlugin::compileAndRebind(const juce::String& source,
                                                                       juce::String& errorOut) {
    auto state = compile(source, currentSampleRate_, errorOut);
    if (!state) {
        DBG("[FaustPlugin] compileAndRebind: compile FAILED: " << errorOut);
        return nullptr;
    }

    std::array<FaustParamSlot, FaustParamPool::kSize> previousSlots{};
    for (int i = 0; i < FaustParamPool::kSize; ++i)
        previousSlots[static_cast<size_t>(i)] = pool_.slot(i);

    UIHarvester harvester;
    state->dsp->buildUserInterface(&harvester);
    DBG("[FaustPlugin] compileAndRebind: harvested " << static_cast<int>(harvester.harvested.size())
                                                     << " controls from DSP");
    for (size_t i = 0; i < harvester.harvested.size(); ++i) {
        const auto& h = harvester.harvested[i];
        DBG("  [" << static_cast<int>(i) << "] kind=" << (int)h.kind << " label='" << h.label
                  << "' min=" << h.minValue << " max=" << h.maxValue
                  << " idx=" << h.metadata.slotIndex << " menu=" << (int)h.metadata.isMenuStyle);
    }

    auto report = pool_.rebindFromHarvest(harvester.harvested);
    state->activeBindings = std::move(report.activeBindings);
    lastDiagnostics_ = std::move(report.diagnostics);
    initialiseUnsetPoolValues(state->activeBindings, previousSlots);

    DBG("[FaustPlugin] compileAndRebind: pool active="
        << pool_.activeCount() << " bindings=" << static_cast<int>(state->activeBindings.size()));
    for (const auto& d : lastDiagnostics_)
        DBG("  diagnostic: " << d);

    return state;
}

void FaustPlugin::initialiseUnsetPoolValues(
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

FaustPlugin::FaustPlugin(const te::PluginCreationInfo& info) : te::Plugin(info) {
    // Pre-create the lifetime-stable pool of AutomatableParameters. Each
    // slot's parameter is normalized 0..1; the audio thread denormalizes
    // per active binding's frozen metadata when writing to zones.
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
        DBG("FaustPlugin: failed to compile saved source: " << err << " — using default");
        compiled = compileAndRebind(kDefaultDspSource, err);
    }

    dspSource_ = savedSource.isNotEmpty() ? savedSource : juce::String(kDefaultDspSource);
    dspName_ = savedName.isNotEmpty() ? savedName : juce::String("Passthrough");
    viewKind_ = static_cast<FaustCustomViewKind>(savedViewKindRaw);

    std::atomic_store(&active_, compiled);

    state.setProperty("dspSource", dspSource_, nullptr);
    state.setProperty("dspName", dspName_, nullptr);
    state.setProperty("dspViewKind", static_cast<int>(viewKind_), nullptr);

    retireTimer_.startTimer(100);

    DBG("FaustPlugin ctor: name=" << dspName_ << " in=" << (compiled ? compiled->dspIn : -1)
                                  << " out=" << (compiled ? compiled->dspOut : -1)
                                  << " active=" << pool_.activeCount());
}

FaustPlugin::~FaustPlugin() {
    notifyListenersOfDeletion();
    retireTimer_.stopTimer();
    for (auto& p : poolParams_) {
        if (p)
            p->detachFromCurrentValue();
    }
    // Drop the active state and any retired ones synchronously here on the
    // message thread; ~Plugin guarantees the audio thread has stopped calling
    // applyToBuffer by the time we run.
    std::atomic_store(&active_, std::shared_ptr<FaustState>{});
    {
        const juce::ScopedLock lk(retiredLock_);
        retired_.clear();
    }
}

void FaustPlugin::drainRetired() {
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

bool FaustPlugin::loadDspSource(const juce::String& name, const juce::String& source,
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

    DBG("FaustPlugin::loadDspSource ok name=" << name << " in=" << compiled->dspIn
                                              << " out=" << compiled->dspOut
                                              << " active=" << pool_.activeCount());
    return true;
}

void FaustPlugin::stageSourceForEditing(const juce::String& name, const juce::String& source) {
    // Editable state only — no compileAndRebind, no active_ swap. The live DSP
    // and the param pool stay as they are; the editor reads dspSource/dspName
    // from state, so the user sees the staged code and compiles it when ready.
    dspName_ = name;
    dspSource_ = source;
    state.setProperty("dspName", dspName_, getUndoManager());
    state.setProperty("dspSource", dspSource_, getUndoManager());
}

void FaustPlugin::initialise(const te::PluginInitialisationInfo& info) {
    currentSampleRate_ = static_cast<int>(info.sampleRate);

    if (auto state = std::atomic_load(&active_)) {
        if (state->dsp)
            state->dsp->instanceInit(currentSampleRate_);
    }

    const int dspIn = std::atomic_load(&active_) ? std::atomic_load(&active_)->dspIn : 0;
    const int maxChannels = std::max(dspIn, 8);
    scratchIn_.setSize(maxChannels, info.blockSizeSamples, false, true, false);

    DBG("FaustPlugin::initialise sr=" << currentSampleRate_
                                      << " blockSize=" << info.blockSizeSamples);
}

void FaustPlugin::deinitialise() {}

void FaustPlugin::reset() {
    if (auto state = std::atomic_load(&active_)) {
        if (state->dsp)
            state->dsp->instanceClear();
    }
}

void FaustPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.destBuffer || fc.bufferNumSamples <= 0)
        return;

    auto active = std::atomic_load(&active_);
    if (!active || !active->dsp)
        return;

    // Audio-thread contract: read pool param values (TE wait-free) and
    // each binding's frozen metadata (immutable for the state's
    // lifetime). Never read the pool's slot table here — that's
    // mutated on the message thread by `loadDspSource`.
    for (const auto& b : active->activeBindings) {
        if (!b.zone)
            continue;
        if (b.slotIndex < 0 || b.slotIndex >= FaustParamPool::kSize)
            continue;
        // Non-User roles (e.g. ProjectTempo) have their zones written
        // by the host below — don't overwrite them with the unused
        // CachedValue stored on the AutomatableParameter.
        if (b.role != FaustControlRole::User)
            continue;
        const auto& param = poolParams_[static_cast<size_t>(b.slotIndex)];
        if (!param)
            continue;
        const float normalized = param->getCurrentValue();
        *b.zone = static_cast<FAUSTFLOAT>(denormalizeForBinding(b, normalized));
    }

    // Host-supplied controls. Currently just ProjectTempo — sample the
    // edit's tempo sequence at this block's start once and write the
    // BPM into every binding tagged ProjectTempo. (Multiple tempo
    // slots in one DSP would be unusual but cost nothing to support.)
    {
        double cachedBpm = -1.0;
        for (const auto& b : active->activeBindings) {
            if (!b.zone || b.role != FaustControlRole::ProjectTempo)
                continue;
            if (cachedBpm < 0.0) {
                cachedBpm = edit.tempoSequence.getBpmAt(fc.editTime.getStart());
            }
            *b.zone = static_cast<FAUSTFLOAT>(cachedBpm);
        }
    }

    const int hostChannels = fc.destBuffer->getNumChannels();
    const int n = fc.bufferNumSamples;
    const int start = fc.bufferStartSample;

    if (hostChannels <= 0 || active->dspIn <= 0 || active->dspOut <= 0)
        return;

    if (scratchIn_.getNumSamples() < n)
        return;

    inPtrs_.resize(static_cast<size_t>(active->dspIn));
    outPtrs_.resize(static_cast<size_t>(active->dspOut));

    for (int ch = 0; ch < active->dspIn; ++ch) {
        float* dst = scratchIn_.getWritePointer(ch);
        if (ch < hostChannels) {
            const float* src = fc.destBuffer->getReadPointer(ch, start);
            std::copy(src, src + n, dst);
        } else {
            std::fill(dst, dst + n, 0.0f);
        }
        inPtrs_[static_cast<size_t>(ch)] = dst;
    }

    const int writableOut = std::min(active->dspOut, hostChannels);
    for (int ch = 0; ch < writableOut; ++ch)
        outPtrs_[static_cast<size_t>(ch)] = fc.destBuffer->getWritePointer(ch, start);
    for (int ch = writableOut; ch < active->dspOut; ++ch)
        outPtrs_[static_cast<size_t>(ch)] =
            scratchIn_.getWritePointer(ch % scratchIn_.getNumChannels());

    active->dsp->compute(n, inPtrs_.data(), outPtrs_.data());
}

void FaustPlugin::restorePluginStateFromValueTree(const juce::ValueTree& v) {
    const auto savedSource = v.getProperty("dspSource", juce::String()).toString();
    const auto savedName = v.getProperty("dspName", juce::String()).toString();

    if (savedSource.isNotEmpty() && savedSource != dspSource_) {
        juce::String err;
        if (!loadDspSource(savedName.isNotEmpty() ? savedName : juce::String("Loaded"), savedSource,
                           err)) {
            DBG("FaustPlugin::restore: compile failed: " << err);
        }
    }

    // Pool param values are bound to the plugin's state ValueTree under
    // stable IDs (param_01 … param_64), so restoring those properties
    // automatically refreshes each AutomatableParameter via
    // updateFromAttachedValue below.
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
