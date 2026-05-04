#include "plugins/FaustPlugin.hpp"

#include <algorithm>
#include <atomic>

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

juce::String slugifyForParamId(const juce::String& label) {
    juce::String out;
    for (auto c : label) {
        if (juce::CharacterFunctions::isLetterOrDigit(c))
            out += juce::CharacterFunctions::toLowerCase(c);
    }
    if (out.isEmpty())
        out = "param";
    return out;
}

struct SliderEntry {
    juce::String label;
    FAUSTFLOAT* zone;
    FAUSTFLOAT init;
    FAUSTFLOAT min;
    FAUSTFLOAT max;
    FAUSTFLOAT step;
};

struct ParamHarvestingUI : public UI {
    std::vector<SliderEntry> sliders;

    void openTabBox(const char*) override {}
    void openHorizontalBox(const char*) override {}
    void openVerticalBox(const char*) override {}
    void closeBox() override {}

    void addButton(const char*, FAUSTFLOAT*) override {}
    void addCheckButton(const char*, FAUSTFLOAT*) override {}

    void addVerticalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min,
                           FAUSTFLOAT max, FAUSTFLOAT step) override {
        sliders.push_back({juce::String(label), zone, init, min, max, step});
    }
    void addHorizontalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min,
                             FAUSTFLOAT max, FAUSTFLOAT step) override {
        sliders.push_back({juce::String(label), zone, init, min, max, step});
    }
    void addNumEntry(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min,
                     FAUSTFLOAT max, FAUSTFLOAT step) override {
        sliders.push_back({juce::String(label), zone, init, min, max, step});
    }

    void addHorizontalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addVerticalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addSoundfile(const char*, const char*, Soundfile**) override {}
};

}  // namespace

FaustPlugin::FaustState::~FaustState() {
    dsp.reset();
    if (factory)
        deleteInterpreterDSPFactory(factory);
}

std::shared_ptr<FaustPlugin::FaustState> FaustPlugin::compile(const juce::String& source,
                                                              int sampleRate,
                                                              juce::String& errorOut) {
    std::string err;
    const auto src = source.toStdString();

    // Pass the bundled faustlibraries dir as `-I` so `import("stdfaust.lib")`
    // and friends resolve at compile time. The path may not exist when running
    // outside the installed bundle (e.g. unit tests) — libfaust falls back to
    // its built-in search paths and a DSP that doesn't import any libs still
    // compiles.
    const auto libsPath = getFaustLibrariesPath().getFullPathName().toStdString();
    std::vector<const char*> argv;
    argv.push_back("-I");
    argv.push_back(libsPath.c_str());

    auto* factory = createInterpreterDSPFactoryFromString(
        "magda_faust", src, static_cast<int>(argv.size()), argv.data(), err);
    if (!factory) {
        errorOut = juce::String(err);
        return nullptr;
    }

    auto state = std::make_shared<FaustState>();
    state->factory = factory;
    state->dsp.reset(factory->createDSPInstance());
    if (!state->dsp) {
        errorOut = "createDSPInstance returned null";
        return nullptr;  // ~FaustState will deleteInterpreterDSPFactory
    }
    state->dsp->init(sampleRate);
    state->dspIn = state->dsp->getNumInputs();
    state->dspOut = state->dsp->getNumOutputs();
    return state;
}

void FaustPlugin::rebuildParameters(const std::shared_ptr<FaustState>& state) {
    for (auto& b : bindings_) {
        if (b->param)
            b->param->detachFromCurrentValue();
    }
    bindings_.clear();
    // Drop the previous DSP's AutomatableParameters from the plugin's
    // parameter list. Without this, params accumulate across DSP swaps and
    // the UI shows the union of every DSP loaded since plugin construction.
    clearParameterList();
    state->zones.clear();

    if (!state->dsp)
        return;

    ParamHarvestingUI harvester;
    state->dsp->buildUserInterface(&harvester);

    auto* um = getUndoManager();
    for (const auto& s : harvester.sliders) {
        auto binding = std::make_unique<ParamBinding>();
        binding->id = slugifyForParamId(s.label);
        binding->label = s.label;
        binding->zone = s.zone;

        juce::NormalisableRange<float> range{static_cast<float>(s.min), static_cast<float>(s.max),
                                             static_cast<float>(s.step)};
        binding->cached.referTo(this->state, juce::Identifier(binding->id), um,
                                static_cast<float>(s.init));
        binding->param = addParam(binding->id, binding->label, range);
        binding->param->attachToCurrentValue(binding->cached);

        state->zones.push_back(s.zone);
        bindings_.push_back(std::move(binding));
    }
}

FaustPlugin::FaustPlugin(const te::PluginCreationInfo& info) : te::Plugin(info) {
    const auto savedSource = state.getProperty("dspSource", juce::String()).toString();
    const auto savedName = state.getProperty("dspName", juce::String()).toString();

    juce::String err;
    auto compiled =
        compile(savedSource.isNotEmpty() ? savedSource : juce::String(kDefaultDspSource),
                currentSampleRate_, err);
    if (!compiled) {
        // Saved source no longer compiles (libraries moved, syntax change,
        // …) — fall back to the default so the plugin always loads.
        DBG("FaustPlugin: failed to compile saved source: " << err << " — using default");
        compiled = compile(kDefaultDspSource, currentSampleRate_, err);
    }

    rebuildParameters(compiled);
    std::atomic_store(&active_, compiled);

    dspSource_ = savedSource.isNotEmpty() ? savedSource : juce::String(kDefaultDspSource);
    dspName_ = savedName.isNotEmpty() ? savedName : juce::String("Passthrough");
    state.setProperty("dspSource", dspSource_, nullptr);
    state.setProperty("dspName", dspName_, nullptr);

    retireTimer_.startTimer(100);

    DBG("FaustPlugin ctor: name=" << dspName_ << " in=" << (compiled ? compiled->dspIn : -1)
                                  << " out=" << (compiled ? compiled->dspOut : -1)
                                  << " params=" << static_cast<int>(bindings_.size()));
}

FaustPlugin::~FaustPlugin() {
    notifyListenersOfDeletion();
    retireTimer_.stopTimer();
    for (auto& b : bindings_) {
        if (b->param)
            b->param->detachFromCurrentValue();
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
            // 200ms is well over any reasonable audio buffer (~10ms at 44.1k),
            // so by now any audio-thread snapshot of this state has been
            // dropped. Final ref drop and dtor run here on the message thread.
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
    auto compiled = compile(source, currentSampleRate_, errorOut);
    if (!compiled)
        return false;

    // Snapshot the outgoing DSP's slider values so a later return to the same
    // .dsp source restores the user's tweaks. Keyed by source so two starters
    // with the same display name can't clash.
    if (dspSource_.isNotEmpty()) {
        auto& entry = savedValuesBySource_[dspSource_];
        for (auto& b : bindings_) {
            if (b->param)
                entry[b->id] = b->param->getCurrentValue();
        }
    }

    auto previous = std::atomic_load(&active_);
    rebuildParameters(compiled);
    std::atomic_store(&active_, compiled);

    // Re-apply previously-seen slider values for this source, if any.
    if (auto it = savedValuesBySource_.find(source); it != savedValuesBySource_.end()) {
        for (auto& b : bindings_) {
            if (auto v = it->second.find(b->id); v != it->second.end()) {
                b->cached = v->second;
                if (b->param)
                    b->param->updateFromAttachedValue();
            }
        }
    }

    if (previous) {
        const juce::ScopedLock lk(retiredLock_);
        retired_.push_back({previous, juce::Time::getMillisecondCounter()});
    }

    dspName_ = name;
    dspSource_ = source;
    state.setProperty("dspName", dspName_, getUndoManager());
    state.setProperty("dspSource", dspSource_, getUndoManager());

    DBG("FaustPlugin::loadDspSource ok name=" << name << " in=" << compiled->dspIn
                                              << " out=" << compiled->dspOut
                                              << " params=" << static_cast<int>(bindings_.size()));
    return true;
}

void FaustPlugin::initialise(const te::PluginInitialisationInfo& info) {
    currentSampleRate_ = static_cast<int>(info.sampleRate);

    // Re-init the live dsp at the host sample rate. Hot-swapping the active
    // shared_ptr is unnecessary — the dsp's internal state is the only thing
    // that depends on SR.
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

    // Push current automatable values into the live dsp's slider zones.
    // bindings_ is owned by the message thread; the audio thread reads
    // ->zone (a stable pointer into the live dsp's memory) and ->param
    // (lock-free getCurrentValue). Safe: bindings_ vector is only mutated
    // under loadDspSource on the message thread, and the active state was
    // installed atomically together with its zones — bindings_ entries
    // referring to the *previous* dsp's zones become stale only after a
    // load, which is allowed to glitch a single block.
    for (auto& b : bindings_) {
        if (b->param && b->zone)
            *b->zone = static_cast<FAUSTFLOAT>(b->param->getCurrentValue());
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

    for (auto& b : bindings_) {
        if (auto p = v.getPropertyPointer(b->cached.getPropertyID()))
            b->cached = static_cast<float>(*p);
        else
            b->cached.resetToDefault();
    }

    for (auto p : getAutomatableParameters())
        p->updateFromAttachedValue();
}

}  // namespace magda::daw::audio
