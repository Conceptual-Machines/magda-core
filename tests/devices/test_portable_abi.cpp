#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <magda/sdk/abi/AbiDevice.hpp>
#include <magda/sdk/abi/AbiHarness.hpp>
#include <string>
#include <vector>

#include "devices/tone/ToneGenerator.hpp"

using magda::devices::ToneGenerator;
using magda::sdk::host::AbiDevice;
using magda::sdk::host::linkedModule;

namespace {

/// Renders @p frames of the Test Tone at @p waveform through the C ABI, in blocks of 64.
std::vector<float> renderThroughAbi(float waveform, int frames) {
    AbiDevice device(*linkedModule(), ToneGenerator::kDeviceType);
    REQUIRE(device);
    REQUIRE(device.prepare(48000.0, 64) == MAGDA_OK);
    device.api().set_param(device.get(), ToneGenerator::kWaveformParamIndex, waveform);
    std::vector<float> left(static_cast<size_t>(frames)), right(left.size());
    for (int start = 0; start < frames; start += 64) {
        std::array<float*, 2> channels{left.data() + start, right.data() + start};
        device.process(channels.data(), 2, std::min(64, frames - start));
    }
    return left;
}

std::vector<float> renderDirect(float waveform, int frames) {
    ToneGenerator tone;
    tone.prepare({48000.0, 64});
    tone.setParameterValue(ToneGenerator::kWaveformParamIndex, waveform);
    std::vector<float> left(static_cast<size_t>(frames)), right(left.size());
    for (int start = 0; start < frames; start += 64) {
        std::array<float*, 2> channels{left.data() + start, right.data() + start};
        magda::sdk::ProcessContext context;
        context.audio = magda::BufferView(channels.data(), 2, std::min(64, frames - start));
        tone.process(context);
    }
    return left;
}

}  // namespace

TEST_CASE("The portable module conforms to the C ABI", "[devices][abi]") {
    const auto failures = magda::sdk::host::checkModuleConformance(linkedModule());
    for (const auto& failure : failures)
        UNSCOPED_INFO(failure);
    CHECK(failures.empty());
}

TEST_CASE("The Test Tone renders identically through the C ABI", "[devices][abi]") {
    for (int waveform = 0; waveform < ToneGenerator::kWaveformCount; ++waveform) {
        const auto normalized = static_cast<float>(waveform) / (ToneGenerator::kWaveformCount - 1);
        INFO("waveform " << waveform);
        CHECK(renderThroughAbi(normalized, 1000) == renderDirect(normalized, 1000));
    }
}

TEST_CASE("The Test Tone's type manifest keeps the saved slot ids", "[devices][abi]") {
    const auto* module = linkedModule();
    std::string manifest;
    for (int i = 0; i < module->device_type_count; ++i)
        if (std::string(module->device_types[i].id) == ToneGenerator::kDeviceType)
            manifest = module->device_types[i].manifest;
    const auto at = [&manifest](const char* id) { return manifest.find(id); };
    CHECK(at("\"id\":\"oscType\",\"index\":0") != std::string::npos);
    CHECK(at("\"id\":\"bandLimit\",\"index\":1") != std::string::npos);
    CHECK(at("\"id\":\"frequency\",\"index\":2") != std::string::npos);
    CHECK(at("\"id\":\"level\",\"index\":3") != std::string::npos);
}

TEST_CASE("Every portable device renders through the C ABI", "[devices][abi]") {
    const auto* module = linkedModule();
    REQUIRE(module->device_type_count == 23);
    for (int index = 0; index < module->device_type_count; ++index) {
        const std::string type = module->device_types[index].id;
        INFO(type);
        AbiDevice device(*module, type.c_str());
        REQUIRE(device);
        REQUIRE(device.prepare(48000.0, 256) == MAGDA_OK);

        std::vector<float> left(4800), right(4800);
        for (size_t i = 0; i < left.size(); ++i)
            left[i] = right[i] = 0.5f * std::sin(0.0288f * static_cast<float>(i));
        std::array<float*, 2> channels{left.data(), right.data()};
        REQUIRE(device.process(channels.data(), 2, 4800) == MAGDA_OK);

        float peak = 0.0f;
        for (const float sample : left) {
            REQUIRE(std::isfinite(sample));
            peak = std::max(peak, std::abs(sample));
        }
        CHECK(peak > 0.0f);
    }
}
