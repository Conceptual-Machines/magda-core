#pragma once

#include <faust/dsp/dsp.h>
#include <faust/gui/UI.h>
#include <faust/gui/meta.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <string>
#include <vector>

namespace magda::daw::audio::compiled {

/**
 * @brief Minimal UI harvester that records every numeric control declared
 *        by a compiled Faust DSP.
 *
 * Faust DSP classes call `buildUserInterface(UI*)` to publish their
 * controls — sliders, nentries, buttons, checkboxes — as paths into a
 * `UI` interface. This concrete UI implementation just stores each
 * control's address (the float zone), label, range, and metadata so a
 * MAGDA plugin can later wrap them as `te::AutomatableParameter`s.
 *
 * Group / metadata calls are recorded but otherwise ignored — bespoke
 * MAGDA UIs (like `MagdaDriveCurveView`) handle styling, this layer
 * just gives downstream code structured access to "what controls exist
 * and where do they live in memory".
 */
struct HarvestedControl {
    enum class Kind { HSlider, VSlider, NumEntry, Button, CheckBox };

    Kind kind = Kind::HSlider;
    std::string label;
    FAUSTFLOAT* zone = nullptr;
    FAUSTFLOAT init = 0.0f;
    FAUSTFLOAT min = 0.0f;
    FAUSTFLOAT max = 1.0f;
    FAUSTFLOAT step = 0.0f;
    // Active group path at the time the control was added — useful if a
    // future device wants to mirror the Faust hierarchy in MAGDA's UI.
    std::vector<std::string> groupPath;
    // Last metadata key/value added before this control was declared.
    // Faust calls declare(zone, key, value) BEFORE addControl; we cache
    // pairs here for the most recent zone.
    std::vector<std::pair<std::string, std::string>> meta;
};

class HarvestUI : public ::UI {
  public:
    void openTabBox(const char*) override {
        groupPath_.emplace_back();
    }
    void openHorizontalBox(const char* label) override {
        groupPath_.emplace_back(label ? label : "");
    }
    void openVerticalBox(const char* label) override {
        groupPath_.emplace_back(label ? label : "");
    }
    void closeBox() override {
        if (!groupPath_.empty())
            groupPath_.pop_back();
    }

    void addButton(const char* label, FAUSTFLOAT* zone) override {
        emit(HarvestedControl::Kind::Button, label, zone, 0, 0, 1, 1);
    }
    void addCheckButton(const char* label, FAUSTFLOAT* zone) override {
        emit(HarvestedControl::Kind::CheckBox, label, zone, 0, 0, 1, 1);
    }
    void addVerticalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min,
                           FAUSTFLOAT max, FAUSTFLOAT step) override {
        emit(HarvestedControl::Kind::VSlider, label, zone, init, min, max, step);
    }
    void addHorizontalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min,
                             FAUSTFLOAT max, FAUSTFLOAT step) override {
        emit(HarvestedControl::Kind::HSlider, label, zone, init, min, max, step);
    }
    void addNumEntry(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min,
                     FAUSTFLOAT max, FAUSTFLOAT step) override {
        emit(HarvestedControl::Kind::NumEntry, label, zone, init, min, max, step);
    }
    void addHorizontalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addVerticalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addSoundfile(const char*, const char*, Soundfile**) override {}

    void declare(FAUSTFLOAT* zone, const char* key, const char* val) override {
        pendingMeta_.emplace_back(key ? key : "", val ? val : "");
        pendingMetaZone_ = zone;
    }

    const std::vector<HarvestedControl>& controls() const {
        return controls_;
    }

  private:
    void emit(HarvestedControl::Kind kind, const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init,
              FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step) {
        HarvestedControl c;
        c.kind = kind;
        c.label = label ? label : "";
        c.zone = zone;
        c.init = init;
        c.min = min;
        c.max = max;
        c.step = step;
        c.groupPath = groupPath_;
        if (pendingMetaZone_ == zone)
            c.meta = std::move(pendingMeta_);
        pendingMeta_.clear();
        pendingMetaZone_ = nullptr;
        controls_.push_back(std::move(c));
    }

    std::vector<std::string> groupPath_;
    std::vector<HarvestedControl> controls_;
    std::vector<std::pair<std::string, std::string>> pendingMeta_;
    FAUSTFLOAT* pendingMetaZone_ = nullptr;
};

/**
 * @brief Audio-thread runner for a compiled Faust DSP.
 *
 * Owns one DSP instance and a scratch buffer for input copying (Faust
 * doesn't allow input/output aliasing unless compiled with `-inpl`).
 * `prepare()` is message-thread; `process()` is audio-thread and
 * lock-free.
 */
template <typename DspClass> class CompiledFaustHost {
  public:
    DspClass& dsp() {
        return dsp_;
    }
    const DspClass& dsp() const {
        return dsp_;
    }

    void prepare(int sampleRate) {
        dsp_.init(sampleRate);
        scratchIn_.setSize(dsp_.getNumInputs(), 0, false, true, true);
        inPtrs_.assign(static_cast<size_t>(dsp_.getNumInputs()), nullptr);
        outPtrs_.assign(static_cast<size_t>(dsp_.getNumOutputs()), nullptr);
    }

    void process(juce::AudioBuffer<float>& io, int startSample, int numSamples) {
        const int nIn = dsp_.getNumInputs();
        const int nOut = dsp_.getNumOutputs();
        const int chans = juce::jmin(io.getNumChannels(), juce::jmax(nIn, nOut));
        if (chans == 0 || numSamples <= 0)
            return;

        // Make sure the scratch buffer has room for this block.
        if (scratchIn_.getNumChannels() != nIn || scratchIn_.getNumSamples() < numSamples)
            scratchIn_.setSize(nIn, numSamples, false, true, true);

        // Copy in (no aliasing).
        for (int ch = 0; ch < nIn; ++ch) {
            const float* src =
                (ch < io.getNumChannels()) ? io.getReadPointer(ch, startSample) : nullptr;
            float* dst = scratchIn_.getWritePointer(ch);
            if (src != nullptr)
                std::copy(src, src + numSamples, dst);
            else
                std::fill(dst, dst + numSamples, 0.0f);
            inPtrs_[static_cast<size_t>(ch)] = dst;
        }

        for (int ch = 0; ch < nOut; ++ch) {
            outPtrs_[static_cast<size_t>(ch)] =
                (ch < io.getNumChannels()) ? io.getWritePointer(ch, startSample) : nullptr;
        }

        dsp_.compute(numSamples, inPtrs_.data(), outPtrs_.data());
    }

    /// Build the harvest by calling the DSP's buildUserInterface on a
    /// fresh HarvestUI. Call once after construction.
    void harvest(HarvestUI& ui) {
        dsp_.buildUserInterface(&ui);
    }

  private:
    DspClass dsp_;
    juce::AudioBuffer<float> scratchIn_;
    std::vector<float*> inPtrs_;
    std::vector<float*> outPtrs_;
};

}  // namespace magda::daw::audio::compiled
