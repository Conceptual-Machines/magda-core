// The settings file is written atomically (#2104).
//
// Config::save() used to call replaceWithText() on config.json, which truncates
// the file before the new contents land. A crash or a yanked volume in that
// window leaves a half-written settings file and every preference in it is
// gone. These cover the replacement, and that no debris is left beside it.

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ConfigFileStore.hpp"

namespace store = magda::ConfigFileStore;

namespace {

// A scratch directory that cleans up after itself, so these tests never go
// near the real config.json.
class ScratchDir {
  public:
    ScratchDir() {
        dir_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("magda-config-store-test")
                   .getNonexistentSibling();
        dir_.createDirectory();
    }
    ~ScratchDir() {
        dir_.deleteRecursively();
    }

    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;
    ScratchDir(ScratchDir&&) = delete;
    ScratchDir& operator=(ScratchDir&&) = delete;

    juce::File configFile() const {
        return dir_.getChildFile("config.json");
    }
    juce::File dir() const {
        return dir_;
    }

  private:
    juce::File dir_;
};

}  // namespace

TEST_CASE("Writing the settings file creates it", "[config][config-store]") {
    ScratchDir scratch;

    REQUIRE(store::write(scratch.configFile(), R"({"uiScale": 1.5})"));
    REQUIRE(scratch.configFile().existsAsFile());
    REQUIRE(scratch.configFile().loadFileAsString() == R"({"uiScale": 1.5})");
}

TEST_CASE("Writing the settings file replaces it wholly", "[config][config-store]") {
    ScratchDir scratch;

    REQUIRE(store::write(scratch.configFile(), R"({"generation": 1})"));
    REQUIRE(store::write(scratch.configFile(), R"({"generation": 2})"));

    // Wholly replaced, not appended to or partially overwritten -- a shorter
    // second write must not leave a tail of the first behind.
    REQUIRE(scratch.configFile().loadFileAsString() == R"({"generation": 2})");
}

TEST_CASE("Writing a shorter settings file leaves no tail of the longer one",
          "[config][config-store]") {
    ScratchDir scratch;

    REQUIRE(store::write(scratch.configFile(), R"({"a": 1, "b": 2, "c": 3, "d": 4})"));
    REQUIRE(store::write(scratch.configFile(), R"({"a": 1})"));

    REQUIRE(scratch.configFile().loadFileAsString() == R"({"a": 1})");
    REQUIRE(scratch.configFile().getSize() == juce::String(R"({"a": 1})").length());
}

TEST_CASE("Writing the settings file leaves no temporaries behind", "[config][config-store]") {
    ScratchDir scratch;

    REQUIRE(store::write(scratch.configFile(), R"({"a": 1})"));
    REQUIRE(store::write(scratch.configFile(), R"({"a": 2})"));
    REQUIRE(store::write(scratch.configFile(), R"({"a": 3})"));

    // The temporary the write goes through must not survive it: the settings
    // file is the only thing in the directory.
    const auto files = scratch.dir().findChildFiles(juce::File::findFilesAndDirectories, false);
    REQUIRE(files.size() == 1);
    REQUIRE(files[0] == scratch.configFile());
}

TEST_CASE("Writing the settings file creates its directory", "[config][config-store]") {
    ScratchDir scratch;
    const auto nested = scratch.dir().getChildFile("nested").getChildFile("config.json");

    REQUIRE(store::write(nested, R"({"a": 1})"));
    REQUIRE(nested.existsAsFile());
}

TEST_CASE("A written settings file parses back", "[config][config-store]") {
    ScratchDir scratch;

    REQUIRE(store::write(scratch.configFile(), R"({"uiScale": 2.0})"));

    juce::var parsed;
    REQUIRE(juce::JSON::parse(scratch.configFile().loadFileAsString(), parsed).wasOk());
    auto* obj = parsed.getDynamicObject();
    REQUIRE(obj != nullptr);
    REQUIRE(static_cast<double>(obj->getProperty("uiScale")) == 2.0);
}
