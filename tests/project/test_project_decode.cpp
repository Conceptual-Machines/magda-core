// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <functional>

#include "magda/daw/project/serialization/JsonReader.hpp"
#include "magda/daw/project/serialization/ProjectSerializer.hpp"

// The checked project load (#2323): a field that is missing or of the wrong type fails the
// load with its path, and a file from a newer schema is refused before it is read.

namespace magda {
namespace {

juce::File smokeProject() {
    return juce::File(MAGDA_SMOKE_DIR).getChildFile("session-launcher/session-launcher.mgd");
}

juce::var readProject(const juce::File& file) {
    juce::FileInputStream input(file);
    juce::GZIPDecompressorInputStream gzip(input);
    return juce::JSON::parse(gzip.readEntireStreamAsString());
}

/** @brief A saved project with one edit applied, written where loadAndStage can read it. */
struct EditedProject {
    explicit EditedProject(const std::function<void(juce::var&)>& edit) {
        auto json = readProject(smokeProject());
        REQUIRE(json.isObject());
        edit(json);

        juce::FileOutputStream output(file.getFile());
        juce::GZIPCompressorOutputStream gzip(output, 9);
        gzip.writeText(juce::JSON::toString(json, true), false, false, nullptr);
    }

    bool load() {
        StagedProjectData staged;
        return ProjectSerializer::loadAndStage(file.getFile(), staged);
    }

    juce::TemporaryFile file{".mgd"};
};

juce::DynamicObject& objectAt(const juce::var& value) {
    auto* object = value.getDynamicObject();
    REQUIRE(object != nullptr);
    return *object;
}

}  // namespace

TEST_CASE("The unedited smoke project loads", "[serialization][decode]") {
    EditedProject project([](juce::var&) {});
    INFO(ProjectSerializer::getLastError());
    CHECK(project.load());
    CHECK(ProjectSerializer::getLastLoadFailure() == ProjectLoadFailure::None);
}

TEST_CASE("Staging twice into one object replaces the first load", "[serialization][decode]") {
    StagedProjectData staged;
    REQUIRE(ProjectSerializer::loadAndStage(smokeProject(), staged));
    const auto tracks = staged.tracks.size();
    const auto clips = staged.clips.size();
    REQUIRE(ProjectSerializer::loadAndStage(smokeProject(), staged));
    CHECK(staged.tracks.size() == tracks);
    CHECK(staged.clips.size() == clips);
}

TEST_CASE("A project from a newer schema is refused", "[serialization][decode]") {
    EditedProject project([](juce::var& json) {
        objectAt(json).setProperty("schemaVersion", kProjectSchemaVersion + 1);
    });
    CHECK_FALSE(project.load());
    CHECK(ProjectSerializer::getLastLoadFailure() == ProjectLoadFailure::NewerSchema);
    CHECK(ProjectSerializer::getLastError().contains(juce::String(kProjectSchemaVersion + 1)));
}

TEST_CASE("A schema version that is not an integer is malformed", "[serialization][decode]") {
    EditedProject project(
        [](juce::var& json) { objectAt(json).setProperty("schemaVersion", "3"); });
    CHECK_FALSE(project.load());
    CHECK(ProjectSerializer::getLastLoadFailure() == ProjectLoadFailure::Malformed);
    CHECK(ProjectSerializer::getLastError().startsWith("schemaVersion: expected an integer"));
}

TEST_CASE("A mistyped field fails the load at its path", "[serialization][decode]") {
    EditedProject project([](juce::var& json) {
        const auto clip = (*json["clips"].getArray())[0];
        objectAt(clip["placement"]).setProperty("startBeat", "soon");
    });
    CHECK_FALSE(project.load());
    CHECK(ProjectSerializer::getLastLoadFailure() == ProjectLoadFailure::Malformed);
    CHECK(ProjectSerializer::getLastError() ==
          "clips[0].placement.startBeat: expected a number, found a string");
}

TEST_CASE("A missing required field fails the load at its path", "[serialization][decode]") {
    EditedProject project(
        [](juce::var& json) { objectAt((*json["tracks"].getArray())[0]).removeProperty("id"); });
    CHECK_FALSE(project.load());
    CHECK(ProjectSerializer::getLastError() == "tracks[0].id: missing");
}

TEST_CASE("A missing optional field keeps its default", "[serialization][decode]") {
    EditedProject project([](juce::var& json) {
        objectAt((*json["tracks"].getArray())[0]).removeProperty("frozen");
    });
    INFO(ProjectSerializer::getLastError());
    CHECK(project.load());
}

TEST_CASE("Every issue is collected, the first one reported", "[serialization][decode]") {
    EditedProject project([](juce::var& json) {
        auto& track = objectAt((*json["tracks"].getArray())[0]);
        track.setProperty("muted", "yes");
        track.setProperty("volume", juce::var());
    });
    CHECK_FALSE(project.load());
    CHECK(ProjectSerializer::getLastError() == "tracks[0].volume: missing (and 1 more)");
}

TEST_CASE("Checked reads convert only what JSON's types allow", "[serialization][decode]") {
    int asInt = 0;
    CHECK(ser::decodeValue(juce::var(4.0), asInt));
    CHECK(asInt == 4);
    CHECK_FALSE(ser::decodeValue(juce::var(4.5), asInt));
    CHECK_FALSE(ser::decodeValue(juce::var("4"), asInt));

    bool asBool = false;
    CHECK(ser::decodeValue(juce::var(1), asBool));
    CHECK(asBool);
    CHECK_FALSE(ser::decodeValue(juce::var("true"), asBool));

    double asDouble = 0.0;
    CHECK(ser::decodeValue(juce::var(3), asDouble));
    CHECK_FALSE(ser::decodeValue(juce::var(true), asDouble));

    juce::Colour colour;
    CHECK(ser::decodeValue(juce::var("FF112233"), colour));
    CHECK(colour == juce::Colour(0xFF112233));
    CHECK_FALSE(ser::decodeValue(juce::var("not a colour"), colour));
}

TEST_CASE("Every factory device preset decodes", "[serialization][decode][presets]") {
    const auto presets = juce::File(MAGDA_LEGACY_CORPUS_DIR)
                             .getParentDirectory()
                             .getParentDirectory()
                             .getParentDirectory()
                             .getChildFile("presets/Devices");
    REQUIRE(presets.isDirectory());

    const auto files = presets.findChildFiles(juce::File::findFiles, true, "*.mps");
    REQUIRE_FALSE(files.isEmpty());
    for (const auto& file : files) {
        INFO("preset: " << file.getFullPathName());
        const auto json = juce::JSON::parse(file);
        DeviceInfo device;
        const bool decoded = ProjectSerializer::deserializeDeviceInfo(json["payload"], device);
        INFO("error: " << ProjectSerializer::getLastError());
        CHECK(decoded);
    }
}

}  // namespace magda
