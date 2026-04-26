#include "FaustPlugin.hpp"

#include <algorithm>

// Vendored Faust runtime headers (from third_party/faust-runtime/). Must
// precede the generated DSP header, which references ::dsp, UI and Meta.
// Kept out of FaustPlugin.hpp so consumers of the plugin don't need Faust
// on their include path.
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"

// Generated from magda/daw/audio/faust_dsp/magda_drive.dsp via scripts/regen-faust.sh.
#include "faust_dsp/magda_drive_generated.hpp"

namespace magda::daw::audio {

const char* FaustPlugin::xmlTypeName = "faust";

namespace {

// Slugify a Faust slider label ("Cutoff", "Drive", "Gain") into a stable
// identifier usable as a juce::Identifier / param ID. Lowercases, strips any
// non-alphanumeric. If the .dsp ever renames a slider, existing saved projects
// will drop that param back to its default — acknowledged POC limitation.
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

// Minimal UI visitor — harvests slider metadata from the Faust DSP's
// buildUserInterface() callback. Everything else is a no-op: our .dsp uses only
// sliders, and layout boxes don't need to influence param registration for the
// POC. Stage 2 / shipping versions will want richer handling (bargraphs for
// metering, buttons for triggers, grouped boxes, etc.).
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

    // Layout — we don't care about nesting for the POC.
    void openTabBox(const char*) override {}
    void openHorizontalBox(const char*) override {}
    void openVerticalBox(const char*) override {}
    void closeBox() override {}

    // Buttons / checkboxes — our .dsp has none; silently ignore.
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

    // Passive displays — Stage 1 doesn't route these anywhere.
    void addHorizontalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addVerticalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addSoundfile(const char*, const char*, Soundfile**) override {}
};

}  // namespace

FaustPlugin::FaustPlugin(const te::PluginCreationInfo& info) : te::Plugin(info) {
    faustDsp_ = std::make_unique<MagdaDriveDsp>();

    // Initialise with a provisional sample rate so instanceResetUserInterface()
    // seeds each zone with its declared default before we read them back as
    // CachedValue defaults. initialise() will re-run instanceInit() with the
    // real SR; param values are re-pushed from AutomatableParameter on every
    // applyToBuffer() call, so the reset-to-default there is harmless.
    faustDsp_->init(44100);

    dspIn_ = faustDsp_->getNumInputs();
    dspOut_ = faustDsp_->getNumOutputs();

    ParamHarvestingUI harvester;
    faustDsp_->buildUserInterface(&harvester);

    auto* um = getUndoManager();

    for (const auto& s : harvester.sliders) {
        auto binding = std::make_unique<ParamBinding>();
        binding->id = slugifyForParamId(s.label);
        binding->label = s.label;
        binding->zone = s.zone;

        juce::NormalisableRange<float> range{static_cast<float>(s.min), static_cast<float>(s.max),
                                              static_cast<float>(s.step)};

        binding->cached.referTo(state, juce::Identifier(binding->id), um,
                                 static_cast<float>(s.init));
        binding->param = addParam(binding->id, binding->label, range);
        binding->param->attachToCurrentValue(binding->cached);

        bindings_.push_back(std::move(binding));
    }

    DBG("FaustPlugin ctor: dspIn=" << dspIn_ << " dspOut=" << dspOut_
                                   << " params=" << static_cast<int>(bindings_.size()));
}

FaustPlugin::~FaustPlugin() {
    notifyListenersOfDeletion();
    for (auto& b : bindings_) {
        if (b->param)
            b->param->detachFromCurrentValue();
    }
}

void FaustPlugin::initialise(const te::PluginInitialisationInfo& info) {
    const auto sr = static_cast<int>(info.sampleRate);
    faustDsp_->instanceInit(sr);

    // Faust requires disjoint input/output buffers. Allocate scratch sized to
    // max(dsp inputs, host channels) so applyToBuffer never reallocates. Host
    // channel count is not exposed here — oversize to a reasonable upper bound.
    const int maxChannels = std::max(dspIn_, 8);
    scratchIn_.setSize(maxChannels, info.blockSizeSamples, false, true, false);

    inPtrs_.resize(static_cast<size_t>(dspIn_));
    outPtrs_.resize(static_cast<size_t>(dspOut_));

    DBG("FaustPlugin::initialise sr=" << sr << " blockSize=" << info.blockSizeSamples);
}

void FaustPlugin::deinitialise() {}

void FaustPlugin::reset() {
    if (faustDsp_)
        faustDsp_->instanceClear();
}

void FaustPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    // POC diagnostic: log the first few calls to confirm we're in the chain.
    static std::atomic<int> callCount{0};
    const int c = callCount.fetch_add(1, std::memory_order_relaxed);
    if (c < 3) {
        DBG("FaustPlugin::applyToBuffer #" << c
                                           << " destBuffer=" << (fc.destBuffer ? "yes" : "no")
                                           << " n=" << fc.bufferNumSamples
                                           << " chans=" << (fc.destBuffer ? fc.destBuffer->getNumChannels() : -1));
    }

    if (!fc.destBuffer || fc.bufferNumSamples <= 0 || !faustDsp_)
        return;

    // Push current automatable values into the Faust zones. Safe on the audio
    // thread — AutomatableParameter::getCurrentValue() is lock-free (same
    // pattern used by ArpeggiatorPlugin / MagdaSamplerPlugin).
    for (auto& b : bindings_) {
        if (b->param && b->zone)
            *b->zone = static_cast<FAUSTFLOAT>(b->param->getCurrentValue());
    }

    const int hostChannels = fc.destBuffer->getNumChannels();
    const int n = fc.bufferNumSamples;
    const int start = fc.bufferStartSample;

    if (hostChannels <= 0 || dspIn_ <= 0 || dspOut_ <= 0)
        return;

    // Copy host audio into scratch (one DSP input channel per iteration). If
    // the host provides fewer channels than the DSP expects, pad the remaining
    // inputs with silence.
    if (scratchIn_.getNumSamples() < n) {
        // Block size grew without initialise() being re-called — skip this
        // block rather than allocating on the audio thread.
        return;
    }

    for (int ch = 0; ch < dspIn_; ++ch) {
        float* dst = scratchIn_.getWritePointer(ch);
        if (ch < hostChannels) {
            const float* src = fc.destBuffer->getReadPointer(ch, start);
            std::copy(src, src + n, dst);
        } else {
            std::fill(dst, dst + n, 0.0f);
        }
        inPtrs_[static_cast<size_t>(ch)] = dst;
    }

    // Output pointers: write DSP outputs directly into the host buffer up to
    // min(dspOut_, hostChannels). Extra host channels pass through unchanged.
    // If dspOut_ > hostChannels, we don't have anywhere to put the extras —
    // route them to the first scratch slot we have available so compute()
    // has a valid destination, then discard.
    const int writableOut = std::min(dspOut_, hostChannels);
    for (int ch = 0; ch < writableOut; ++ch)
        outPtrs_[static_cast<size_t>(ch)] = fc.destBuffer->getWritePointer(ch, start);
    for (int ch = writableOut; ch < dspOut_; ++ch)
        outPtrs_[static_cast<size_t>(ch)] = scratchIn_.getWritePointer(ch % scratchIn_.getNumChannels());

    faustDsp_->compute(n, inPtrs_.data(), outPtrs_.data());
}

void FaustPlugin::restorePluginStateFromValueTree(const juce::ValueTree& v) {
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
