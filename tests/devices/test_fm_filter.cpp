// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <utility>
#include <vector>

#include "DeviceTestBlock.hpp"
#include "TestDeviceMidiBuffer.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaFMCompiledPlugin.hpp"

// FM's filter (#3120), and its switches mid-note (#3246).

using FM = magda::daw::audio::compiled::MagdaFMCompiledPlugin;

namespace {

constexpr int kBlockSize = 256;
constexpr double kSampleRate = 48000.0;
constexpr int kOp1WaveSlot = 28;
constexpr int kAmpSustainSlot = 26;

using Settings = std::vector<std::pair<int, float>>;

/// @p slot moves to @p value before block @p block.
struct Change {
    int block;
    int slot;
    float value;
};

/// The left channel of @p blocks of FM at @p settings and then @p changes, @p notes held from
/// the first block.
std::vector<float> render(const Settings& settings, const std::vector<int>& notes, int blocks,
                          double rate = kSampleRate, const std::vector<Change>& changes = {}) {
    FM synth;
    synth.prepare({.sampleRate = rate, .maximumBlockSize = kBlockSize});
    auto set = [&synth](int slot, float value) {
        synth.getSlotParameter(slot).setValueFromHost(synth.displayValueToNativeValue(slot, value));
    };
    for (auto [slot, value] : settings)
        set(slot, value);

    std::vector<float> out;
    for (int block = 0; block < blocks; ++block) {
        for (const auto& change : changes)
            if (change.block == block)
                set(change.slot, change.value);
        juce::AudioBuffer<float> audio(2, kBlockSize);
        audio.clear();
        magda::test::DeviceMidiBuffer midi, midiOut;
        if (block == 0)
            for (int note : notes)
                midi.events.push_back({juce::MidiMessage::noteOn(1, note, 1.0f), 0});
        magda::test::DeviceTestBlock contextBlock(audio, kBlockSize);
        contextBlock.context.midiIn = &midi;
        contextBlock.context.midiOut = &midiOut;
        contextBlock.context.isPlaying = true;
        synth.process(contextBlock.context);
        out.insert(out.end(), audio.getReadPointer(0), audio.getReadPointer(0) + kBlockSize);
    }
    return out;
}

float rms(const std::vector<float>& samples, size_t from = 0, size_t to = 0) {
    to = to == 0 ? samples.size() : to;
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += static_cast<double>(samples[i]) * samples[i];
    return static_cast<float>(std::sqrt(sum / static_cast<double>(to - from)));
}

/// The largest sample-to-sample step in [@p from, @p to), @p from > 0.
float largestStep(const std::vector<float>& samples, size_t from, size_t to) {
    float worst = 0.0f;
    for (size_t i = from; i < to; ++i)
        worst = std::max(worst, std::abs(samples[i] - samples[i - 1]));
    return worst;
}

Settings with(Settings settings, const Settings& more) {
    settings.insert(settings.end(), more.begin(), more.end());
    return settings;
}

/// A held saw on op 1, the second half measured, through @p filter.
float heldSawRms(const Settings& filter, int note = 60) {
    Settings settings{{kOp1WaveSlot, 2.0f}, {4, 0.0f}, {kAmpSustainSlot, 1.0f}};
    settings.insert(settings.end(), filter.begin(), filter.end());
    const auto out = render(settings, {note}, 40);
    return rms(out, out.size() / 2);
}

Settings lowpass(float cutoffHz, int poles = 0) {
    return {{FM::kFilterEnableSlot, 1.0f},
            {FM::kFilterCutoffSlot, cutoffHz},
            {FM::kFilterResonanceSlot, 0.0f},
            {FM::kFilterPolesSlot, static_cast<float>(poles)}};
}

}  // namespace

TEST_CASE("With the filter off, no filter control changes the sound", "[fm][3120]") {
    const auto plain = render({}, {60, 64, 67}, 20);
    const auto tweaked = render({{FM::kFilterEngineSlot, 2.0f},
                                 {FM::kFilterCutoffSlot, 200.0f},
                                 {FM::kFilterResonanceSlot, 1.0f},
                                 {FM::kFilterDriveSlot, 1.0f},
                                 {FM::kFilterKeyTrackSlot, 1.0f},
                                 {FM::kFilterEnvSlot, 4.0f}},
                                {60, 64, 67}, 20);
    REQUIRE(rms(plain) > 0.01f);
    CHECK(plain == tweaked);
}

TEST_CASE("Every engine shapes the sound", "[fm][3120]") {
    const float dry = heldSawRms({});
    for (int engine = 0; engine < 3; ++engine) {
        auto filter = lowpass(400.0f);
        filter.push_back({FM::kFilterEngineSlot, static_cast<float>(engine)});
        const float wet = heldSawRms(filter);
        INFO("engine " << engine);
        CHECK(std::isfinite(wet));
        CHECK(std::abs(wet - dry) > 0.1f * dry);
    }
}

TEST_CASE("More SVF poles cut harder past the cutoff", "[fm][3120]") {
    const float two = heldSawRms(lowpass(300.0f, 0));
    const float four = heldSawRms(lowpass(300.0f, 1));
    const float six = heldSawRms(lowpass(300.0f, 2));
    CHECK(four < two);
    CHECK(six < four);
}

TEST_CASE("Key tracking moves the cutoff with the note", "[fm][3120]") {
    // Two octaves up a lowpass at a fixed cutoff loses more than one that tracks.
    const auto ratio = [](float keyTrack) {
        auto filter = lowpass(300.0f, 1);
        filter.push_back({FM::kFilterKeyTrackSlot, keyTrack});
        return heldSawRms(filter, 84) / heldSawRms(filter, 60);
    };
    CHECK(ratio(1.0f) > 1.5f * ratio(0.0f));
}

TEST_CASE("The filter envelope opens the cutoff over its attack", "[fm][3120]") {
    auto settings = lowpass(100.0f, 1);
    settings.insert(settings.end(), {{kOp1WaveSlot, 2.0f},
                                     {4, 0.0f},
                                     {kAmpSustainSlot, 1.0f},
                                     {FM::kFilterEnvSlot, 4.0f},
                                     {FM::kFilterAdsrBaseSlot + 0, 400.0f},
                                     {FM::kFilterAdsrBaseSlot + 2, 1.0f}});
    const auto out = render(settings, {60}, 300);
    const size_t early = static_cast<size_t>(0.02 * kSampleRate);
    const size_t late = static_cast<size_t>(1.2 * kSampleRate);
    CHECK(rms(out, late, late + early) > 2.0f * rms(out, early, 2 * early));
}

TEST_CASE("Full resonance stays finite and bounded on every engine", "[fm][3120]") {
    for (int engine = 0; engine < 3; ++engine) {
        for (float cutoff : {40.0f, 1000.0f, 20000.0f}) {
            Settings settings{{FM::kFilterEnableSlot, 1.0f},
                              {FM::kFilterEngineSlot, static_cast<float>(engine)},
                              {FM::kFilterPolesSlot, 2.0f},
                              {FM::kFilterCutoffSlot, cutoff},
                              {FM::kFilterResonanceSlot, 1.0f},
                              {FM::kFilterDriveSlot, 1.0f},
                              {FM::kFilterEnvSlot, 4.0f}};
            // Every operator at full level, feeding back on itself at full depth.
            for (int op = 0; op < 4; ++op) {
                settings.push_back({20 + op, 6.0f});
                settings.push_back({op * 5, 8.0f});
            }
            const auto out = render(settings, {36, 48, 60, 72, 84, 96}, 100);
            INFO("engine " << engine << " cutoff " << cutoff);
            CHECK(std::all_of(out.begin(), out.end(),
                              [](float s) { return std::isfinite(s) && std::abs(s) <= 1.0f; }));
        }
    }
}

// The filter as a matrix node (#3121).

TEST_CASE("In Output position the node's sends, returns and level do nothing", "[fm][3121]") {
    auto settings = lowpass(500.0f, 1);
    const auto plain = render(settings, {60, 67}, 20);
    for (int op = 0; op < 4; ++op) {
        settings.push_back({FM::kFilterSendBaseSlot + op, 1.0f});
        settings.push_back({FM::kFilterReturnBaseSlot + op, 8.0f});
    }
    settings.push_back({FM::kFilterLevelSlot, -20.0f});
    CHECK(render(settings, {60, 67}, 20) == plain);
}

TEST_CASE("In Matrix position with nothing sent, the operators pass unfiltered", "[fm][3121]") {
    auto settings = lowpass(200.0f, 2);
    settings.push_back({FM::kFilterPositionSlot, 1.0f});
    CHECK(render(settings, {60, 67}, 20) == render({}, {60, 67}, 20));
}

TEST_CASE("Sends reach the mix at F Level, and returns modulate the operators", "[fm][3121]") {
    auto base = lowpass(800.0f, 0);
    base.insert(base.end(), {{FM::kFilterPositionSlot, 1.0f}, {FM::kFilterResonanceSlot, 0.6f}});
    const auto dry = render(base, {60}, 20);

    auto sent = base;
    sent.push_back({FM::kFilterSendBaseSlot + 1, 1.0f});
    const auto withSend = render(sent, {60}, 20);
    CHECK(withSend != dry);

    // A silent F Level leaves only the returns to change the sound.
    sent.push_back({FM::kFilterLevelSlot, -60.0f});
    const auto silentNode = render(sent, {60}, 20);
    CHECK(silentNode == dry);
    sent.push_back({FM::kFilterReturnBaseSlot + 0, 4.0f});
    CHECK(render(sent, {60}, 20) != dry);
}

TEST_CASE("Every path at maximum with full resonance doesn't run away", "[fm][3121]") {
    for (int engine = 0; engine < 3; ++engine) {
        for (float cutoff : {40.0f, 1000.0f, 20000.0f}) {
            Settings settings{{FM::kFilterEnableSlot, 1.0f},
                              {FM::kFilterPositionSlot, 1.0f},
                              {FM::kFilterEngineSlot, static_cast<float>(engine)},
                              {FM::kFilterPolesSlot, 2.0f},
                              {FM::kFilterCutoffSlot, cutoff},
                              {FM::kFilterResonanceSlot, 1.0f},
                              {FM::kFilterDriveSlot, 1.0f},
                              {FM::kFilterLevelSlot, 6.0f}};
            for (int op = 0; op < 4; ++op) {
                settings.push_back({20 + op, 6.0f});
                settings.push_back({FM::kFilterSendBaseSlot + op, 1.0f});
                settings.push_back({FM::kFilterReturnBaseSlot + op, 8.0f});
                for (int dst = 0; dst < 4; ++dst)
                    settings.push_back({op * 4 + dst, 8.0f});
            }
            const auto out = render(settings, {36, 48, 60, 72, 84, 96}, 100);
            INFO("engine " << engine << " cutoff " << cutoff);
            CHECK(std::all_of(out.begin(), out.end(),
                              [](float s) { return std::isfinite(s) && std::abs(s) <= 1.0f; }));
        }
    }
}

TEST_CASE("Switching filter type, poles, engine or enable mid-note doesn't click", "[fm][3246]") {
    // As Poly Synth's (#3129): a held sine moves little from one sample to the next, so a step
    // stands out. Each value holds long enough to settle; its last 50 ms is the settled reference.
    // Every export rate: an unfaded comb input stepped only at 96 kHz and up.
    constexpr double kSampleRates[] = {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0};
    constexpr double kHold = 0.15;
    constexpr double kSettled = 0.05;
    const Settings sine{{4, 0.0f},
                        {kAmpSustainSlot, 1.0f},
                        {FM::kFilterEnableSlot, 1.0f},
                        {FM::kFilterCutoffSlot, 400.0f}};
    const Settings highpass = with(sine, {{FM::kFilterTypeSlot, 1.0f}});
    // Every pair of types, every ordered pair of three choices, and both ways twice.
    const std::vector<float> types{0.0f, 1.0f, 2.0f, 3.0f, 0.0f, 2.0f, 1.0f, 3.0f};
    const std::vector<float> threes{0.0f, 1.0f, 2.0f, 0.0f, 2.0f, 1.0f, 0.0f};
    const std::vector<float> onOff{0.0f, 1.0f, 0.0f, 1.0f};

    struct Sweep {
        Settings settings;
        int slot;
        const std::vector<float>& values;
    };
    std::vector<Sweep> sweeps{{sine, FM::kFilterTypeSlot, types},
                              {highpass, FM::kFilterEngineSlot, threes}};
    for (int type = 0; type < 4; ++type)
        sweeps.push_back({with(sine, {{FM::kFilterTypeSlot, static_cast<float>(type)}}),
                          FM::kFilterPolesSlot, threes});
    // Each engine at the output, and as a matrix node op 1 sends to and is modulated by.
    for (int engine = 0; engine < 3; ++engine) {
        const Settings on = with(highpass, {{FM::kFilterEngineSlot, static_cast<float>(engine)}});
        sweeps.push_back({on, FM::kFilterEnableSlot, onOff});
        sweeps.push_back({with(on, {{FM::kFilterPositionSlot, 1.0f},
                                    {FM::kFilterSendBaseSlot, 1.0f},
                                    {FM::kFilterReturnBaseSlot, 1.0f}}),
                          FM::kFilterEnableSlot, onOff});
    }

    for (double rate : kSampleRates) {
        auto blockAt = [rate](size_t hold) {
            return static_cast<int>(
                std::ceil(static_cast<double>(hold) * kHold * rate / kBlockSize));
        };
        for (const auto& sweep : sweeps) {
            std::vector<Change> changes;
            for (size_t i = 1; i < sweep.values.size(); ++i)
                changes.push_back({blockAt(i), sweep.slot, sweep.values[i]});
            const auto samples = render(with(sweep.settings, {{sweep.slot, sweep.values[0]}}), {60},
                                        blockAt(sweep.values.size()), rate, changes);

            const auto settled = static_cast<size_t>(kSettled * rate);
            for (size_t i = 1; i < sweep.values.size(); ++i) {
                const auto from = static_cast<size_t>(blockAt(i)) * kBlockSize;
                const auto to = static_cast<size_t>(blockAt(i + 1)) * kBlockSize;
                const float before = largestStep(samples, from - settled, from);
                const float after = largestStep(samples, to - settled, to);
                CAPTURE(rate, sweep.slot, sweep.values[i - 1], sweep.values[i], before, after);
                REQUIRE(after > 0.0f);
                CHECK(largestStep(samples, from, to) < 2.0f * std::max(before, after));
            }
        }
    }
}
