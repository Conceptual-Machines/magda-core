#include <juce_events/juce_events.h>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>

#include "EngineRig.hpp"
#include "LatencyProbe.hpp"
#include "MgdFixture.hpp"
#include "NullDiffCase.hpp"
#include "NullDiffHostedPlugin.hpp"
#include "ParityMeasure.hpp"
#include "PumpThread.hpp"
#include "exec/BlockProfile.hpp"
#include "magda/daw/core/TempoMap.hpp"
#include "magda/daw/engine/AudioEngine.hpp"
#include "magda/daw/engine/PluginService.hpp"
#include "magda/daw/project/ProjectManager.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"

/**
 * @file parity_bench_main.cpp
 * @brief One measurement of one engine on one corpus project (#2082).
 *
 * scripts/parity_bench.py runs this once per engine, project and block size, each in a process
 * of its own so memory is the session's alone, and judges native against the fork. The result
 * is the last stdout line that starts with "PARITY-RESULT ". docs/development/parity-bench.md
 * has the method.
 */

using namespace magda;
using namespace magda::parity;

namespace {

constexpr const char* kResultPrefix = "PARITY-RESULT ";

/// How long a load may take before it is reported rather than waited on.
constexpr double kLoadTimeoutSeconds = 180.0;

/// How far either side of its place the latency probe is looked for.
constexpr double kProbeSearchSeconds = 0.25;

/// A correlation peak this many times the median is a probe found rather than a guess.
constexpr double kProbeConfidence = 8.0;

struct Options {
    std::string engine;
    std::string project;
    double sampleRate = 44100.0;
    int blockSize = 512;
    int passes = 2;
    double speed = 1.0;
    bool latency = false;
    bool list = false;
};

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

double megabytes(std::int64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

const char* buildType() {
#if defined(NDEBUG)
    return "release";
#else
    return "debug";
#endif
}

void emit(const juce::var& result) {
    std::cout << std::flush;
    std::cout << "\n" << kResultPrefix << juce::JSON::toString(result, true) << std::endl;
}

juce::var failed(juce::DynamicObject::Ptr result, const std::string& status,
                 const std::string& reason) {
    result->setProperty("status", juce::String(status));
    result->setProperty("reason", juce::String(reason));
    return result.get();
}

/// Pumps the message loop until @p done holds, or @p timeoutSeconds pass.
bool dispatchUntil(const std::function<bool()>& done, double timeoutSeconds) {
    const auto deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                             std::chrono::duration<double>(timeoutSeconds));
    while (!done()) {
        if (Clock::now() > deadline)
            return false;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    }
    return true;
}

const nulldiff::MgdFixture* findFixture(const std::string& name) {
    for (const auto& fixture : nulldiff::mgdFixtures())
        if (fixture.declaration.name == name)
            return &fixture;
    return nullptr;
}

juce::File scratchFor(const Options& options) {
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("magda-parity-bench")
                    .getChildFile(options.engine);
    root.createDirectory();
    return root;
}

/// The plugins @p staged hosts that this machine has not got, after looking for them by name.
std::vector<std::string> absentPlugins(const nulldiff::MgdFixture& fixture,
                                       const StagedProjectData& staged) {
    auto& plugins = PluginService::getInstance();
    if (plugins.formats() == nullptr || plugins.knownList() == nullptr)
        return {"the plugin service has no list"};

    std::vector<std::string> names(fixture.hostedPlugins.begin(), fixture.hostedPlugins.end());
    nulldiff::addInstalledPluginsNamed(names, *plugins.formats(), *plugins.knownList());

    nulldiff::Case project;
    project.tracks = staged.tracks;
    if (staged.masterTrack != nullptr)
        project.master = *staged.masterTrack;
    return nulldiff::absentPluginsIn(project, plugins.knownList());
}

/// What a measurement leaves running. Held past the result's emission, and never torn down.
struct Running {
    std::unique_ptr<EngineRig> rig;
    std::unique_ptr<PumpThread> pump;
};

juce::var measure(const Options& options, Running& running) {
    juce::DynamicObject::Ptr result = new juce::DynamicObject();
    result->setProperty("engine", juce::String(options.engine));
    result->setProperty("project", juce::String(options.project));
    result->setProperty("block_size", options.blockSize);
    result->setProperty("sample_rate", options.sampleRate);
    result->setProperty("build", buildType());

    const auto* fixture = findFixture(options.project);
    if (fixture == nullptr)
        return failed(result, "failed", "no corpus project called " + options.project);

    std::string failure;
    running.rig = EngineRig::create(options.engine, options.sampleRate, options.blockSize, failure);
    if (running.rig == nullptr)
        return failed(result, "failed", failure);

    auto& rig = running.rig;
    running.pump = std::make_unique<PumpThread>(rig->backend(), options.sampleRate,
                                                options.blockSize, options.speed);
    auto& pump = *running.pump;
    if (!dispatchUntil([&rig] { return rig->isReady(); }, kLoadTimeoutSeconds))
        return failed(result, "failed", "the engine did not settle on the pump before any load");

    const auto scratch = scratchFor(options);
    StagedProjectData staged;
    std::map<juce::String, juce::File> written;
    if (auto refusal = nulldiff::stageFixture(*fixture, scratch, staged, written); !refusal.empty())
        return failed(result, "failed", refusal);

    if (const auto absent = absentPlugins(*fixture, staged); !absent.empty()) {
        juce::StringArray names;
        for (const auto& name : absent)
            names.add(name);
        return failed(result, "not_run",
                      "not on this machine: " + names.joinIntoString(", ").toStdString());
    }

    auto& engine = rig->engine();
    const auto& declaration = fixture->declaration;
    const auto commitTiming = [&engine](const ProjectInfo& info) {
        engine.setTempo(info.tempo);
        engine.setTimeSignature(info.timeSignatureNumerator, info.timeSignatureDenominator);
    };

    // At the export's first sample, so no clip start falls between two samples.
    std::optional<LatencyProbe> probe;
    if (options.latency) {
        probe = addLatencyProbe(staged, scratch, declaration.startBeat, options.sampleRate);
        if (!probe)
            return failed(result, "failed", "the probe's file could not be written");
    }

    const auto baseline = currentFootprintBytes();
    FootprintSampler sampler;

    rig->loadStarting();
    const auto loadStarted = Clock::now();
    ProjectManager::getInstance().commitStagedProject(
        staged, scratch.getChildFile(juce::String(declaration.name) + ".mgd"), false, commitTiming);

    if (!dispatchUntil([&rig] { return rig->isReady(); }, kLoadTimeoutSeconds))
        return failed(result, "failed", "the project was not ready to play after the load timeout");

    const auto loadMs = millisecondsSince(loadStarted);
    const auto* tempo = engine.tempoMap();
    const auto startSeconds = tempo->beatToTime(declaration.startBeat);
    const auto endSeconds = tempo->beatToTime(declaration.endBeat);

    if (options.latency) {
        sampler.stop();

        juce::DynamicObject::Ptr latency = new juce::DynamicObject();
        const auto reported = rig->reportedLatencySamples();
        latency->setProperty("reported_samples", reported);

        const auto bounce = scratch.getChildFile("parity-probe-render.wav");
        bounce.deleteFile();

        OfflineRenderRequest request;
        request.destination = bounce;
        request.bitDepth = 32;
        request.dither = OfflineRenderDither::None;
        request.sampleRate = options.sampleRate;
        request.blockSize = options.blockSize;
        request.range = {{declaration.startBeat}, {declaration.endBeat}};

        auto session = engine.createOfflineRenderSession(false);
        auto task = session != nullptr ? session->createTask(request) : nullptr;
        const auto rendered = task != nullptr ? task->run() : OfflineRenderResult{};
        const auto searched =
            static_cast<int>(std::llround(kProbeSearchSeconds * options.sampleRate));
        const auto finding =
            rendered.success ? findLatencyProbe(bounce, *probe, searched) : std::nullopt;

        if (!finding) {
            latency->setProperty("status", "unmeasured");
            latency->setProperty("reason", "the export did not render: " + rendered.error);
        } else if (finding->confidence < kProbeConfidence) {
            latency->setProperty("status", "unmeasured");
            latency->setProperty("reason", "the probe was not found in the export");
        } else {
            // An export is trimmed by the reported latency, so the probe lands late by exactly
            // what the report left out.
            latency->setProperty("status", "ok");
            latency->setProperty("measured_samples", reported + finding->offsetSamples);
        }
        if (finding)
            latency->setProperty("confidence", finding->confidence);

        result->setProperty("status", "ok");
        result->setProperty("latency", latency.get());
        return result.get();
    }

    const auto windowSamples =
        static_cast<std::int64_t>(std::llround((endSeconds - startSeconds) * options.sampleRate));

    engine.onLoopRegionChanged(startSeconds, endSeconds, true);
    engine.onTransportPlay(startSeconds);
    pump.measure(windowSamples, windowSamples * options.passes);

    std::optional<SpanResult> span;
    const auto playSeconds = static_cast<double>(windowSamples) * (options.passes + 1) /
                             options.sampleRate / options.speed;
    if (!dispatchUntil([&] { return (span = pump.result()).has_value(); }, playSeconds + 60.0))
        return failed(result, "failed", "the measured passes did not finish");

    const auto peak = sampler.stop();

    juce::DynamicObject::Ptr memory = new juce::DynamicObject();
    memory->setProperty("baseline_mb", megabytes(baseline));
    memory->setProperty("peak_mb", megabytes(peak));
    memory->setProperty("session_mb", megabytes(std::max<std::int64_t>(0, peak - baseline)));

    const auto blockMicros = 1.0e6 * options.blockSize / options.sampleRate;
    juce::DynamicObject::Ptr cpu = new juce::DynamicObject();
    cpu->setProperty("blocks", span->blocks.blocks);
    cpu->setProperty("block_us", blockMicros);
    cpu->setProperty("mean_us", span->blocks.meanUs);
    cpu->setProperty("p50_us", span->blocks.p50Us);
    cpu->setProperty("p95_us", span->blocks.p95Us);
    cpu->setProperty("p99_us", span->blocks.p99Us);
    cpu->setProperty("max_us", span->blocks.maxUs);
    cpu->setProperty("load_mean", span->blocks.meanUs / blockMicros);
    cpu->setProperty("overruns", span->overruns);
    cpu->setProperty("process_cpu_us_per_block", span->processCpuUsPerBlock);

    result->setProperty("load_ms", loadMs);
    result->setProperty("memory", memory.get());
    result->setProperty("cpu", cpu.get());
    result->setProperty("output_peak", span->outputPeak);
    juce::Array<juce::var> envelope;
    for (const auto peak : span->envelope)
        envelope.add(peak);
    result->setProperty("envelope", envelope);

    // A silent engine is a cheap one, and a cheap number is not a measurement.
    if (span->outputPeak <= 0.0f)
        return failed(result, "failed", "the engine rendered silence through the measured passes");

    result->setProperty("status", "ok");
    return result.get();
}

juce::var listProjects(const Options& options) {
    juce::Array<juce::var> projects;
    for (const auto& fixture : nulldiff::mgdFixtures()) {
        juce::DynamicObject::Ptr project = new juce::DynamicObject();
        project->setProperty("name", juce::String(fixture.declaration.name));

        juce::Array<juce::var> plugins;
        for (const auto* plugin : fixture.hostedPlugins)
            plugins.add(juce::String(plugin));
        project->setProperty("hosted_plugins", plugins);
        projects.add(project.get());
    }

    juce::DynamicObject::Ptr result = new juce::DynamicObject();
    result->setProperty("status", "ok");
    result->setProperty("build", buildType());
    result->setProperty("sample_rate", options.sampleRate);
    result->setProperty("projects", projects);
    return result.get();
}

std::optional<Options> parse(int argc, char* argv[], std::string& error) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        const auto value = [&]() -> std::string {
            return index + 1 < argc ? argv[++index] : std::string{};
        };

        if (flag == "--list")
            options.list = true;
        else if (flag == "--latency")
            options.latency = true;
        else if (flag == "--engine")
            options.engine = value();
        else if (flag == "--project")
            options.project = value();
        else if (flag == "--block-size")
            options.blockSize = std::atoi(value().c_str());
        else if (flag == "--sample-rate")
            options.sampleRate = std::atof(value().c_str());
        else if (flag == "--passes")
            options.passes = std::atoi(value().c_str());
        else if (flag == "--speed")
            options.speed = std::atof(value().c_str());
        else {
            error = "unknown argument " + flag;
            return std::nullopt;
        }
    }

    if (!options.list && (options.engine.empty() || options.project.empty()))
        error = "--engine and --project are required";
    else if (options.blockSize <= 0 || options.sampleRate <= 0.0 || options.passes <= 0 ||
             options.speed <= 0.0)
        error = "block size, sample rate, passes and speed must be positive";

    return error.empty() ? std::optional<Options>(options) : std::nullopt;
}

}  // namespace

int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::string error;
    const auto options = parse(argc, argv, error);
    if (!options) {
        juce::DynamicObject::Ptr result = new juce::DynamicObject();
        emit(failed(result, "failed", error));
        std::_Exit(2);
    }

    Running running;
    emit(options->list ? listProjects(*options) : measure(*options, running));

    // Engine and singleton teardown is not what this measures, and it is where a crash would
    // cost the result that was already printed.
    if (engine::BlockProfile::enabled())
        engine::BlockProfile::report();
    std::cout.flush();
    std::_Exit(0);
}
