// The settings file's read/write rules (#2104). A config.json that failed to
// parse used to leave Config at its defaults with nothing recording the
// failure, so the next save wrote those defaults over the user's file and every
// preference -- including API keys -- was lost. These are the rules that stop
// that: an unreadable file is preserved rather than overwritten, and writes are
// atomic so an interrupted one cannot truncate the live file.

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ConfigFileStore.hpp"

namespace store = magda::ConfigFileStore;
using Status = store::ReadStatus;

namespace {

const juce::String kStamp = "20260817-120000";

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

// Files the scratch dir holds whose name starts with `prefix`.
juce::Array<juce::File> filesMatching(const juce::File& dir, const juce::String& prefix) {
    juce::Array<juce::File> out;
    for (const auto& f : dir.findChildFiles(juce::File::findFiles, false))
        if (f.getFileName().startsWith(prefix))
            out.add(f);
    return out;
}

}  // namespace

TEST_CASE("A missing settings file is a first run, not a failure", "[config][config-store]") {
    ScratchDir scratch;

    const auto result = store::read(scratch.configFile(), kStamp);

    REQUIRE(result.status == Status::NoFile);
    REQUIRE(store::mayWrite(result.status));
}

TEST_CASE("A readable settings file parses", "[config][config-store]") {
    ScratchDir scratch;
    scratch.configFile().replaceWithText(R"({"uiScale": 1.5, "theme": "dark"})");

    const auto result = store::read(scratch.configFile(), kStamp);

    REQUIRE(result.status == Status::Loaded);
    REQUIRE(store::mayWrite(result.status));
    auto* obj = result.parsed.getDynamicObject();
    REQUIRE(obj != nullptr);
    REQUIRE(static_cast<double>(obj->getProperty("uiScale")) == 1.5);
    REQUIRE(obj->getProperty("theme").toString() == "dark");
    // A file that read cleanly stays exactly where it is.
    REQUIRE(scratch.configFile().existsAsFile());
}

TEST_CASE("An unreadable settings file is preserved, not left to be overwritten",
          "[config][config-store]") {
    SECTION("truncated JSON, the shape an interrupted write leaves behind") {
        ScratchDir scratch;
        const juce::String original = R"({"uiScale": 1.5, "ai": {"credenti)";
        scratch.configFile().replaceWithText(original);

        const auto result = store::read(scratch.configFile(), kStamp);

        REQUIRE(result.status == Status::Quarantined);
        // The original is out of the way, so a later save cannot destroy it...
        REQUIRE_FALSE(scratch.configFile().existsAsFile());
        // ...and its contents survive intact.
        REQUIRE(result.quarantinedAs.existsAsFile());
        REQUIRE(result.quarantinedAs.loadFileAsString() == original);
        REQUIRE(result.quarantinedAs.getFileName().contains(kStamp));
        // With the original safely aside, saving a fresh file is allowed.
        REQUIRE(store::mayWrite(result.status));
    }

    SECTION("valid JSON that is not an object") {
        ScratchDir scratch;
        scratch.configFile().replaceWithText("[1, 2, 3]");

        const auto result = store::read(scratch.configFile(), kStamp);

        REQUIRE(result.status == Status::Quarantined);
        REQUIRE(result.quarantinedAs.loadFileAsString() == "[1, 2, 3]");
    }

    SECTION("the message names the file and the problem") {
        ScratchDir scratch;
        scratch.configFile().replaceWithText("{oh no");

        const auto result = store::read(scratch.configFile(), kStamp);

        REQUIRE(result.message.contains("config.json"));
        REQUIRE(result.message.isNotEmpty());
    }
}

TEST_CASE("A second unreadable file does not clobber the first quarantine",
          "[config][config-store]") {
    ScratchDir scratch;

    scratch.configFile().replaceWithText("{first");
    const auto first = store::read(scratch.configFile(), kStamp);

    scratch.configFile().replaceWithText("{second");
    const auto second = store::read(scratch.configFile(), kStamp);

    REQUIRE(first.status == Status::Quarantined);
    REQUIRE(second.status == Status::Quarantined);
    REQUIRE(first.quarantinedAs != second.quarantinedAs);
    REQUIRE(first.quarantinedAs.loadFileAsString() == "{first");
    REQUIRE(second.quarantinedAs.loadFileAsString() == "{second");
}

TEST_CASE("A file that could not be read and could not be moved is never written over",
          "[config][config-store]") {
    // The one case where saving must be refused: the file still holds settings
    // this build failed to read.
    REQUIRE_FALSE(store::mayWrite(Status::Unreadable));

    REQUIRE(store::mayWrite(Status::Loaded));
    REQUIRE(store::mayWrite(Status::NoFile));
    REQUIRE(store::mayWrite(Status::Quarantined));
}

TEST_CASE("Writing the settings file keeps the previous contents", "[config][config-store]") {
    ScratchDir scratch;

    REQUIRE(store::write(scratch.configFile(), R"({"generation": 1})"));
    REQUIRE(scratch.configFile().loadFileAsString() == R"({"generation": 1})");
    // Nothing to back up on the first write.
    REQUIRE_FALSE(scratch.configFile().getSiblingFile("config.json.bak").existsAsFile());

    REQUIRE(store::write(scratch.configFile(), R"({"generation": 2})"));
    REQUIRE(scratch.configFile().loadFileAsString() == R"({"generation": 2})");

    const auto backup = scratch.configFile().getSiblingFile("config.json.bak");
    REQUIRE(backup.existsAsFile());
    REQUIRE(backup.loadFileAsString() == R"({"generation": 1})");
}

TEST_CASE("Writing the settings file leaves no stray temporaries", "[config][config-store]") {
    ScratchDir scratch;

    REQUIRE(store::write(scratch.configFile(), R"({"a": 1})"));
    REQUIRE(store::write(scratch.configFile(), R"({"a": 2})"));

    // Exactly config.json and config.json.bak, nothing half-written left over.
    const auto files = filesMatching(scratch.dir(), "config.json");
    REQUIRE(files.size() == 2);
}

TEST_CASE("A written settings file reads back", "[config][config-store]") {
    ScratchDir scratch;

    REQUIRE(store::write(scratch.configFile(), R"({"uiScale": 2.0})"));
    const auto result = store::read(scratch.configFile(), kStamp);

    REQUIRE(result.status == Status::Loaded);
    auto* obj = result.parsed.getDynamicObject();
    REQUIRE(obj != nullptr);
    REQUIRE(static_cast<double>(obj->getProperty("uiScale")) == 2.0);
}
