#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>

#include "audio/plugins/LevelsPlugin.hpp"
#include "custom_ui/DeviceTelemetrySources.hpp"

/**
 * The faceplate's end of #2585: a telemetry source over whichever device the
 * engine is rendering, rather than over a plugin pointer only the fork has.
 *
 * What these pin is the lifetime rule the issue asks for -- a UI bound to a
 * device that is rebuilt or torn down is rebound or reads nothing, never left
 * on a freed instance -- which is a property of resolving the device per read
 * rather than holding it.
 */

namespace audio = magda::daw::audio;
namespace ui = magda::daw::ui;

namespace {

/// A device whose ring holds one value, so a read says which device answered.
class ScopeDevice final : public audio::MagdaDevice, public audio::OscilloscopeTelemetry {
  public:
    explicit ScopeDevice(float sample) : sample_(sample) {}

    audio::DeviceProperties properties() const override {
        return {.pluginId = "scope", .name = "Scope", .shortName = "Scope"};
    }

    void process(audio::DeviceProcessContext&) override {}

    std::string_view telemetryKey() const override {
        return kKey;
    }

    audio::DeviceTelemetry* telemetry(std::string_view key) override {
        return key == kKey ? this : nullptr;
    }

    const audio::DeviceTelemetry* telemetry(std::string_view key) const override {
        return key == kKey ? this : nullptr;
    }

    std::size_t writePosition() const override {
        return written_;
    }

    std::size_t readLatest(float* dest, int numSamples) const override {
        for (auto i = 0; i < numSamples; ++i)
            dest[i] = sample_;
        return written_;
    }

    double sampleRate() const override {
        return 48000.0;
    }

    int traceColourIndex() const override {
        return colour_;
    }

    void setTraceColourIndex(int index) override {
        colour_ = index;
    }

    float timebaseMs() const override {
        return timebase_;
    }

    void setTimebaseMs(float ms) override {
        timebase_ = ms;
    }

    std::size_t written_ = 1024;

  private:
    float sample_ = 0.0f;
    int colour_ = 0;
    float timebase_ = 10.0f;
};

/// A device of MAGDA's own with no telemetry at all, which is most of them.
class SilentDevice final : public audio::MagdaDevice {
  public:
    audio::DeviceProperties properties() const override {
        return {.pluginId = "silent", .name = "Silent", .shortName = "Silent"};
    }

    void process(audio::DeviceProcessContext&) override {}
};

float firstSampleOf(const ui::OscilloscopeTelemetrySource& source) {
    std::array<float, 4> trace{};
    source.readLatest(trace.data(), static_cast<int>(trace.size()));
    return trace[0];
}

}  // namespace

TEST_CASE("A device telemetry source reads through the device the engine renders",
          "[device-telemetry][2585]") {
    auto device = std::make_shared<ScopeDevice>(0.5f);
    ui::DeviceOscilloscopeTelemetry source([device] { return device; });

    CHECK(source.writePosition() == 1024);
    CHECK(source.sampleRate() == 48000.0);
    CHECK(firstSampleOf(source) == 0.5f);

    source.setTimebaseMs(250.0f);
    CHECK(source.timebaseMs() == 250.0f);
    CHECK(device->timebaseMs() == 250.0f);
}

TEST_CASE("A rebuilt device is read without rebinding the source", "[device-telemetry][2585]") {
    auto device = std::make_shared<ScopeDevice>(0.5f);
    ui::DeviceOscilloscopeTelemetry source([&device] { return device; });

    REQUIRE(firstSampleOf(source) == 0.5f);

    // What a plan rebuild is (#2575): the same slot, a new instance. The source
    // resolves per read, so it follows it, and nothing holds the old one open.
    std::weak_ptr<ScopeDevice> retired = device;
    device = std::make_shared<ScopeDevice>(-0.25f);

    CHECK(retired.expired());
    CHECK(firstSampleOf(source) == -0.25f);
}

TEST_CASE("A source whose device has gone reads nothing", "[device-telemetry][2585]") {
    std::shared_ptr<ScopeDevice> device;
    ui::DeviceOscilloscopeTelemetry source([&device] { return device; });

    CHECK(source.writePosition() == 0);
    CHECK(firstSampleOf(source) == 0.0f);
    CHECK(source.sampleRate() == 44100.0);
    CHECK(source.timebaseMs() == 10.0f);

    // A write with nothing behind it is dropped rather than crashing: the slot
    // can be drawn while its device is still being built.
    source.setTimebaseMs(250.0f);
    CHECK(source.timebaseMs() == 10.0f);
}

TEST_CASE("A device with no such surface answers nothing", "[device-telemetry][2585]") {
    auto device = std::make_shared<SilentDevice>();
    ui::DeviceOscilloscopeTelemetry source(
        [device]() -> std::shared_ptr<audio::MagdaDevice> { return device; });

    CHECK(source.writePosition() == 0);
    CHECK(firstSampleOf(source) == 0.0f);
}

TEST_CASE("A spectrum source reads the same way", "[device-telemetry][2585]") {
    class SpectrumDevice final : public audio::MagdaDevice, public audio::SpectrumTelemetry {
      public:
        audio::DeviceProperties properties() const override {
            return {.pluginId = "spectrum", .name = "Spectrum", .shortName = "Spectrum"};
        }

        void process(audio::DeviceProcessContext&) override {}

        std::string_view telemetryKey() const override {
            return kKey;
        }

        audio::DeviceTelemetry* telemetry(std::string_view key) override {
            return key == kKey ? this : nullptr;
        }

        const audio::DeviceTelemetry* telemetry(std::string_view key) const override {
            return key == kKey ? this : nullptr;
        }

        std::size_t writePosition() const override {
            return 64;
        }

        std::size_t readLatest(float*, int) const override {
            return 64;
        }

        double sampleRate() const override {
            return 48000.0;
        }

        int traceColourIndex() const override {
            return 0;
        }

        void setTraceColourIndex(int) override {}

        int fftOrder() const override {
            return order_;
        }

        void setFftOrder(int order) override {
            order_ = order;
        }

        float slopeDbPerOct() const override {
            return 3.0f;
        }

        void setSlopeDbPerOct(float) override {}

        float smoothing() const override {
            return 0.25f;
        }

        void setSmoothing(float) override {}

      private:
        int order_ = 11;
    };

    auto device = std::make_shared<SpectrumDevice>();
    ui::DeviceSpectrumTelemetry source([device] { return device; });

    CHECK(source.writePosition() == 64);
    CHECK(source.slopeDbPerOct() == 3.0f);
    CHECK(source.smoothing() == 0.25f);

    source.setFftOrder(12);
    CHECK(source.fftOrder() == 12);
    CHECK(device->fftOrder() == 12);
}

namespace {

/// One block of @p level on both channels through @p device, as a host renders it.
void measureBlock(audio::LevelsPlugin& device, float level) {
    juce::AudioBuffer<float> buffer(2, 512);
    for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
        juce::FloatVectorOperations::fill(buffer.getWritePointer(channel), level, 512);

    audio::DeviceProcessContext context;
    context.audio = &buffer;
    context.numSamples = 512;
    device.process(context);
}

}  // namespace

TEST_CASE("A Levels faceplate measures through the device the engine renders",
          "[device-telemetry][2658]") {
    std::shared_ptr<audio::LevelsPlugin> device;
    ui::DeviceLevelsTelemetry source(
        [&device]() -> std::shared_ptr<audio::MagdaDevice> { return device; });

    // The faceplate shows before anything renders behind it.
    source.setActive(true);
    CHECK(source.snapshot().samplePeakDb == audio::kSilenceDb);

    device = std::make_shared<audio::LevelsPlugin>();
    device->prepare({.sampleRate = 48000.0, .maximumBlockSize = 512});

    measureBlock(*device, 0.5f);
    REQUIRE(source.snapshot().samplePeakDb == audio::kSilenceDb);

    // The read handed the device the faceplate's state, so the next block is measured.
    measureBlock(*device, 0.5f);
    CHECK(source.snapshot().samplePeakDb == Catch::Approx(-6.02f).margin(0.01f));

    source.setActive(false);
    source.requestReset();
    measureBlock(*device, 1.0f);
    CHECK(source.snapshot().samplePeakDb == Catch::Approx(-6.02f).margin(0.01f));
}
