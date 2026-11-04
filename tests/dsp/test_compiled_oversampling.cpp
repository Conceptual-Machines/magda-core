// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <complex>
#include <numbers>
#include <numeric>
#include <random>
#include <vector>

#include "devices/faust/CompiledEffect.hpp"
#include "devices/faust/CompiledOversampler.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"

// The shared oversampling wrapper for compiled effects (#2276).

namespace {

namespace faust = magda::devices::faust;
namespace sdk = magda::sdk;
using Signal = std::vector<std::vector<float>>;

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 256;

/** @brief A hand-written Faust dsp: a hard clip at its Ceiling, which remembers its rate. */
class ClipProbeDsp final : public ::dsp {
  public:
    explicit ClipProbeDsp(int channels) : channels_(channels) {}

    int getNumInputs() override {
        return channels_;
    }
    int getNumOutputs() override {
        return channels_;
    }
    void buildUserInterface(UI* ui) override {
        ui->openVerticalBox("Probe");
        ui->declare(&ceiling_, "idx", "0");
        ui->addHorizontalSlider("Ceiling", &ceiling_, 16.0f, 0.0f, 16.0f, 0.01f);
        ui->closeBox();
    }
    int getSampleRate() override {
        return sampleRate_;
    }
    void init(int sampleRate) override {
        instanceInit(sampleRate);
    }
    void instanceInit(int sampleRate) override {
        instanceConstants(sampleRate);
        instanceResetUserInterface();
        instanceClear();
    }
    void instanceConstants(int sampleRate) override {
        sampleRate_ = sampleRate;
    }
    void instanceResetUserInterface() override {
        ceiling_ = 16.0f;
    }
    void instanceClear() override {}
    ::dsp* clone() override {
        return new ClipProbeDsp(channels_);
    }
    void metadata(Meta*) override {}
    void compute(int count, FAUSTFLOAT** inputs, FAUSTFLOAT** outputs) override {
        for (int channel = 0; channel < channels_; ++channel)
            for (int i = 0; i < count; ++i)
                outputs[channel][i] = std::clamp(inputs[channel][i], -ceiling_, ceiling_);
    }

  private:
    int channels_;
    int sampleRate_ = 0;
    FAUSTFLOAT ceiling_ = 16.0f;
};

/** @brief The smallest compiled effect that can offer oversampling: one Ceiling slot. */
class ProbeEffect final : public faust::CompiledEffect {
  public:
    ProbeEffect(int maxFactor, int channels) : maxFactor_(maxFactor), channels_(channels) {
        initEffect();
    }

    int maxOversamplingFactor() const override {
        return maxFactor_;
    }

    int dspSampleRate() const {
        return probe_ != nullptr ? probe_->getSampleRate() : 0;
    }
    void setCeiling(float ceiling) {
        setParameterValue(0, displayToNormalized(0, ceiling));
    }

  protected:
    std::string devicePluginId() const override {
        return "test_oversampling_probe";
    }
    std::string deviceName() const override {
        return "Probe";
    }
    ::dsp* createEngineDsp(int) const override {
        probe_ = new ClipProbeDsp(channels_);
        return probe_;
    }
    std::vector<faust::SlotInfo> slotInfos() const override {
        return {{.name = "Ceiling", .minValue = 0.0f, .maxValue = 16.0f, .defaultValue = 16.0f}};
    }
    const char* slotIdPrefix() const override {
        return "test_probe_";
    }

  private:
    int maxFactor_;
    int channels_;
    mutable ClipProbeDsp* probe_ = nullptr;
};

/** @brief A host that keeps what the device reports. */
class RecordingHost final : public sdk::DeviceHost {
  public:
    void stateChanged(sdk::StateNode state) override {
        reported.push_back(std::move(state));
    }
    void rebuildRequired() override {
        ++rebuilds;
    }

    std::vector<sdk::StateNode> reported;
    int rebuilds = 0;
};

void prepare(faust::CompiledEffect& device, double sampleRate = kSampleRate) {
    device.prepare(sdk::PrepareContext{.sampleRate = sampleRate, .maximumBlockSize = kBlockSize});
}

/** @brief @p input through @p device in host blocks of @p blockSize; the device keeps its state. */
Signal render(faust::CompiledEffect& device, const Signal& input, int blockSize = kBlockSize) {
    const auto channels = input.size();
    const int length = static_cast<int>(input.front().size());
    Signal output = input;
    Signal block(channels, std::vector<float>(static_cast<size_t>(blockSize)));
    std::vector<float*> pointers;
    for (auto& channel : block)
        pointers.push_back(channel.data());

    for (int start = 0; start < length; start += blockSize) {
        const int count = std::min(blockSize, length - start);
        for (size_t channel = 0; channel < channels; ++channel)
            std::copy_n(input[channel].begin() + start, count, block[channel].begin());

        sdk::ProcessContext context;
        context.audio = magda::BufferView(pointers.data(), static_cast<int>(channels), count);
        device.process(context);

        for (size_t channel = 0; channel < channels; ++channel)
            std::copy_n(block[channel].begin(), count, output[channel].begin() + start);
    }
    return output;
}

Signal impulse(int channels, int length) {
    Signal signal(static_cast<size_t>(channels), std::vector<float>(static_cast<size_t>(length)));
    for (auto& channel : signal)
        channel[0] = 1.0f;
    return signal;
}

Signal constant(int channels, int length, float value) {
    return Signal(static_cast<size_t>(channels),
                  std::vector<float>(static_cast<size_t>(length), value));
}

int peakIndex(const std::vector<float>& samples) {
    const auto peak = std::max_element(samples.begin(), samples.end(),
                                       [](float a, float b) { return std::abs(a) < std::abs(b); });
    return static_cast<int>(peak - samples.begin());
}

/// Magnitude of DFT bin @p bin over the last @p length samples, relative to a full-scale sine.
double binLevel(const std::vector<float>& samples, int bin, int length) {
    std::complex<double> sum;
    const auto offset = samples.size() - static_cast<size_t>(length);
    for (int i = 0; i < length; ++i)
        sum += static_cast<double>(samples[offset + static_cast<size_t>(i)]) *
               std::polar(1.0, -2.0 * std::numbers::pi * bin * i / length);
    return 2.0 * std::abs(sum) / length;
}

double dB(double level) {
    return 20.0 * std::log10(std::max(level, 1.0e-12));
}

}  // namespace

TEST_CASE("Halfband round trip delays by exactly what it reports", "[oversampling][dsp]") {
    CHECK(faust::CompiledOversampler::latencySamples(1) == 0);

    for (const int factor : {2, 4}) {
        INFO("factor " << factor);
        faust::CompiledOversampler oversampler;
        oversampler.prepare(1, 1, kBlockSize);

        const int latency = faust::CompiledOversampler::latencySamples(factor);
        const double frequency = 1000.0;
        std::vector<float> in(kBlockSize);
        std::vector<float> out(kBlockSize);
        double worst = 0.0;
        for (int block = 0; block < 32; ++block) {
            for (int i = 0; i < kBlockSize; ++i)
                in[static_cast<size_t>(i)] = static_cast<float>(std::sin(
                    2.0 * std::numbers::pi * frequency * (block * kBlockSize + i) / kSampleRate));

            const float* inputs[] = {in.data()};
            float* outputs[] = {out.data()};
            oversampler.upsample(inputs, 1, kBlockSize, factor);
            std::copy_n(oversampler.inputs()[0], kBlockSize * factor, oversampler.outputs()[0]);
            oversampler.downsample(outputs, 1, kBlockSize, factor);

            if (block < 2)
                continue;
            for (int i = 0; i < kBlockSize; ++i) {
                const double expected = std::sin(2.0 * std::numbers::pi * frequency *
                                                 (block * kBlockSize + i - latency) / kSampleRate);
                worst = std::max(worst, std::abs(out[static_cast<size_t>(i)] - expected));
            }
        }
        CHECK(worst < 1.0e-4);
    }
}

TEST_CASE("Halfband decimation keeps what lies above the host Nyquist out of the band",
          "[oversampling][dsp]") {
    // Each tone sits in a decimator's stopband; anything left of it is an alias.
    for (const auto& [factor, frequency] : std::vector<std::pair<int, double>>{
             {2, 30000.0}, {2, 40000.0}, {4, 30000.0}, {4, 60000.0}, {4, 80000.0}}) {
        INFO("factor " << factor << ", " << frequency << " Hz");
        faust::CompiledOversampler oversampler;
        oversampler.prepare(1, 1, kBlockSize);

        std::vector<float> silence(kBlockSize, 0.0f);
        std::vector<float> out(kBlockSize);
        double peak = 0.0;
        const double rate = kSampleRate * factor;
        for (int block = 0; block < 32; ++block) {
            const float* inputs[] = {silence.data()};
            float* outputs[] = {out.data()};
            oversampler.upsample(inputs, 1, kBlockSize, factor);
            for (int i = 0; i < kBlockSize * factor; ++i)
                oversampler.outputs()[0][i] = static_cast<float>(std::sin(
                    2.0 * std::numbers::pi * frequency * (block * kBlockSize * factor + i) / rate));
            oversampler.downsample(outputs, 1, kBlockSize, factor);
            if (block >= 2)
                for (const float sample : out)
                    peak = std::max(peak, static_cast<double>(std::abs(sample)));
        }
        CHECK(dB(peak) < -85.0);
    }
}

TEST_CASE("Halfband output does not depend on how the host splits the block",
          "[oversampling][dsp]") {
    std::mt19937 random(2276);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    std::vector<float> in(1024);
    for (auto& sample : in)
        sample = noise(random);

    for (const int factor : {2, 4}) {
        INFO("factor " << factor);
        const auto run = [&](int chunk) {
            faust::CompiledOversampler oversampler;
            oversampler.prepare(1, 1, 300);
            std::vector<float> out(in.size());
            for (size_t start = 0; start < in.size(); start += static_cast<size_t>(chunk)) {
                const int count = std::min(chunk, static_cast<int>(in.size() - start));
                const float* inputs[] = {in.data() + start};
                float* outputs[] = {out.data() + start};
                oversampler.upsample(inputs, 1, count, factor);
                std::copy_n(oversampler.inputs()[0], count * factor, oversampler.outputs()[0]);
                oversampler.downsample(outputs, 1, count, factor);
            }
            return out;
        };

        const auto whole = run(300);
        for (const int chunk : {1, 7, 64, 299}) {
            INFO("chunk " << chunk);
            const auto split = run(chunk);
            for (size_t i = 0; i < in.size(); ++i)
                REQUIRE(split[i] == Catch::Approx(whole[i]).margin(1.0e-6));
        }
    }
}

TEST_CASE("A compiled effect that offers no oversampling is untouched by the wrapper",
          "[oversampling][device]") {
    RecordingHost host;
    ProbeEffect device(1, 2);
    device.setHost(&host);
    prepare(device);

    CHECK(device.latencySamples() == 0);
    device.setOversamplingFactor(4);
    CHECK(device.oversamplingFactor() == 1);
    CHECK(host.reported.empty());
    CHECK(device.dspSampleRate() == static_cast<int>(kSampleRate));

    std::mt19937 random(7);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    Signal input(2, std::vector<float>(2048));
    for (auto& channel : input)
        for (auto& sample : channel)
            sample = noise(random);
    CHECK(render(device, input) == input);
    device.setHost(nullptr);
}

TEST_CASE("An oversampling device reports one latency at every factor and delays by it",
          "[oversampling][device]") {
    const int reported = faust::CompiledOversampler::latencySamples(4);
    for (const int factor : {1, 2, 4}) {
        INFO("factor " << factor);
        ProbeEffect device(4, 2);
        device.setOversamplingFactor(factor);
        prepare(device);

        CHECK(device.runningOversamplingFactor() == factor);
        CHECK(device.dspSampleRate() == static_cast<int>(kSampleRate) * factor);
        CHECK(device.latencySamples() == reported);

        // Band-limited at 0.45 fs, so the impulse comes back a little spread, at unity DC gain.
        const auto output = render(device, impulse(2, 1024));
        for (const auto& channel : output) {
            CHECK(peakIndex(channel) == reported);
            CHECK(channel[static_cast<size_t>(reported)] > 0.95f);
            CHECK(std::accumulate(channel.begin(), channel.end(), 0.0) ==
                  Catch::Approx(1.0).margin(1.0e-4));
        }
    }
}

TEST_CASE("A mono dsp's untouched channel is delayed with the one it writes",
          "[oversampling][device]") {
    ProbeEffect device(2, 1);
    device.setOversamplingFactor(2);
    prepare(device);

    const int latency = faust::CompiledOversampler::latencySamples(2);
    const auto output = render(device, impulse(2, 512));
    CHECK(peakIndex(output[0]) == latency);
    CHECK(peakIndex(output[1]) == latency);
    CHECK(output[1][static_cast<size_t>(latency)] == 1.0f);
}

TEST_CASE("The factor stops where the dsp rate would pass Faust's 192 kHz",
          "[oversampling][device]") {
    ProbeEffect device(4, 2);
    device.setOversamplingFactor(4);

    prepare(device, 96000.0);
    CHECK(device.runningOversamplingFactor() == 2);
    CHECK(device.dspSampleRate() == 192000);
    CHECK(device.latencySamples() == faust::CompiledOversampler::latencySamples(2));

    prepare(device, 192000.0);
    CHECK(device.runningOversamplingFactor() == 1);
    CHECK(device.latencySamples() == 0);
    CHECK(device.oversamplingFactor() == 4);
}

TEST_CASE("A factor change fades out, re-inits the dsp and fades back in",
          "[oversampling][device]") {
    ProbeEffect device(4, 2);
    prepare(device);
    REQUIRE(device.runningOversamplingFactor() == 1);

    const auto settled = render(device, constant(2, 4800, 0.5f));
    CHECK(settled[0].back() == 0.5f);

    device.setOversamplingFactor(2);
    const auto change = render(device, constant(2, 9600, 0.5f));
    CHECK(device.runningOversamplingFactor() == 2);
    CHECK(device.dspSampleRate() == 96000);
    CHECK(device.latencySamples() == faust::CompiledOversampler::latencySamples(4));

    const auto& left = change[0];
    CHECK(std::count(left.begin(), left.end(), 0.0f) > 0);
    CHECK(*std::min_element(left.begin(), left.end()) > -0.01f);
    CHECK(*std::max_element(left.begin(), left.end()) < 0.55f);
    CHECK(left.back() == Catch::Approx(0.5f).margin(1.0e-5));

    // The dip is the two fades and the new path filling, not a stretch of silence.
    const auto quiet = std::count_if(left.begin(), left.end(), [](float s) { return s < 0.25f; });
    CHECK(quiet < static_cast<long>(0.02 * kSampleRate));
}

TEST_CASE("Oversampling keeps a hard clip's harmonics from folding into the band",
          "[oversampling][device]") {
    // A bin-centred tone, so its 5th harmonic folds onto bin 8192 - 5 * 1195 at 1x and every
    // component is periodic in the window.
    constexpr int kWindow = 8192;
    constexpr int kToneBin = 1195;
    constexpr int kAliasBin = kWindow - 5 * kToneBin;
    Signal tone(2, std::vector<float>(4 * kWindow));
    for (auto& channel : tone)
        for (size_t i = 0; i < channel.size(); ++i)
            channel[i] = 0.9f * static_cast<float>(std::sin(2.0 * std::numbers::pi * kToneBin *
                                                            static_cast<double>(i) / kWindow));

    const auto aliasLevel = [&](int factor) {
        ProbeEffect device(4, 2);
        device.setOversamplingFactor(factor);
        device.setCeiling(0.25f);
        prepare(device);
        return dB(binLevel(render(device, tone)[0], kAliasBin, kWindow));
    };

    const double plain = aliasLevel(1);
    CHECK(plain > -40.0);
    CHECK(aliasLevel(2) < plain - 60.0);
    CHECK(aliasLevel(4) < plain - 60.0);
}

TEST_CASE("The oversampling factor is the host's state, and absent is Off",
          "[oversampling][device]") {
    RecordingHost host;
    ProbeEffect device(4, 2);
    device.setHost(&host);

    // A faceplate's choice goes to the host's document, which is what a restore reads back.
    device.setOversamplingFactor(2);
    REQUIRE(host.reported.size() == 1);
    CHECK(host.reported.front().getInt("oversampling") == 2);
    CHECK(host.rebuilds == 0);

    ProbeEffect restored(4, 2);
    CHECK(restored.restoreState(host.reported.front()).ok);
    CHECK(restored.oversamplingFactor() == 2);

    CHECK(restored.restoreState(sdk::StateNode{}).ok);
    CHECK(restored.oversamplingFactor() == 1);

    sdk::StateNode three;
    three.setInt("oversampling", 3);
    restored.restoreState(three);
    CHECK(restored.oversamplingFactor() == 2);

    ProbeEffect twiceAtMost(2, 2);
    twiceAtMost.setOversamplingFactor(4);
    CHECK(twiceAtMost.oversamplingFactor() == 2);
    device.setHost(nullptr);
}
