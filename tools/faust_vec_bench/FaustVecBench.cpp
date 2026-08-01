// Scalar vs vectorized throughput for the compiled Faust devices (issue #1397).
//
// Every device is generated twice by this tool's CMakeLists: once the way the
// build has always generated it, once with `-vec -vs N`. Both classes are
// linked into this one binary so a single run measures the pair under
// identical conditions, and the two variants are timed alternately so a
// thermal ramp cannot favour whichever ran first.
//
// The numbers are only meaningful when this target is optimized, which its
// CMakeLists forces regardless of CMAKE_BUILD_TYPE.

#include <faust/dsp/dsp.h>
#include <faust/gui/MapUI.h>
#include <faust/gui/meta.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "FaustVecBenchGenerated.hpp"

namespace {

struct Variant {
    const char* label;
    dsp* (*make)();
};

struct Device {
    const char* name;
    Variant scalar;
    Variant vec;
};

#define MAGDA_VEC_BENCH_ENTRY(NAME, SCALAR_CLASS, VEC_CLASS)                                       \
    Device{#NAME, Variant{"scalar", []() -> dsp* { return new SCALAR_CLASS(); }},                  \
           Variant{"vec", []() -> dsp* { return new VEC_CLASS(); }}},

const std::vector<Device> devices = {MAGDA_VEC_BENCH_ENTRIES};

#undef MAGDA_VEC_BENCH_ENTRY

// Instruments idle at gate 0, and an idle envelope is not the code path worth
// measuring. Drive every device into a sounding state before timing it.
void primeControls(MapUI& ui) {
    for (int i = 0; i < ui.getParamsCount(); ++i) {
        const std::string path = ui.getParamAddress(i);
        auto endsWith = [&path](const char* suffix) {
            const size_t n = std::strlen(suffix);
            return path.size() >= n && path.compare(path.size() - n, n, suffix) == 0;
        };

        if (endsWith("/gate") || endsWith("/trigger"))
            ui.setParamValue(path, 1.0f);
        else if (endsWith("/freq"))
            ui.setParamValue(path, 440.0f);
        else if (endsWith("/gain"))
            ui.setParamValue(path, 0.5f);
    }
}

struct Harness {
    std::unique_ptr<dsp> instance;
    MapUI ui;
    std::vector<std::vector<FAUSTFLOAT>> inStorage, outStorage;
    std::vector<FAUSTFLOAT*> in, out;
    int blockSize = 0;

    Harness(dsp* raw, int sampleRate, int block) : instance(raw), blockSize(block) {
        instance->init(sampleRate);
        instance->buildUserInterface(&ui);
        primeControls(ui);

        inStorage.assign(size_t(instance->getNumInputs()),
                         std::vector<FAUSTFLOAT>(size_t(block), 0.0f));
        outStorage.assign(size_t(instance->getNumOutputs()),
                          std::vector<FAUSTFLOAT>(size_t(block), 0.0f));

        for (size_t c = 0; c < inStorage.size(); ++c) {
            for (int i = 0; i < block; ++i)
                inStorage[c][size_t(i)] = 0.25f * std::sin(0.05f * float(i) + 0.7f * float(c));
            in.push_back(inStorage[c].data());
        }
        for (auto& channel : outStorage)
            out.push_back(channel.data());
    }

    void run(int blocks) {
        for (int b = 0; b < blocks; ++b)
            instance->compute(blockSize, in.data(), out.data());
    }

    double peakDifferenceFrom(const Harness& other) const {
        double worst = 0.0;
        for (size_t c = 0; c < outStorage.size() && c < other.outStorage.size(); ++c)
            for (size_t i = 0; i < outStorage[c].size(); ++i)
                worst = std::max(
                    worst, std::abs(double(outStorage[c][i]) - double(other.outStorage[c][i])));
        return worst;
    }
};

// Confirms the two variants are the same DSP. Run on fresh instances over a
// short window: `-vec` reorders arithmetic, so the last bits can differ, and
// on a free-running oscillator that divergence compounds. A few blocks from
// init catches a genuine codegen bug without failing on rounding.
double compareOutputs(const Device& device, int sampleRate, int blockSize) {
    Harness scalar(device.scalar.make(), sampleRate, blockSize);
    Harness vec(device.vec.make(), sampleRate, blockSize);
    scalar.run(8);
    vec.run(8);
    return vec.peakDifferenceFrom(scalar);
}

double timeRun(Harness& h, int blocks) {
    const auto t0 = std::chrono::steady_clock::now();
    h.run(blocks);
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

// Pick a block count that puts one timed run near targetSeconds, so a fast
// device is not measured against clock granularity and a slow one does not
// stretch the job.
int calibrate(Harness& h, double targetSeconds) {
    int blocks = 64;
    for (int attempt = 0; attempt < 12; ++attempt) {
        const double elapsed = timeRun(h, blocks);
        if (elapsed > targetSeconds * 0.5)
            return std::max(1, int(double(blocks) * targetSeconds / elapsed));
        blocks *= 4;
    }
    return blocks;
}

}  // namespace

int main(int argc, char** argv) {
    const int sampleRate = 48000;
    int blockSize = 128;
    int reps = 7;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--block") == 0 && i + 1 < argc)
            blockSize = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--reps") == 0 && i + 1 < argc)
            reps = std::atoi(argv[++i]);
    }

    std::printf("Faust codegen benchmark: scalar vs -vec -vs %d\n", MAGDA_VEC_BENCH_VECTOR_SIZE);
    std::printf("block %d, %d reps, best of each, %d Hz\n\n", blockSize, reps, sampleRate);
    std::printf("| device | scalar (x realtime) | vec (x realtime) | change | peak diff |\n");
    std::printf("|---|---|---|---|---|\n");

    int wins = 0, losses = 0;
    double worst = 1e30, best = -1e30;

    for (const auto& device : devices) {
        const double drift = compareOutputs(device, sampleRate, blockSize);

        Harness scalar(device.scalar.make(), sampleRate, blockSize);
        Harness vec(device.vec.make(), sampleRate, blockSize);

        const int blocks = calibrate(scalar, 0.15);

        double bestScalar = 1e30, bestVec = 1e30;
        for (int rep = 0; rep < reps; ++rep) {
            bestScalar = std::min(bestScalar, timeRun(scalar, blocks));
            bestVec = std::min(bestVec, timeRun(vec, blocks));
        }

        const double audioSeconds = double(blocks) * double(blockSize) / double(sampleRate);
        const double scalarRt = audioSeconds / bestScalar;
        const double vecRt = audioSeconds / bestVec;
        const double change = (vecRt / scalarRt - 1.0) * 100.0;

        std::printf("| %s | %.0f | %.0f | %+.0f%% | %.1e |\n", device.name, scalarRt, vecRt, change,
                    drift);

        if (change > 2.0)
            ++wins;
        else if (change < -2.0)
            ++losses;
        worst = std::min(worst, change);
        best = std::max(best, change);
    }

    std::printf("\n%d faster, %d slower, %zu total (range %+.0f%% to %+.0f%%)\n", wins, losses,
                devices.size(), worst, best);
    return 0;
}
