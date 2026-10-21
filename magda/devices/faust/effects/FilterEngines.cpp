#include "devices/faust/effects/FilterEngines.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_filter_comb.generated.cpp"
#include "magda_filter_diode.generated.cpp"
#include "magda_filter_korg35.generated.cpp"
#include "magda_filter_ladder.generated.cpp"
#include "magda_filter_oberheim.generated.cpp"
#include "magda_filter_sk.generated.cpp"
#include "magda_filter_svf.generated.cpp"
#include "magda_filter_svf_notch.generated.cpp"
#include "magda_filter_svf_staged.generated.cpp"

namespace magda::devices::faust {

namespace {

/// The dsps' [idx:N] numbering.
constexpr int kCutoffIdx = 0;
constexpr int kResonanceIdx = 1;
constexpr int kModeIdx = 3;
constexpr int kSmoothingIdx = 4;
constexpr int kPolesIdx = 5;

/// Each zone by the idx its control declares.
class IdxZones : public ::UI {
  public:
    std::array<float*, 8> zones{};

    void openTabBox(const char*) override {}
    void openHorizontalBox(const char*) override {}
    void openVerticalBox(const char*) override {}
    void closeBox() override {}
    void addButton(const char*, FAUSTFLOAT*) override {}
    void addCheckButton(const char*, FAUSTFLOAT*) override {}
    void addVerticalSlider(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT,
                           FAUSTFLOAT) override {}
    void addHorizontalSlider(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT,
                             FAUSTFLOAT) override {}
    void addNumEntry(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT,
                     FAUSTFLOAT) override {}
    void addHorizontalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addVerticalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addSoundfile(const char*, const char*, Soundfile**) override {}

    void declare(FAUSTFLOAT* zone, const char* key, const char* value) override {
        if (zone == nullptr || key == nullptr || value == nullptr || std::strcmp(key, "idx") != 0)
            return;
        const int idx = std::atoi(value);
        if (idx >= 0 && idx < static_cast<int>(zones.size()))
            zones[static_cast<size_t>(idx)] = zone;
    }
};

constexpr float kMinCutoffHz = 20.0f;
constexpr float kMaxCutoffHz = 20000.0f;

}  // namespace

::dsp* createFilterDsp(int dspIndex) {
    if (dspIndex == kSvfStagedDsp)
        return new MagdaSVFStagedDsp();
    if (dspIndex == kSvfNotchPlusDsp)
        return new MagdaSVFNotchPlusDsp();
    switch (static_cast<FilterFamily>(dspIndex)) {
        case FilterFamily::SVF:
            return new MagdaSVFDsp();
        case FilterFamily::Ladder:
            return new MagdaLadderDsp();
        case FilterFamily::Korg35:
            return new MagdaKorg35Dsp();
        case FilterFamily::Oberheim:
            return new MagdaOberheimDsp();
        case FilterFamily::SallenKey:
            return new MagdaSallenKeyDsp();
        case FilterFamily::Diode:
            return new MagdaDiodeDsp();
        case FilterFamily::Comb:
            return new MagdaCombDsp();
    }
    return nullptr;
}

float clampSallenKeyCutoffHz(float cutoffHz, double sampleRate, float minCutoffHz) {
    if (sampleRate <= 0.0)
        return cutoffHz;

    constexpr float kStableNyquistFraction = 0.84f;
    const float maxCutoffHz = static_cast<float>(sampleRate) * 0.5f * kStableNyquistFraction;
    if (maxCutoffHz <= minCutoffHz)
        return minCutoffHz;
    return std::clamp(cutoffHz, minCutoffHz, maxCutoffHz);
}

FilterBank::FilterBank() = default;
FilterBank::~FilterBank() = default;

void FilterBank::prepare(double sampleRate, float smoothingSeconds) {
    sampleRate_ = sampleRate;
    for (int index = 0; index < kFilterDspCount; ++index) {
        auto& engine = engines_[static_cast<size_t>(index)];
        if (engine.instance == nullptr)
            engine.instance.reset(createFilterDsp(index));
        engine.instance->init(static_cast<int>(sampleRate));

        IdxZones harvested;
        engine.instance->buildUserInterface(&harvested);
        engine.cutoff = harvested.zones[kCutoffIdx];
        engine.resonance = harvested.zones[kResonanceIdx];
        engine.mode = harvested.zones[kModeIdx];
        engine.smoothing = harvested.zones[kSmoothingIdx];
        engine.poles = harvested.zones[kPolesIdx];
        if (engine.smoothing != nullptr)
            *engine.smoothing = smoothingSeconds;
    }
}

void FilterBank::reset(FilterFamily family, int mode, int poleIndex) {
    auto& engine = engines_[static_cast<size_t>(filterDspFor(family, mode, poleIndex))];
    if (engine.instance != nullptr)
        engine.instance->instanceClear();
}

void FilterBank::process(FilterFamily family, int mode, int poleIndex, float cutoffHz,
                         float resonance, const float* const* in, float* const* out,
                         int numSamples) {
    auto& engine = engines_[static_cast<size_t>(filterDspFor(family, mode, poleIndex))];
    if (engine.instance == nullptr) {
        for (int channel = 0; channel < 2; ++channel)
            std::copy_n(in[channel], numSamples, out[channel]);
        return;
    }

    float hz = std::clamp(cutoffHz, kMinCutoffHz, kMaxCutoffHz);
    if (family == FilterFamily::SallenKey)
        hz = clampSallenKeyCutoffHz(hz, sampleRate_, kMinCutoffHz);
    if (engine.cutoff != nullptr)
        *engine.cutoff = hz;
    if (engine.resonance != nullptr)
        *engine.resonance = std::clamp(resonance, 0.0f, 1.0f);
    // A mode the family lacks would select nothing and fall silent.
    if (engine.mode != nullptr)
        *engine.mode = static_cast<float>(mode >= 0 && mode < filterModeCount(family) ? mode : 0);
    if (engine.poles != nullptr)
        *engine.poles = static_cast<float>(std::clamp(poleIndex, 0, 2));

    engine.instance->compute(numSamples, const_cast<float**>(in), const_cast<float**>(out));
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < numSamples; ++i)
            if (!std::isfinite(out[channel][i]))
                out[channel][i] = 0.0f;
}

}  // namespace magda::devices::faust
