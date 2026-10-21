#pragma once

#include <array>
#include <memory>

class dsp;

namespace magda::devices::faust {

/// The Filter device's engines, in its Engine slot's order.
enum class FilterFamily { SVF, Ladder, Korg35, Oberheim, SallenKey, Diode, Comb };
inline constexpr int kFilterFamilyCount = 7;

/// How many modes @p family's dsp declares: a prefix of LP, BP, HP, Notch, Notch+, except Comb's
/// + and -.
constexpr int filterModeCount(FilterFamily family) {
    switch (family) {
        case FilterFamily::SVF:
            return 5;
        case FilterFamily::Oberheim:
            return 4;
        case FilterFamily::SallenKey:
            return 3;
        case FilterFamily::Korg35:
        case FilterFamily::Comb:
            return 2;
        case FilterFamily::Ladder:
        case FilterFamily::Diode:
            return 1;
    }
    return 1;
}

inline constexpr int kNotchPlusMode = 4;

/// The compiled dsps behind the families. The SVF is three: its 2-pole modes alone stay as cheap
/// as they always were, since Faust runs every branch of a dsp whichever is selected.
inline constexpr int kSvfStagedDsp = kFilterFamilyCount;
inline constexpr int kSvfNotchPlusDsp = kFilterFamilyCount + 1;
inline constexpr int kFilterDspCount = kFilterFamilyCount + 2;

/// The dsp that plays @p family in @p mode with @p poleIndex (2, 4 or 6 poles as 0, 1, 2).
constexpr int filterDspFor(FilterFamily family, int mode, int poleIndex) {
    if (family != FilterFamily::SVF)
        return static_cast<int>(family);
    if (mode == kNotchPlusMode)
        return kSvfNotchPlusDsp;
    return poleIndex > 0 ? kSvfStagedDsp : static_cast<int>(family);
}

::dsp* createFilterDsp(int dspIndex);

/// Sallen-Key goes unstable near Nyquist, so it gets a lower ceiling than the slot's range.
float clampSallenKeyCutoffHz(float cutoffHz, double sampleRate, float minCutoffHz);

/**
 * @brief One stereo filter of each family, driven through its zones rather than a device host:
 *        what a sampler voice runs (#3097).
 *
 * prepare() allocates and runs on the message thread; reset() and process() are audio-thread safe.
 */
class FilterBank {
  public:
    FilterBank();
    ~FilterBank();

    /// @p smoothingSeconds is how fast cutoff and resonance follow a change: the Filter device's
    /// 20 ms would smear a filter envelope.
    void prepare(double sampleRate, float smoothingSeconds);

    /// Clear the state of the dsp playing @p family in @p mode with @p poleIndex, so a new note
    /// starts from silence at its own cutoff.
    void reset(FilterFamily family, int mode, int poleIndex);

    /// Filter two channels from @p in to @p out, which must not alias.
    /// @p poleIndex picks 2, 4 or 6 poles where the family has the choice (SVF).
    void process(FilterFamily family, int mode, int poleIndex, float cutoffHz, float resonance,
                 const float* const* in, float* const* out, int numSamples);

  private:
    struct Engine {
        std::unique_ptr<::dsp> instance;
        float* cutoff = nullptr;
        float* resonance = nullptr;
        float* mode = nullptr;
        float* smoothing = nullptr;
        float* poles = nullptr;
    };
    std::array<Engine, kFilterDspCount> engines_;
    double sampleRate_ = 44100.0;
};

}  // namespace magda::devices::faust
