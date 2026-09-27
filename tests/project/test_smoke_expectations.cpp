#include <juce_core/juce_core.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include "LegacyCorpus.hpp"
#include "magda/daw/api/remote_api.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"

// The smoke project set (#2782): every expectation file in tests/smoke is well
// formed and names a project that loads with the tracks it checks. The format
// is documented in tests/smoke/README.md.

namespace magda {
namespace {

juce::File smokeDir() {
    return juce::File(MAGDA_SMOKE_DIR);
}

bool isNumber(const juce::var& v) {
    return v.isInt() || v.isInt64() || v.isDouble();
}

/// Every problem with one expectation file, so a failure lists them all.
juce::StringArray problemsWith(const juce::File& expectationFile) {
    juce::StringArray problems;
    const auto json = juce::JSON::parse(expectationFile.loadFileAsString());
    if (!json.isObject())
        return {"not a JSON object"};

    if (!json["version"].isInt() || static_cast<int>(json["version"]) != 1)
        problems.add("version must be 1");
    if (!json["cluster"].isString() || json["cluster"].toString().isEmpty())
        problems.add("cluster must be a non-empty string");
    if (!json["listen"].isString() || json["listen"].toString().isEmpty())
        problems.add("listen must be a non-empty string");

    const auto& range = json["range"];
    if (!isNumber(range["startBeat"]) || !isNumber(range["endBeat"]) ||
        static_cast<double>(range["startBeat"]) < 0.0 ||
        static_cast<double>(range["startBeat"]) >= static_cast<double>(range["endBeat"]))
        problems.add("range must hold startBeat >= 0 and endBeat > startBeat");
    if (range.hasProperty("seconds") &&
        (!isNumber(range["seconds"]) || static_cast<double>(range["seconds"]) <= 0.0))
        problems.add("range.seconds must be a positive number");

    const auto& requires_ = json["requires"];
    for (const char* key : {"plugins", "hardware"}) {
        const auto* list = requires_[key].getArray();
        if (list == nullptr) {
            problems.add(juce::String("requires.") + key + " must be an array");
            continue;
        }
        for (const auto& item : *list)
            if (!item.isString() || item.toString().isEmpty())
                problems.add(juce::String("requires.") + key + " holds a non-string");
    }
    if (const auto* hardware = requires_["hardware"].getArray())
        for (const auto& item : *hardware)
            if (item.toString() != "insert" && item.toString() != "loopback")
                problems.add("requires.hardware: unknown '" + item.toString() + "'");

    if (json.hasProperty("scenario")) {
        const auto* steps = json["scenario"].getArray();
        if (steps == nullptr)
            problems.add("scenario must be an array");
        for (const auto& step : steps != nullptr ? *steps : juce::Array<juce::var>{}) {
            const auto call = step["call"].toString();
            const auto* operation = remote::OperationRegistry::instance().find(call);
            if (operation == nullptr) {
                problems.add("scenario: no operation '" + call + "'");
                continue;
            }
            const auto beat = step["beat"];
            if (!isNumber(beat) ||
                static_cast<double>(beat) < static_cast<double>(range["startBeat"]) ||
                static_cast<double>(beat) >= static_cast<double>(range["endBeat"]))
                problems.add("scenario: " + call + " is not at a beat inside the range");
            if (!remote::validateJson(step["input"], operation->inputSchema).empty())
                problems.add("scenario: " + call + " input does not match its schema");
        }
    }

    const auto* tracks = json["tracks"].getArray();
    if (tracks == nullptr || tracks->isEmpty()) {
        problems.add("tracks must be a non-empty array");
        tracks = nullptr;
    }
    if (tracks != nullptr) {
        for (const auto& track : *tracks) {
            const auto name = track["name"].toString();
            if (!track["name"].isString() || name.isEmpty())
                problems.add("a track has no name");
            if (!track["sound"].isBool())
                problems.add("track '" + name + "': sound must be a boolean");
            const auto& peak = track["peakDb"];
            const bool sounds = track["sound"].isBool() && static_cast<bool>(track["sound"]);
            if (sounds && (!isNumber(peak["min"]) || !isNumber(peak["max"])))
                problems.add("track '" + name + "': a sounding track needs peakDb.min and max");
            if (isNumber(peak["min"]) && isNumber(peak["max"]) &&
                static_cast<double>(peak["min"]) > static_cast<double>(peak["max"]))
                problems.add("track '" + name + "': peakDb.min is above max");
        }
    }

    const auto* parameterChecks = json["parameters"].getArray();
    if (json.hasProperty("parameters") && parameterChecks == nullptr)
        problems.add("parameters must be an array");
    for (const auto& check :
         parameterChecks != nullptr ? *parameterChecks : juce::Array<juce::var>{}) {
        if (!check["track"].isString() || !check["device"].isString())
            problems.add("parameters: each check needs a track and a device name");
        const auto* values = check["normalized"].getDynamicObject();
        if (values == nullptr || values->getProperties().isEmpty())
            problems.add("parameters: normalized must name at least one parameter");
        for (const auto& value :
             values != nullptr ? values->getProperties() : juce::NamedValueSet{})
            if (!isNumber(value.value) || static_cast<double>(value.value) < 0.0 ||
                static_cast<double>(value.value) > 1.0)
                problems.add("parameters: " + value.name.toString() + " must be within 0..1");
    }

    const auto projectFile = expectationFile.getSiblingFile(json["project"].toString());
    if (!json["project"].isString() || !projectFile.existsAsFile()) {
        problems.add("project '" + json["project"].toString() + "' not found");
        return problems;
    }

    StagedProjectData staged;
    if (!ProjectSerializer::loadAndStage(projectFile, staged)) {
        problems.add("project does not load: " + ProjectSerializer::getLastError());
        return problems;
    }

    if (tracks != nullptr) {
        for (const auto& track : *tracks) {
            const auto name = track["name"].toString();
            const auto found = std::ranges::any_of(
                staged.tracks, [&name](const TrackInfo& t) { return t.name == name; });
            if (!found)
                problems.add("track '" + name + "' is not in the project");
        }
    }

    for (const auto& check :
         parameterChecks != nullptr ? *parameterChecks : juce::Array<juce::var>{}) {
        const auto trackName = check["track"].toString();
        const auto deviceName = check["device"].toString();
        const auto track = std::ranges::find(staged.tracks, trackName, &TrackInfo::name);
        bool found = false;
        if (track != staged.tracks.end())
            test::legacy_corpus::forEachDevice(*track, [&](const DeviceInfo& device) {
                found = found || device.name == deviceName;
            });
        if (!found)
            problems.add("parameters: no device '" + deviceName + "' on track '" + trackName + "'");
    }

    const bool core = requires_["plugins"].size() == 0 && requires_["hardware"].size() == 0;
    if (core) {
        const auto checkDevice = [&problems](const DeviceInfo& device) {
            if (device.format != PluginFormat::Internal)
                problems.add("core project hosts plugin '" + device.name + "'");
        };
        for (const auto& track : staged.tracks)
            test::legacy_corpus::forEachDevice(track, checkDevice);
        if (staged.masterTrack != nullptr)
            test::legacy_corpus::forEachDevice(*staged.masterTrack, checkDevice);

        for (const auto& source : staged.sources) {
            const auto file = projectFile.getParentDirectory().getChildFile(source.filePath);
            if (!file.existsAsFile() || !file.isAChildOf(projectFile.getParentDirectory()))
                problems.add("core project reads audio outside its folder: " + source.filePath);
        }
    }
    return problems;
}

}  // namespace

TEST_CASE("Smoke expectation files are well formed and match their projects", "[smoke][project]") {
    const auto files = smokeDir().findChildFiles(juce::File::findFiles, true, "*.smoke.json");
    REQUIRE_FALSE(files.isEmpty());

    for (const auto& file : files) {
        INFO(file.getFileName());
        CHECK(problemsWith(file).joinIntoString("; ") == "");
    }
}

TEST_CASE("Every smoke project has an expectation file", "[smoke][project]") {
    for (const auto& project : smokeDir().findChildFiles(juce::File::findFiles, true, "*.mgd")) {
        INFO(project.getFileName());
        CHECK(project.getSiblingFile(project.getFileNameWithoutExtension() + ".smoke.json")
                  .existsAsFile());
    }
}

}  // namespace magda
