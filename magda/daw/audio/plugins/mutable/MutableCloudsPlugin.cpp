#include "plugins/mutable/MutableCloudsPlugin.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>

// Upstream Mutable Instruments DSP (third_party/eurorack, magda::mutable),
// compiled with -DTEST. Walled behind the pimpl so this is the only TU in the
// base device pack that sees the eurorack headers.
#include "clouds/dsp/frame.h"
#include "clouds/dsp/granular_processor.h"

namespace magda::daw::audio {

namespace {

constexpr int kBlock = static_cast<int>(clouds::kMaxBlockSize);  // 32

// Silence placed in the output ring at reset. A grain block arrives 32 samples
// at a time while the output is drawn continuously, so the ring swings by a
// block; the window on top of that is what the interpolator reads. See
// Impl::reset.
constexpr int kWetPrimeFrames = kBlock + 4;
const float kInternalRate = 32000.0f;  // Clouds native rate

const char* const kModeNames[] = {"Granular", "Stretch", "Looping Delay", "Spectral"};
constexpr int kNumModes = 4;

inline float cubic(float x0, float x1, float x2, float x3, float t) {
    const float a = -0.5f * x0 + 1.5f * x1 - 1.5f * x2 + 0.5f * x3;
    const float b = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
    const float c = -0.5f * x0 + 0.5f * x2;
    return ((a * t + b) * t + c) * t + x1;
}

// One biquad section, transposed direct form II.
struct Biquad {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void clear() {
        z1 = z2 = 0.0f;
    }
    void setLowpass(double fc, double fs, double q) {
        const double w0 = 2.0 * juce::MathConstants<double>::pi * fc / fs;
        const double cw = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = static_cast<float>((1.0 - cw) * 0.5 / a0);
        b1 = static_cast<float>((1.0 - cw) / a0);
        b2 = b0;
        a1 = static_cast<float>(-2.0 * cw / a0);
        a2 = static_cast<float>((1.0 - alpha) / a0);
    }
    float process(float x) {
        const float y = (b0 * x) + z1;
        z1 = (b1 * x) - (a1 * y) + z2;
        z2 = (b2 * x) - (a2 * y);
        return y;
    }
};

// Pole Qs of an 8th-order Butterworth, 1/(2 cos(pi (2k+1)/16)).
constexpr double kButterworthQ[4] = {0.50979558, 0.60134489, 0.89997622, 2.56291545};
constexpr double kBandLimitHz = MutableCloudsPlugin::kBandLimitHz;

// Feedback at or above this never decays, so no finite tail describes it.
constexpr float kFeedbackSustains = 0.75f;

static_assert(kBlock == 32, "the measured latency assumes a 32-sample grain block");

// The 32 kHz DSP is band-limited to 16 kHz and the cubic interpolators do not
// enforce it: unfiltered, a 20 kHz tone folds to 12 kHz louder than it left,
// and a 10 kHz tone's image sits 17 dB down coming back up. One lowpass per
// crossing, both at host rate.
struct BandLimit {
    Biquad l[4], r[4];

    void prepare(double hostRate) {
        // At every rate, 32 kHz included: bypassing where nothing resamples
        // puts the declared latency 3 samples out at that rate. Below Nyquist
        // at every rate too. OfflineMixAnalysis renders at 22.05 kHz with
        // plugins in, and a 15 kHz corner there is an unstable biquad: it took
        // the output to 1e37 and then to NaN (#2365 review).
        const double corner = std::min(kBandLimitHz, 0.45 * hostRate);
        for (int i = 0; i < 4; ++i) {
            l[i].setLowpass(corner, hostRate, kButterworthQ[i]);
            r[i].setLowpass(corner, hostRate, kButterworthQ[i]);
        }
        clear();
    }
    void clear() {
        for (int i = 0; i < 4; ++i) {
            l[i].clear();
            r[i].clear();
        }
    }
    void process(float& L, float& R) {
        for (int i = 0; i < 4; ++i) {
            L = l[i].process(L);
            R = r[i].process(R);
        }
    }
};

inline short floatToShort(float x) {
    return static_cast<short>(juce::jlimit(-32768, 32767, juce::roundToInt(x * 32767.0f)));
}
inline float shortToFloat(short x) {
    return static_cast<float>(x) * (1.0f / 32768.0f);
}

// Stereo ring buffer (no allocation on the audio thread).
struct StereoRing {
    static constexpr int kCap = 4096;
    static constexpr int kMask = kCap - 1;
    float l[kCap];
    float r[kCap];
    int head = 0;
    int count = 0;

    void clear() {
        head = 0;
        count = 0;
    }
    void push(float L, float R) {
        const int i = (head + count) & kMask;
        l[i] = L;
        r[i] = R;
        if (count < kCap)
            ++count;
        else
            head = (head + 1) & kMask;  // overrun: drop oldest
    }
    float L(int i) const {
        return l[(head + i) & kMask];
    }
    float R(int i) const {
        return r[(head + i) & kMask];
    }
    void pop(int n) {
        head = (head + n) & kMask;
        count -= n;
    }
};

// Cubic pull resampler reading from a StereoRing at rate Fin, producing one
// output sample at rate Fout per pull (step = Fin/Fout). Phase stays in [1,2)
// so the 4-point window is always ring indices 0..3.
struct Resampler {
    double pos = 1.0;
    double step = 1.0;

    void reset() {
        pos = 1.0;
    }
    bool pull(StereoRing& ring, float& oL, float& oR) {
        // The 4-point window plus whatever this call pops: step can exceed 1,
        // and popping past count leaves it negative, which puts the next push
        // behind head (#2365 review).
        if (ring.count < 3 + static_cast<int>(std::ceil(step)))
            return false;
        const auto t = static_cast<float>(pos - 1.0);
        oL = cubic(ring.L(0), ring.L(1), ring.L(2), ring.L(3), t);
        oR = cubic(ring.R(0), ring.R(1), ring.R(2), ring.R(3), t);
        pos += step;
        while (pos >= 2.0) {
            ring.pop(1);
            pos -= 1.0;
        }
        return true;
    }
};

enum class Kind { Normalised, Pitch, Mode, Freeze };
struct Desc {
    const char* id;
    const char* name;
    float def;
    Kind kind;
};

const std::array<Desc, MutableCloudsPlugin::kNumParams> kDescs = {{
    {.id = "position", .name = "Position", .def = 0.5f, .kind = Kind::Normalised},
    {.id = "size", .name = "Size", .def = 0.5f, .kind = Kind::Normalised},
    {.id = "pitch", .name = "Pitch", .def = 0.0f, .kind = Kind::Pitch},
    {.id = "density", .name = "Density", .def = 0.4f, .kind = Kind::Normalised},
    {.id = "texture", .name = "Texture", .def = 0.5f, .kind = Kind::Normalised},
    {.id = "dryWet", .name = "Dry/Wet", .def = 0.5f, .kind = Kind::Normalised},
    {.id = "spread", .name = "Spread", .def = 0.5f, .kind = Kind::Normalised},
    {.id = "feedback", .name = "Feedback", .def = 0.0f, .kind = Kind::Normalised},
    {.id = "reverb", .name = "Reverb", .def = 0.0f, .kind = Kind::Normalised},
    {.id = "mode", .name = "Mode", .def = 0.0f, .kind = Kind::Mode},
    {.id = "freeze", .name = "Freeze", .def = 0.0f, .kind = Kind::Freeze},
}};

/// One slot's metadata, from the descriptor table. The ids, order and display
/// ranges are pinned to what the retired host-native plugin registered,
/// because projects store parameter values in the model in display units
/// against these ranges and address the slots by index.
ParameterInfo slotInfo(int index) {
    ParameterInfo info;
    if (index < 0 || index >= MutableCloudsPlugin::kNumParams)
        return info;

    const auto& d = kDescs[static_cast<size_t>(index)];
    info.paramIndex = index;
    info.stableId = d.id;
    info.name = d.name;
    info.defaultValue = d.def;

    switch (d.kind) {
        case Kind::Normalised:
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.displayFormat = DisplayFormat::Percent;
            break;
        case Kind::Pitch:
            info.unit = "st";
            info.minValue = -24.0f;
            info.maxValue = 24.0f;
            break;
        case Kind::Mode:
            info.scale = ParameterScale::Discrete;
            info.minValue = 0.0f;
            info.maxValue = static_cast<float>(kNumModes - 1);
            info.choices = {kModeNames, kModeNames + kNumModes};
            break;
        case Kind::Freeze:
            // Boolean, and not modulatable, which is also what the old
            // processor inferred from the two-state host parameter.
            info.scale = ParameterScale::Boolean;
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.choices = {"Off", "On"};
            info.modulatable = false;
            break;
    }

    return info;
}

}  // namespace

//==============================================================================
struct MutableCloudsPlugin::Impl {
    Impl() {
        // See MutableElementsPlugin: zero the heap-allocated processor + its
        // external audio buffers before Init so no stale state/samples surface
        // on a fresh instance (the upstream DSP assumes zero-initialised BSS).
        std::memset(static_cast<void*>(&processor_), 0, sizeof(processor_));
        std::memset(largeBuffer_, 0, sizeof(largeBuffer_));
        std::memset(smallBuffer_, 0, sizeof(smallBuffer_));
        processor_.Init(largeBuffer_, sizeof(largeBuffer_), smallBuffer_, sizeof(smallBuffer_));
        processor_.set_num_channels(2);
        processor_.set_low_fidelity(false);
    }

    void prepare(double hostRate) {
        // step = Fin/Fout, the rate the ring is read at over the rate it is
        // pulled at (see Resampler). The input resampler reads host-rate data,
        // the output one reads 32 kHz.
        inResampler_.step = hostRate / kInternalRate;   // host -> 32k (downsample)
        outResampler_.step = kInternalRate / hostRate;  // 32k -> host (upsample)
        inLimit_.prepare(hostRate);
        outLimit_.prepare(hostRate);
        reset();
    }

    void reset() {
        hostIn_.clear();
        wet32k_.clear();
        inResampler_.reset();
        outResampler_.reset();
        inLimit_.clear();
        outLimit_.clear();
        grainFill_ = 0;

        // Start the output ring with a grain block of silence in it. Without
        // that cushion, whether a pull finds data depends on where the host's
        // callback boundaries fall, and a starved pull emits a zero WITHOUT
        // advancing the resampler, so each one adds a sample of delay for
        // good. Two hosts with different buffer sizes then produce different
        // audio permanently, not just a different transient: at 48 kHz a
        // 1-sample callback diverged from a 128-sample one across the whole
        // render (#2365 review). Placed here, the delay is designed rather
        // than discovered, and after startup a starved pull cannot happen.
        for (int i = 0; i < kWetPrimeFrames; ++i)
            wet32k_.push(0.0f, 0.0f);
    }

    clouds::Parameters* parameters() {
        return processor_.mutable_parameters();
    }
    void setMode(int mode) {
        processor_.set_playback_mode(static_cast<clouds::PlaybackMode>(mode));
    }

    // Push host input, drain whatever full grain blocks it enables, and pull
    // host output back out. Output lags input by the resampler + grain-block
    // latency; the rings carry the cushion.
    //
    // In chunks, because pushing a whole call before draining any of it makes
    // StereoRing::kCap a limit on the host's buffer size: at 8192 the ring
    // overruns and drops input continuously (#2365 review). A chunk bounds
    // occupancy at roughly kChunk * max(1, 32000/hostRate) whatever the host
    // hands over, so prepare() does not have to be told the block size.
    void process(float* L, float* R, int n) {
        constexpr int kChunk = 256;

        for (int base = 0; base < n; base += kChunk) {
            const int m = std::min(kChunk, n - base);

            for (int i = 0; i < m; ++i) {
                float inL = L[base + i];
                float inR = R != nullptr ? R[base + i] : inL;
                inLimit_.process(inL, inR);
                hostIn_.push(inL, inR);
            }

            float sL, sR;
            while (inResampler_.pull(hostIn_, sL, sR)) {
                grainIn_[grainFill_].l = sL;
                grainIn_[grainFill_].r = sR;
                if (++grainFill_ == kBlock) {
                    runBlock();
                    grainFill_ = 0;
                }
            }

            for (int i = 0; i < m; ++i) {
                // Zero unless the ring has a window yet: priming latency only.
                // The filter still sees those samples, so its state stays
                // continuous across the transition into real output.
                float oL = 0.0f, oR = 0.0f;
                outResampler_.pull(wet32k_, oL, oR);
                outLimit_.process(oL, oR);
                L[base + i] = oL;
                if (R != nullptr)
                    R[base + i] = oR;
            }
        }
    }

  private:
    void runBlock() {
        clouds::ShortFrame in[kBlock];
        clouds::ShortFrame out[kBlock];
        for (int j = 0; j < kBlock; ++j) {
            in[j].l = floatToShort(grainIn_[j].l);
            in[j].r = floatToShort(grainIn_[j].r);
        }
        processor_.Prepare();
        processor_.Process(in, out, static_cast<size_t>(kBlock));
        for (int j = 0; j < kBlock; ++j)
            wet32k_.push(shortToFloat(out[j].l), shortToFloat(out[j].r));
    }

    clouds::GranularProcessor processor_;
    uint8_t largeBuffer_[118784];
    uint8_t smallBuffer_[65536 - 128];

    StereoRing hostIn_;  // host-rate input awaiting downsample
    StereoRing wet32k_;  // 32 kHz processed output awaiting upsample
    Resampler inResampler_;
    Resampler outResampler_;
    BandLimit inLimit_;   // before the decimation, against aliasing
    BandLimit outLimit_;  // after the interpolation, against imaging

    clouds::FloatFrame grainIn_[kBlock];
    int grainFill_ = 0;
};

//==============================================================================
const char* MutableCloudsPlugin::xmlTypeName = "magda_clouds";

MutableCloudsPlugin::MutableCloudsPlugin() : impl_(std::make_unique<Impl>()) {
    for (int index = 0; index < kNumParams; ++index) {
        const auto info = slotInfo(index);
        domains_[static_cast<size_t>(index)] = ParameterUtils::domainOf(info);
        values_[static_cast<size_t>(index)] =
            ParameterUtils::realToNormalized(info.defaultValue, info);
    }
}

MutableCloudsPlugin::~MutableCloudsPlugin() = default;

ParameterInfo MutableCloudsPlugin::parameterInfo(int index) const {
    return slotInfo(index);
}

float MutableCloudsPlugin::parameterValue(int index) const {
    if (index < 0 || index >= kNumParams)
        return 0.0f;
    return values_[static_cast<size_t>(index)];
}

double MutableCloudsPlugin::tailSecondsFor(float reverb, float feedback, bool freeze,
                                           float position, int mode) {
    // Upstream's own reverb_amount (granular_processor.cc): feedback only joins
    // it once frozen, and reverb alone tops out at 0.95.
    const float amount =
        juce::jlimit(0.0f, 1.0f, (reverb * 0.95f) + (freeze ? feedback * (2.0f - feedback) : 0.0f));

    // Measured decay to -60 dBFS against reverb, +20% headroom, in granular at
    // grain size 1: that is the longest of the four modes by about 60%, and the
    // curve has to cover the worst. A table rather than a formula, because the
    // decay is not a single exponential and a fit is either wrong in the middle
    // or absurd at the top.
    constexpr float kAmount[] = {0.0f,   0.095f,  0.2375f, 0.38f, 0.5225f,
                                 0.665f, 0.8075f, 0.95f,   1.0f};
    constexpr double kSeconds[] = {0.7, 2.8, 3.8, 5.4, 7.3, 10.1, 16.4, 39.0, 40.0};
    constexpr int kPoints = static_cast<int>(std::size(kAmount));

    double seconds = kSeconds[kPoints - 1];
    for (int i = 1; i < kPoints; ++i) {
        if (amount <= kAmount[i]) {
            const double t = (amount - kAmount[i - 1]) / (kAmount[i] - kAmount[i - 1]);
            seconds = kSeconds[i - 1] + t * (kSeconds[i] - kSeconds[i - 1]);
            break;
        }
    }

    // Every mode replays the buffer from `position`, so a delayed copy outlives
    // the input on top of the decay above: 0.51 s at position 0, which the
    // curve's first point already carries, rising to 1.10 s at position 1. Rate
    // independent, longest at grain size 1.
    seconds += 0.8 * position;

    // Spectral resynthesis is not deterministic: identical renders at reverb
    // 0.75 have come back 0 s, 0 s, 20.7 s and 37.7 s, where the curve above
    // says 12.2. Nothing measurable to fit, so take enough headroom that any
    // engaged reverb reaches the ceiling, which is the only honest bound on a
    // mode that can ring for most of a minute.
    if (mode == 3)
        seconds *= 4.0;

    // Unfrozen feedback is a delay line: the buffer repeats every `position`
    // seconds at that gain, so the tail is however many repeats reach -60 dB.
    // Analytic rather than measured, which over-declares by up to 2x at low
    // feedback and covers it at high (#2365 review).
    if (feedback > 0.01f && feedback < kFeedbackSustains) {
        const double delaySeconds = 0.1 + 1.02 * position;
        seconds = std::max(seconds, delaySeconds * 3.0 / -std::log10(feedback));
    }

    // Past that it stops decaying at all: 0.7 with a full delay runs 17 s, 0.79
    // was still above -60 dBFS after 45. Declare the ceiling rather than a
    // decay the device is not performing.
    if (feedback >= kFeedbackSustains)
        seconds = kMaxTailSeconds;

    return juce::jlimit(0.25, kMaxTailSeconds, seconds);
}

void MutableCloudsPlugin::setParameterValue(int index, float value) {
    if (index < 0 || index >= kNumParams)
        return;
    values_[static_cast<size_t>(index)] = juce::jlimit(0.0f, 1.0f, value);

    if (index == kReverb || index == kFeedback || index == kFreeze || index == kPosition ||
        index == kMode)
        tailSeconds_.store(tailSecondsFor(displayValue(kReverb), displayValue(kFeedback),
                                          displayValue(kFreeze) > 0.5f, displayValue(kPosition),
                                          juce::roundToInt(displayValue(kMode))),
                           std::memory_order_relaxed);
}

float MutableCloudsPlugin::displayValue(int index) const {
    return ParameterUtils::normalizedToReal(values_[static_cast<size_t>(index)],
                                            domains_[static_cast<size_t>(index)]);
}

void MutableCloudsPlugin::prepare(const DevicePrepareContext& context) {
    sampleRate_ = context.sampleRate;
    impl_->prepare(sampleRate_);
    envBucketLen_ =
        juce::jmax(1, static_cast<int>(kBufferSeconds * sampleRate_ / kEnvelopeBuckets));
}

void MutableCloudsPlugin::reset() {
    impl_->reset();
}

void MutableCloudsPlugin::process(DeviceProcessContext& context) {
    if (context.audio == nullptr || context.numSamples <= 0)
        return;

    float v[kNumParams];
    for (int i = 0; i < kNumParams; ++i)
        v[i] = displayValue(i);

    impl_->setMode(juce::jlimit(0, kNumModes - 1, juce::roundToInt(v[kMode])));

    auto* p = impl_->parameters();
    p->position = v[kPosition];
    p->size = v[kSize];
    p->pitch = v[kPitch];
    p->density = v[kDensity];
    p->texture = v[kTexture];
    p->dry_wet = v[kDryWet];
    p->stereo_spread = v[kSpread];
    p->feedback = v[kFeedback];
    p->reverb = v[kReverb];
    p->freeze = v[kFreeze] > 0.5f;
    p->trigger = false;
    p->gate = false;

    auto& buffer = *context.audio;
    const int start = context.startSample;
    auto* destL = buffer.getWritePointer(0, start);
    float* destR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1, start) : nullptr;

    // Tap the dry input into a decimated peak envelope; the faceplate uses its
    // recent level to drive the ambient grain-cloud liveliness.
    for (int i = 0; i < context.numSamples; ++i) {
        const float s = destR != nullptr ? 0.5f * (destL[i] + destR[i]) : destL[i];
        envPeak_ = juce::jmax(envPeak_, std::abs(s));
        if (++envCount_ >= envBucketLen_) {
            inputEnvelope_.write(&envPeak_, 1);
            envPeak_ = 0.0f;
            envCount_ = 0;
        }
    }

    impl_->process(destL, destR, context.numSamples);
}

}  // namespace magda::daw::audio
