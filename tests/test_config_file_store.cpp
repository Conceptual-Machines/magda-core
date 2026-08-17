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

    SECTION("an empty file, the other shape a truncated write leaves") {
        ScratchDir scratch;
        scratch.configFile().replaceWithText("");

        const auto result = store::read(scratch.configFile(), kStamp);

        REQUIRE(result.status == Status::Quarantined);
        REQUIRE_FALSE(scratch.configFile().existsAsFile());
        REQUIRE(result.quarantinedAs.existsAsFile());
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

TEST_CASE("Writing the settings file replaces it", "[config][config-store]") {
    ScratchDir scratch;

    REQUIRE(store::write(scratch.configFile(), R"({"generation": 1})", false));
    REQUIRE(scratch.configFile().loadFileAsString() == R"({"generation": 1})");

    REQUIRE(store::write(scratch.configFile(), R"({"generation": 2})", false));
    REQUIRE(scratch.configFile().loadFileAsString() == R"({"generation": 2})");
}

TEST_CASE("The backup holds the state at session start, not the last write",
          "[config][config-store]") {
    // The point of the flag. A backup refreshed on every write is worthless
    // exactly when it is needed: one bad save rolls it forward over the good
    // copy. That is how a real set of API keys was lost -- the wrecked config
    // was saved twice in the same second, and the second save's backup was the
    // first save's damage.
    ScratchDir scratch;
    const auto backup = scratch.configFile().getSiblingFile("config.json.bak");

    // Settings as the session found them.
    scratch.configFile().replaceWithText(R"({"keys": "precious"})");

    // First save of the session takes the backup...
    REQUIRE(store::write(scratch.configFile(), R"({"keys": "precious", "edited": 1})", true));
    REQUIRE(backup.existsAsFile());
    REQUIRE(backup.loadFileAsString() == R"({"keys": "precious"})");

    // ...and no later save in the same session may touch it, however wrong it is.
    REQUIRE(store::write(scratch.configFile(), R"({"wiped": true})", false));
    REQUIRE(store::write(scratch.configFile(), R"({"wiped": true})", false));

    REQUIRE(scratch.configFile().loadFileAsString() == R"({"wiped": true})");
    REQUIRE(backup.loadFileAsString() == R"({"keys": "precious"})");
}

TEST_CASE("A first write with nothing on disk leaves no backup", "[config][config-store]") {
    ScratchDir scratch;

    REQUIRE(store::write(scratch.configFile(), R"({"generation": 1})", true));
    REQUIRE_FALSE(scratch.configFile().getSiblingFile("config.json.bak").existsAsFile());
}

TEST_CASE("Writing the settings file leaves no stray temporaries", "[config][config-store]") {
    ScratchDir scratch;

    REQUIRE(store::write(scratch.configFile(), R"({"a": 1})", true));
    REQUIRE(store::write(scratch.configFile(), R"({"a": 2})", false));
    REQUIRE(store::write(scratch.configFile(), R"({"a": 3})", false));

    // Exactly config.json, nothing half-written left over. No .bak: the first
    // write had nothing to copy.
    const auto files = filesMatching(scratch.dir(), "config.json");
    REQUIRE(files.size() == 1);
}

TEST_CASE("A written settings file reads back", "[config][config-store]") {
    ScratchDir scratch;

    REQUIRE(store::write(scratch.configFile(), R"({"uiScale": 2.0})", false));
    const auto result = store::read(scratch.configFile(), kStamp);

    REQUIRE(result.status == Status::Loaded);
    auto* obj = result.parsed.getDynamicObject();
    REQUIRE(obj != nullptr);
    REQUIRE(static_cast<double>(obj->getProperty("uiScale")) == 2.0);
}
