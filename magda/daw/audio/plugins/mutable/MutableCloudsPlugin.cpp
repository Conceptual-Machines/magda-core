#include "plugins/mutable/MutableCloudsPlugin.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

// Upstream Mutable Instruments DSP (third_party/eurorack, magda::mutable),
// compiled with -DTEST. Walled behind the pimpl so this is the only TU in the
// base device pack that sees the eurorack headers.
#include "clouds/dsp/frame.h"
#include "clouds/dsp/granular_processor.h"

namespace magda::daw::audio {

namespace {

constexpr int kBlock = static_cast<int>(clouds::kMaxBlockSize);  // 32
const float kInternalRate = 32000.0f;                            // Clouds native rate

const char* const kModeNames[] = {"Granular", "Stretch", "Looping Delay", "Spectral"};
constexpr int kNumModes = 4;

inline float cubic(float x0, float x1, float x2, float x3, float t) {
    const float a = -0.5f * x0 + 1.5f * x1 - 1.5f * x2 + 0.5f * x3;
    const float b = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
    const float c = -0.5f * x0 + 0.5f * x2;
    return ((a * t + b) * t + c) * t + x1;
}

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
        if (ring.count < 4)
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
    {"position", "Position", 0.5f, Kind::Normalised},
    {"size", "Size", 0.5f, Kind::Normalised},
    {"pitch", "Pitch", 0.0f, Kind::Pitch},
    {"density", "Density", 0.4f, Kind::Normalised},
    {"texture", "Texture", 0.5f, Kind::Normalised},
    {"dryWet", "Dry/Wet", 0.5f, Kind::Normalised},
    {"spread", "Spread", 0.5f, Kind::Normalised},
    {"feedback", "Feedback", 0.0f, Kind::Normalised},
    {"reverb", "Reverb", 0.0f, Kind::Normalised},
    {"mode", "Mode", 0.0f, Kind::Mode},
    {"freeze", "Freeze", 0.0f, Kind::Freeze},
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
        reset();
    }

    void reset() {
        hostIn_.clear();
        wet32k_.clear();
        inResampler_.reset();
        outResampler_.reset();
        grainFill_ = 0;
    }

    clouds::Parameters* parameters() {
        return processor_.mutable_parameters();
    }
    void setMode(int mode) {
        processor_.set_playback_mode(static_cast<clouds::PlaybackMode>(mode));
    }

    // Push one host input frame, drain whatever full grain blocks it enables,
    // and pull host output frames back out. Output lags input by the resampler
    // + grain-block latency; the rings carry the cushion.
    void process(float* L, float* R, int n) {
        for (int i = 0; i < n; ++i)
            hostIn_.push(L[i], R != nullptr ? R[i] : L[i]);

        float sL, sR;
        while (inResampler_.pull(hostIn_, sL, sR)) {
            grainIn_[grainFill_].l = sL;
            grainIn_[grainFill_].r = sR;
            if (++grainFill_ == kBlock) {
                runBlock();
                grainFill_ = 0;
            }
        }

        for (int i = 0; i < n; ++i) {
            float oL, oR;
            if (outResampler_.pull(wet32k_, oL, oR)) {
                L[i] = oL;
                if (R != nullptr)
                    R[i] = oR;
            } else {
                L[i] = 0.0f;  // priming latency only
                if (R != nullptr)
                    R[i] = 0.0f;
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

void MutableCloudsPlugin::setParameterValue(int index, float value) {
    if (index < 0 || index >= kNumParams)
        return;
    values_[static_cast<size_t>(index)] = juce::jlimit(0.0f, 1.0f, value);
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
