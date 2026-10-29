// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>
#include <vector>

#include "../../magda/daw/audio/analysis/SpectrumAnalysis.hpp"

using magda::daw::audio::nearestNoteLabel;
using magda::daw::audio::OverlappedSpectrum;
using magda::daw::audio::spectrumColumns;

namespace {

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;

std::vector<float> sines(std::initializer_list<double> freqs, int numSamples, float amp = 1.0f) {
    std::vector<float> out(static_cast<size_t>(numSamples), 0.0f);
    for (double f : freqs)
        for (int i = 0; i < numSamples; ++i)
            out[static_cast<size_t>(i)] +=
                amp * static_cast<float>(std::sin(2.0 * kPi * f * i / kSr));
    return out;
}

std::vector<float> analyseOnce(int order, const std::vector<float>& signal) {
    OverlappedSpectrum spectrum;
    spectrum.prepare(order);
    std::vector<float> db;
    spectrum.analyse(signal.data(), static_cast<int>(signal.size()), signal.size(), db);
    return db;
}

float maxIn(const std::vector<float>& db, double binHz, double loHz, double hiHz) {
    float m = -1000.0f;
    for (size_t i = 1; i < db.size(); ++i)
        if (i * binHz >= loHz && i * binHz <= hiHz)
            m = std::max(m, db[i]);
    return m;
}

}  // namespace

TEST_CASE("A full-scale sine centred on a bin reads 0 dBFS at every size", "[spectrum][3185]") {
    for (int order = 11; order <= 13; ++order) {
        const int n = 1 << order;
        const double binHz = kSr / n;
        const auto db = analyseOnce(order, sines({100 * binHz}, n));
        CHECK(db[100] == Catch::Approx(0.0f).margin(0.05f));
    }
}

TEST_CASE("A sine between bins loses no more than the Hann scalloping", "[spectrum][3185]") {
    const auto db = analyseOnce(13, sines({1000.0}, 8192));
    CHECK(maxIn(db, kSr / 8192, 990.0, 1010.0) > -1.5f);
}

// Hann at 8192 separates tones ~2.5 bins apart; 40 vs 50 Hz is under two and merges.
TEST_CASE("40 Hz and 55 Hz sines are told apart at 8192", "[spectrum][3185]") {
    const auto db = analyseOnce(13, sines({40.0, 55.0}, 8192 + 4 * 4096, 0.5f));
    const double binHz = kSr / 8192;
    std::vector<float> columns(800);
    spectrumColumns(db, binHz, 20.0f, 20000.0f, columns);

    auto columnAt = [&](double hz) {
        const double t = std::log(hz / 20.0) / std::log(1000.0);
        return columns[static_cast<size_t>(t * columns.size())];
    };
    CHECK(columnAt(47.0) < std::min(columnAt(40.0), columnAt(55.0)) - 6.0f);
}

TEST_CASE("Every frame written since the last call is read, up to the cap", "[spectrum][3185]") {
    OverlappedSpectrum spectrum;
    spectrum.prepare(11);
    const int n = spectrum.fftSize();
    const int hop = n / 2;

    CHECK(spectrum.samplesToRead(0, 32768) == 0);
    CHECK(spectrum.samplesToRead(10000, 32768) == n);

    std::vector<float> buf(32768, 0.0f);
    std::vector<float> db;
    spectrum.analyse(buf.data(), n, 10000, db);
    CHECK(spectrum.samplesToRead(10000, 32768) == 0);
    CHECK(spectrum.samplesToRead(10000 + 3 * hop, 32768) == n + 2 * hop);
    CHECK(spectrum.samplesToRead(10000 + 100 * hop, 4 * n) == 4 * n);
}

TEST_CASE("Averaged noise draws a steadier top end than the raw bins", "[spectrum][3185]") {
    std::mt19937 rng(7);
    std::normal_distribution<float> noise(0.0f, 0.1f);
    std::vector<float> signal(32768);
    for (auto& s : signal)
        s = noise(rng);

    OverlappedSpectrum spectrum;
    spectrum.prepare(13);
    const double binHz = kSr / 8192;
    std::vector<float> db, first(400), second(400);

    // Two ticks, each averaging the frames of its own half of the signal.
    spectrum.analyse(signal.data(), 16384, 16384, db);
    spectrumColumns(db, binHz, 5000.0f, 20000.0f, first);
    spectrum.analyse(signal.data() + 16384, 16384, 32768, db);
    spectrumColumns(db, binHz, 5000.0f, 20000.0f, second);

    double sq = 0.0;
    for (size_t c = 0; c < first.size(); ++c)
        sq += (first[c] - second[c]) * (first[c] - second[c]);
    const double columnRms = std::sqrt(sq / first.size());

    // The old trace: one frame per tick, every bin drawn.
    const auto rawA = analyseOnce(13, {signal.begin() + 8192, signal.begin() + 16384});
    const auto rawB = analyseOnce(13, {signal.begin() + 24576, signal.end()});
    double rawSq = 0.0;
    int rawCount = 0;
    for (size_t i = static_cast<size_t>(5000 / binHz); i < rawA.size(); ++i, ++rawCount)
        rawSq += (rawA[i] - rawB[i]) * (rawA[i] - rawB[i]);
    const double rawRms = std::sqrt(rawSq / rawCount);
    INFO("column rms " << columnRms << " dB, raw bin rms " << rawRms << " dB");
    CHECK(columnRms < rawRms / 3.0);
}

TEST_CASE("A column between bins never dips below its neighbours", "[spectrum][3185]") {
    std::vector<float> db(1024, -60.0f);
    db[3] = 0.0f;
    std::vector<float> columns(1000);
    spectrumColumns(db, 5.0, 10.0f, 40.0f, columns);
    for (float v : columns) {
        CHECK(v <= 0.0f);
        CHECK(v >= -60.0f);
    }
}

TEST_CASE("The hover note is named as the rest of the app names notes", "[spectrum][3185]") {
    CHECK(nearestNoteLabel(440.0) == "A3 +0c");
    CHECK(nearestNoteLabel(261.6256) == "C3 +0c");
    CHECK(nearestNoteLabel(452.0) == "A3 +47c");
    CHECK(nearestNoteLabel(430.0) == "A3 -40c");
}

TEST_CASE("A 50 Hz sine draws as a bump at 8192", "[spectrum][3185]") {
    const auto columnsFor = [](int order) {
        const int n = 1 << order;
        const auto db = analyseOnce(order, sines({50.0}, n + 4 * (n / 2)));
        std::vector<float> columns(800);
        spectrumColumns(db, kSr / n, 20.0f, 20000.0f, columns);
        return columns;
    };
    const auto columnAt = [](const std::vector<float>& columns, double hz) {
        const double t = std::log(hz / 20.0) / std::log(1000.0);
        return columns[static_cast<size_t>(t * columns.size())];
    };
    const auto wide = columnsFor(11);
    const auto fine = columnsFor(13);
    INFO("2048: 25 Hz " << columnAt(wide, 25.0) << ", 50 Hz " << columnAt(wide, 50.0));
    INFO("8192: 25 Hz " << columnAt(fine, 25.0) << ", 50 Hz " << columnAt(fine, 50.0) << ", 80 Hz "
                        << columnAt(fine, 80.0));
    CHECK(columnAt(fine, 50.0) - columnAt(fine, 25.0) > 30.0f);
    CHECK(columnAt(fine, 50.0) - columnAt(fine, 80.0) > 30.0f);
}

TEST_CASE("A tone between two bins draws a rounded peak above both", "[spectrum][3185]") {
    std::vector<float> db(1024, -60.0f);
    db[3] = -6.0f;
    db[4] = 0.0f;
    db[5] = 0.0f;
    db[6] = -6.0f;
    std::vector<float> columns(1000);
    spectrumColumns(db, 1.0, 4.0f, 5.0f, columns);
    const float middle = columns[columns.size() / 2];
    CHECK(middle > 0.0f);
    CHECK(middle <= 1.42f);
}
