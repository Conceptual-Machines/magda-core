#include <catch2/catch_test_macros.hpp>
#include <catch2/interfaces/catch_interfaces_config.hpp>
#include <catch2/internal/catch_context.hpp>

#include "magda/daw/project/RecoverySession.hpp"

using namespace magda;

namespace {
struct RecoveryFixture {
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("magda-recovery-test-" + juce::Uuid().toString());

    ~RecoveryFixture() {
        root.deleteRecursively();
    }

    RecoveryEntry metadata() const {
        RecoveryEntry entry;
        entry.name = "A recognisable song";
        entry.version = "0.20.0";
        entry.created = juce::Time::getCurrentTime() - juce::RelativeTime::days(10);
        entry.edited = juce::Time::getCurrentTime() - juce::RelativeTime::hours(2);
        entry.saved = juce::Time::getCurrentTime();
        entry.tracks = 4;
        entry.clips = 12;
        return entry;
    }

    void write(RecoverySession& session, const RecoveryEntry& entry) const {
        REQUIRE(session.write(entry, [](const juce::File& file) {
            return file.replaceWithText("recoverable project");
        }));
    }
};
}  // namespace

TEST_CASE("Recovery is offered once after an unclean session", "[project][autosave][2785]") {
    RecoveryFixture fixture;
    {
        RecoverySession crashed(fixture.root, "build A");
        fixture.write(crashed, fixture.metadata());
    }
    {
        RecoverySession next(fixture.root, "build A");
        const auto entry = next.startupCandidate();
        REQUIRE(entry.snapshot.existsAsFile());
        REQUIRE(entry.name == "A recognisable song");
        REQUIRE(entry.version == "0.20.0");
        REQUIRE(entry.tracks == 4);
        REQUIRE(entry.clips == 12);
        REQUIRE(entry.created < entry.edited);
        REQUIRE(entry.edited < entry.saved);
        REQUIRE(next.markOffered(entry));
        REQUIRE_FALSE(next.markOffered(entry));
        REQUIRE_FALSE(next.startupCandidate().snapshot.existsAsFile());
        REQUIRE(next.entries().size() == 1);  // Later/Cancel keeps it browsable.
    }
    RecoverySession third(fixture.root, "build A");
    REQUIRE_FALSE(third.startupCandidate().snapshot.existsAsFile());
    REQUIRE(third.entries().size() == 1);
}

TEST_CASE("Clean quit does not offer older unclaimed recovery", "[project][autosave][2785]") {
    RecoveryFixture fixture;
    juce::File oldSnapshot;
    {
        RecoverySession old(fixture.root, "build A");
        fixture.write(old, fixture.metadata());
        oldSnapshot = old.snapshot();
    }
    {
        RecoverySession clean(fixture.root, "build A");
        fixture.write(clean, fixture.metadata());
        const auto abandoned = clean.snapshot();
        clean.finish();
        REQUIRE_FALSE(abandoned.existsAsFile());
        REQUIRE(oldSnapshot.existsAsFile());
    }
    RecoverySession next(fixture.root, "build A");
    REQUIRE_FALSE(next.startupCandidate().snapshot.existsAsFile());
    REQUIRE(next.entries().size() == 1);
}

TEST_CASE("Recovery slots isolate builds and live sessions", "[project][autosave][2785]") {
    RecoveryFixture fixture;
    RecoverySession live(fixture.root, "build A");
    fixture.write(live, fixture.metadata());
    RecoverySession concurrent(fixture.root, "build A");
    REQUIRE_FALSE(concurrent.startupCandidate().snapshot.existsAsFile());
    REQUIRE(concurrent.entries().empty());
    const auto active = concurrent.entries(true);
    REQUIRE(active.size() == 1);
    REQUIRE_FALSE(concurrent.discard(active.front()));
    REQUIRE_FALSE(concurrent.adopt(active.front()));
    REQUIRE_FALSE(concurrent.markOffered(active.front()));
    {
        RecoverySession other(fixture.root, "build B");
        fixture.write(other, fixture.metadata());
        REQUIRE(other.snapshot() != live.snapshot());
    }
    RecoverySession next(fixture.root, "build A");
    REQUIRE_FALSE(next.startupCandidate().snapshot.existsAsFile());
    REQUIRE(next.entries().size() == 1);
    REQUIRE(next.entries().front().build == "build B");
}

TEST_CASE("A new session without an autosave cannot offer an older slot",
          "[project][autosave][2785]") {
    RecoveryFixture fixture;
    {
        RecoverySession old(fixture.root, "build A");
        fixture.write(old, fixture.metadata());
    }
    { RecoverySession empty(fixture.root, "build A"); }
    RecoverySession next(fixture.root, "build A");
    REQUIRE_FALSE(next.startupCandidate().snapshot.existsAsFile());
    REQUIRE(next.entries().size() == 1);
}

TEST_CASE("Failed autosave preserves the last complete snapshot and metadata",
          "[project][autosave][2785]") {
    RecoveryFixture fixture;
    RecoverySession session(fixture.root, "build A");
    fixture.write(session, fixture.metadata());
    const auto original = session.snapshot();
    auto changed = fixture.metadata();
    changed.name = "An incomplete write";
    REQUIRE_FALSE(session.write(changed, [](const juce::File& file) {
        file.replaceWithText("incomplete");
        return false;
    }));
    REQUIRE(session.snapshot() == original);
    REQUIRE(original.loadFileAsString() == "recoverable project");
    REQUIRE(session.entries(true).front().name == "A recognisable song");
}

TEST_CASE("Adopting a recovery survives an immediate second crash", "[project][autosave][2785]") {
    RecoveryFixture fixture;
    juce::File original;
    {
        RecoverySession first(fixture.root, "build A");
        fixture.write(first, fixture.metadata());
        original = first.snapshot();
    }
    {
        RecoverySession second(fixture.root, "build A");
        const auto entry = second.startupCandidate();
        REQUIRE(second.markOffered(entry));
        REQUIRE(second.adopt(entry));
        REQUIRE_FALSE(original.existsAsFile());
        REQUIRE(second.snapshot().loadFileAsString() == "recoverable project");
        REQUIRE(second.snapshot() != original);
    }
    RecoverySession third(fixture.root, "build A");
    REQUIRE(third.startupCandidate().snapshot.existsAsFile());
    REQUIRE(third.startupCandidate().name == "A recognisable song");
}

TEST_CASE("A stale selection cannot discard a transferred recovery", "[project][autosave][2785]") {
    RecoveryFixture fixture;
    {
        RecoverySession crashed(fixture.root, "build A");
        fixture.write(crashed, fixture.metadata());
    }
    RecoverySession reader(fixture.root, "build A");
    const auto entry = reader.startupCandidate();
    REQUIRE(reader.adopt(entry));
    REQUIRE_FALSE(reader.discard(entry));
    REQUIRE_FALSE(reader.adopt(entry));
    REQUIRE(reader.snapshot().existsAsFile());
}

TEST_CASE("Saved projects recover only newer snapshots from their build",
          "[project][autosave][2785]") {
    RecoveryFixture fixture;
    fixture.root.createDirectory();
    const auto project = fixture.root.getChildFile("Song.mgd");
    REQUIRE(project.replaceWithText("saved"));
    REQUIRE(project.setLastModificationTime(juce::Time::getCurrentTime() -
                                            juce::RelativeTime::hours(1)));
    auto info = fixture.metadata();
    info.originalFile = project.getFullPathName();
    {
        RecoverySession crashed(fixture.root, "build A");
        fixture.write(crashed, info);
    }
    RecoverySession other(fixture.root, "build B");
    REQUIRE_FALSE(other.projectCandidate(project).snapshot.existsAsFile());
    RecoverySession next(fixture.root, "build A");
    REQUIRE_FALSE(next.startupCandidate().snapshot.existsAsFile());
    const auto entry = next.projectCandidate(project);
    REQUIRE(entry.snapshot.existsAsFile());
    REQUIRE(next.markOffered(entry));
    REQUIRE_FALSE(next.projectCandidate(project).snapshot.existsAsFile());
    REQUIRE(next.projectCandidate(project, true).snapshot == entry.snapshot);
    REQUIRE(entry.snapshot.existsAsFile());
}

TEST_CASE("Clean shutdown retires the session in a copied data directory",
          "[project][autosave][2785]") {
    RecoveryFixture fixture;
    const auto source = fixture.root.getChildFile("source");
    const auto destination = fixture.root.getChildFile("destination");
    {
        RecoverySession session(source, "build A");
        fixture.write(session, fixture.metadata());
        REQUIRE(source.copyDirectoryTo(destination));
        session.finish(destination);
    }
    RecoverySession next(destination, "build A");
    REQUIRE_FALSE(next.startupCandidate().snapshot.existsAsFile());
    REQUIRE(next.entries().empty());
}

TEST_CASE("Recovery process helper", "[.recovery-process-helper]") {
    const juce::String name(
        static_cast<std::string>(Catch::getCurrentContext().getConfig()->name()));
    if (!juce::File::isAbsolutePath(name))
        SKIP("Only run as a child of the recovery process test");
    const juce::File root(name);
    if (!root.isDirectory() || !root.getFileName().startsWith("magda-recovery-test-"))
        SKIP("Only run as a child of the recovery process test");
    RecoverySession session(root, "build A");
    RecoveryEntry entry;
    entry.name = "Killed process project";
    entry.saved = juce::Time::getCurrentTime();
    REQUIRE(session.write(entry, [](const juce::File& file) {
        return file.replaceWithText("snapshot from another process");
    }));
    REQUIRE(root.getChildFile("ready").replaceWithText("ready"));
    // The parent kills this process. Bound the wait if its test fails first.
    juce::Thread::sleep(15000);
}

TEST_CASE("Killing another process releases its recovery slot and offers it once",
          "[project][autosave][2785]") {
    RecoveryFixture fixture;
    REQUIRE(fixture.root.createDirectory());
    juce::ChildProcess child;
    REQUIRE(child.start(juce::StringArray{
        juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName(),
        "[.recovery-process-helper]", "--name", fixture.root.getFullPathName()}));
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    while (!fixture.root.getChildFile("ready").existsAsFile() && child.isRunning() &&
           juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::Thread::sleep(10);
    REQUIRE(fixture.root.getChildFile("ready").existsAsFile());
    RecoverySession observer(fixture.root, "build B");
    REQUIRE(observer.entries().empty());
    const auto live = observer.entries(true);
    REQUIRE(live.size() == 1);
    REQUIRE_FALSE(observer.adopt(live.front()));
    REQUIRE_FALSE(observer.discard(live.front()));
    REQUIRE(child.kill());
    REQUIRE(child.waitForProcessToFinish(5000));
    RecoverySession next(fixture.root, "build A");
    const auto entry = next.startupCandidate();
    REQUIRE(entry.name == "Killed process project");
    REQUIRE(entry.snapshot.loadFileAsString() == "snapshot from another process");
    REQUIRE(next.markOffered(entry));
    REQUIRE_FALSE(next.startupCandidate().snapshot.existsAsFile());
    REQUIRE(next.entries().size() == 1);
}
