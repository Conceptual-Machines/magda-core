#include "plugins/FaustPlugin.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <regex>

#include "FaustBackend.hpp"
#include "FaustResources.hpp"
#include "FaustUIHarvester.hpp"
#include "faust/dsp/dsp.h"
#include "plugins/FaustParamInfo.hpp"

namespace magda::daw::audio {

const char* FaustPlugin::xmlTypeName = "faust-fx";

namespace {

// Default DSP source used when a fresh FaustPlugin is created without a saved
// .dsp source. Stereo passthrough — no stdfaust.lib import so it compiles even
// before bundled libraries are wired into the search path.
constexpr const char* kDefaultDspSource = R"FAUST(
declare name "Passthrough";
declare description "Stereo passthrough. Replace this with your own DSP.";
declare author "MAGDA";
declare license "GPL-3.0";
declare version "1.0";
process = _, _;
)FAUST";

// Map a normalized 0..1 value from the AutomatableParameter back to
// the real units the live zone expects, using the binding's frozen
// metadata. Audio-thread hot path — no allocation.
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
        magda::faust::deleteFactory(factory);
}

std::shared_ptr<FaustPlugin::FaustState> FaustPlugin::compile(const juce::String& source,
                                                              int sampleRate,
                                                              juce::String& errorOut) {
    // Passthrough by contract, without entering libfaust. See FaustResources.hpp.
    if (faustLibraryImportsDisallowed() && source.contains("import(")) {
        errorOut = "Faust library imports are disallowed in this process";
        return nullptr;
    }

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

        auto* factory = magda::faust::createFactoryFromString(
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
            return nullptr;  // ~FaustState will delete the factory
        }
        state->dsp->init(sampleRate);
        state->dspIn = state->dsp->getNumInputs();
        state->dspOut = state->dsp->getNumOutputs();
        return state;
    };

    // In disallowed mode the injection is skipped too: it would turn a
    // self-contained source into an importing one. See FaustResources.hpp.
    const juce::String normalised =
        faustLibraryImportsDisallowed() ? source : ensureStdfaustImport(source);

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

    FaustUIHarvester harvester;
    state->dsp->buildUserInterface(&harvester);
    const auto& harvested = harvester.controls();
    DBG("[FaustPlugin] compileAndRebind: harvested " << static_cast<int>(harvested.size())
                                                     << " controls from DSP");
    for (size_t i = 0; i < harvested.size(); ++i) {
        const auto& h = harvested[i];
        DBG("  [" << static_cast<int>(i) << "] kind=" << (int)h.kind << " label='" << h.label
                  << "' min=" << h.minValue << " max=" << h.maxValue
                  << " idx=" << h.metadata.slotIndex << " choice=" << (int)h.metadata.choiceStyle);
    }

    auto report = pool_.rebindFromHarvest(harvested, harvester.outputs());
    state->activeBindings = std::move(report.activeBindings);
    lastDiagnostics_ = std::move(report.diagnostics);
    if (scratchIn_.getNumSamples() > 0 && state->dspIn > scratchIn_.getNumChannels()) {
        lastDiagnostics_.insert(
            lastDiagnostics_.begin(),
            "DSP needs " + juce::String(state->dspIn) +
                " input channels, but the current audio graph has capacity for " +
                juce::String(scratchIn_.getNumChannels()) +
                "; reload the device or project to activate this patch");
    }
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

        poolValues_[static_cast<size_t>(slotIndex)].store(
            normaliseDefaultForSlot(pool_.slot(slotIndex)), std::memory_order_relaxed);
    }
    refreshPoolDomains();
}

FaustPlugin::FaustPlugin() {
    // The pool is lifetime-stable and starts empty; a saved source arrives
    // later through restoreState(), which recompiles and rebinds onto the same
    // slots. Construction settles on the default patch so the device is
    // answerable before any project has been loaded into it.
    for (auto& value : poolValues_)
        value.store(0.0f, std::memory_order_relaxed);

    juce::String err;
    auto compiled = compileAndRebind(kDefaultDspSource, err);
    activeDspMatchesSource_ = compiled != nullptr;

    dspSource_ = kDefaultDspSource;
    dspName_ = "Passthrough";
    // Derived, not restored: the source is the only thing that has to persist.
    viewName_ = readCustomViewName(dspSource_);

    reservePointerScratch(compiled);
    std::atomic_store(&active_, compiled);

    retireTimer_.startTimer(100);

    DBG("FaustPlugin ctor: name=" << dspName_ << " in=" << (compiled ? compiled->dspIn : -1)
                                  << " out=" << (compiled ? compiled->dspOut : -1)
                                  << " active=" << pool_.activeCount());
}

FaustPlugin::~FaustPlugin() {
    retireTimer_.stopTimer();
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
                                juce::String& errorOut) {
    auto compiled = compileAndRebind(source, errorOut);
    if (!compiled)
        return false;

    auto previous = std::atomic_load(&active_);
    reservePointerScratch(compiled);
    std::atomic_store(&active_, compiled);
    activeDspMatchesSource_ = true;

    // A stereo-only replacement can no longer consume the host's appended key
    // channels. The device says so through properties().canSidechain, which the
    // host re-reads when refreshDeviceParameters() runs after a compile; there
    // is nothing for the device itself to unroute.

    if (previous) {
        const juce::ScopedLock lk(retiredLock_);
        retired_.push_back({previous, juce::Time::getMillisecondCounter()});
    }

    dspName_ = name;
    dspSource_ = source;
    viewName_ = readCustomViewName(source);

    DBG("FaustPlugin::loadDspSource ok name=" << name << " in=" << compiled->dspIn
                                              << " out=" << compiled->dspOut
                                              << " active=" << pool_.activeCount());
    return true;
}

void FaustPlugin::stageSourceForEditing(const juce::String& name, const juce::String& source) {
    // Editable state only — no compileAndRebind, no active_ swap. The live DSP
    // and the param pool stay as they are; the editor reads dspSource/dspName
    // from state, so the user sees the staged code and compiles it when ready.
    activeDspMatchesSource_ = false;
    dspName_ = name;
    dspSource_ = source;
}

void FaustPlugin::prepare(const DevicePrepareContext& context) {
    currentSampleRate_ = static_cast<int>(context.sampleRate);

    if (auto state = std::atomic_load(&active_)) {
        if (state->dsp)
            state->dsp->instanceInit(currentSampleRate_);
    }

    const int dspIn = std::atomic_load(&active_) ? std::atomic_load(&active_)->dspIn : 0;
    const int maxChannels = std::max(dspIn, 8);
    scratchIn_.setSize(maxChannels, context.maximumBlockSize, false, true, false);

    DBG("FaustPlugin::prepare sr=" << currentSampleRate_
                                   << " blockSize=" << context.maximumBlockSize);
}

DeviceProperties FaustPlugin::properties() const {
    // The channel counts are the compiled dsp's own, and more inputs than
    // outputs is how this device says it takes a sidechain key.
    const auto active = std::atomic_load(&active_);
    const int inputCount = active ? active->dspIn : 2;
    const int outputCount = active ? active->dspOut : 2;

    return {
        .pluginId = xmlTypeName,
        .name = getPluginName(),
        .shortName = "Faust",
        .canSidechain = inputCount > 2,
        .outputChannelCount = outputCount,
        .inputChannelCount = inputCount,
    };
}

void FaustPlugin::release() {}

void FaustPlugin::reset() {
    if (auto state = std::atomic_load(&active_)) {
        if (state->dsp)
            state->dsp->instanceClear();
    }
}

void FaustPlugin::reservePointerScratch(const std::shared_ptr<FaustState>& state) {
    if (!state)
        return;
    inPtrs_.reserve(static_cast<size_t>(std::max(0, state->dspIn)));
    outPtrs_.reserve(static_cast<size_t>(std::max(0, state->dspOut)));
}

void FaustPlugin::process(DeviceProcessContext& context) {
    if (context.audio == nullptr || context.numSamples <= 0)
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
        const float normalized =
            poolValues_[static_cast<size_t>(b.slotIndex)].load(std::memory_order_relaxed);
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
                cachedBpm = context.tempoMap != nullptr
                                ? context.tempoMap->bpmAtSeconds(context.timelineStartSeconds)
                                : 120.0;
            }
            *b.zone = static_cast<FAUSTFLOAT>(cachedBpm);
        }
    }

    const int hostChannels = context.audio->getNumChannels();
    const int n = context.numSamples;
    const int start = context.startSample;

    if (hostChannels <= 0 || active->dspIn <= 0 || active->dspOut <= 0)
        return;

    if (scratchIn_.getNumSamples() < n || scratchIn_.getNumChannels() < active->dspIn)
        return;

    inPtrs_.resize(static_cast<size_t>(active->dspIn));
    outPtrs_.resize(static_cast<size_t>(active->dspOut));

    for (int ch = 0; ch < active->dspIn; ++ch) {
        float* dst = scratchIn_.getWritePointer(ch);
        if (ch < hostChannels) {
            const float* src = context.audio->getReadPointer(ch, start);
            std::copy(src, src + n, dst);
        } else {
            std::fill(dst, dst + n, 0.0f);
        }
        inPtrs_[static_cast<size_t>(ch)] = dst;
    }

    const int writableOut = std::min(active->dspOut, hostChannels);
    for (int ch = 0; ch < writableOut; ++ch)
        outPtrs_[static_cast<size_t>(ch)] = context.audio->getWritePointer(ch, start);
    for (int ch = writableOut; ch < active->dspOut; ++ch)
        outPtrs_[static_cast<size_t>(ch)] =
            scratchIn_.getWritePointer(ch % scratchIn_.getNumChannels());

    active->dsp->compute(n, inPtrs_.data(), outPtrs_.data());
}

void FaustPlugin::flushState(juce::ValueTree& state) {
    state.setProperty("dspName", dspName_, nullptr);
    state.setProperty("dspSource", dspSource_, nullptr);
    for (int i = 0; i < FaustParamPool::kSize; ++i)
        state.setProperty(juce::Identifier(poolParamId(i)),
                          poolValues_[static_cast<size_t>(i)].load(std::memory_order_relaxed),
                          nullptr);
}

void FaustPlugin::restoreState(const juce::ValueTree& v) {
    const auto savedSource = v.getProperty("dspSource", juce::String()).toString();
    const auto savedName = v.getProperty("dspName", juce::String()).toString();

    if (savedSource.isNotEmpty() && savedSource != dspSource_) {
        juce::String err;
        if (!loadDspSource(savedName.isNotEmpty() ? savedName : juce::String("Loaded"), savedSource,
                           err)) {
            DBG("FaustPlugin::restore: compile failed: " << err);

            // The saved source is what the project asked for and the live DSP
            // is the passthrough the constructor settled on, which is precisely
            // what activeDspMatchesSource() answers -- and it was left saying
            // the opposite, because nothing on the failure path touched it and
            // the constructor had just set it true.
            //
            // What reads it is the guard that clears a serialized audio
            // sidechain when the live DSP has no key input to route into
            // (clearStaleFaustAudioSidechain). So a project whose Faust source
            // stopped compiling -- an older Faust's syntax, a library that
            // moved -- opened with its sidechain routing deleted rather than
            // held until the source is repaired, which is the one thing this
            // flag exists to prevent.
            //
            // Staged rather than dropped: the source stays editable so whoever
            // opens the project sees what failed and can fix it, which is the
            // same state the editor puts a device in while its code is being
            // worked on.
            stageSourceForEditing(savedName.isNotEmpty() ? savedName : juce::String("Loaded"),
                                  savedSource);
        }
    }

    // Pool values are saved under stable ids (param_01 ... param_64), which is
    // what lets a macro or automation lane keep pointing at the same control
    // across a recompile.
    for (int i = 0; i < FaustParamPool::kSize; ++i) {
        const auto id = poolParamId(i);
        const auto* saved = v.getPropertyPointer(juce::Identifier(id));
        poolValueWasRestored_[static_cast<size_t>(i)] = saved != nullptr;
        poolValues_[static_cast<size_t>(i)].store(
            saved != nullptr ? static_cast<float>(*saved) : 0.0f, std::memory_order_relaxed);
    }
    refreshPoolDomains();
}

void FaustPlugin::refreshPoolDomains() {
    for (int i = 0; i < FaustParamPool::kSize; ++i)
        poolDomains_[static_cast<size_t>(i)] =
            ParameterUtils::domainOf(paramInfoFromSlot(pool_.slot(i)));
}

ParameterInfo FaustPlugin::parameterInfo(int index) const {
    if (index < 0 || index >= FaustParamPool::kSize)
        return {};
    auto info = paramInfoFromSlot(pool_.slot(index));
    info.paramIndex = index;
    info.stableId = poolParamId(index);
    return info;
}

float FaustPlugin::parameterValue(int index) const {
    if (index < 0 || index >= FaustParamPool::kSize)
        return 0.0f;
    return poolValues_[static_cast<size_t>(index)].load(std::memory_order_relaxed);
}

void FaustPlugin::setParameterValue(int index, float value) {
    if (index < 0 || index >= FaustParamPool::kSize)
        return;
    poolValues_[static_cast<size_t>(index)].store(juce::jlimit(0.0f, 1.0f, value),
                                                  std::memory_order_relaxed);
}

}  // namespace magda::daw::audio
