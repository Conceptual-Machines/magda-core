#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/project/ProjectManager.hpp"

// The gap between one project's runtime ending and the next one's model
// arriving (#2576): every path that replaces the project declares it, and
// declares it while the outgoing project is still the one in the model.

namespace {

using magda::ProjectManager;
using magda::TrackManager;

/// What each listener callback was, in the order it arrived. The order is the
/// point: a teardown after the restore would be a listener tearing down the
/// project that just arrived.
class LifecycleLog final : public magda::ProjectManagerListener {
  public:
    LifecycleLog() {
        ProjectManager::getInstance().addListener(this);
    }

    ~LifecycleLog() override {
        ProjectManager::getInstance().removeListener(this);
    }

    LifecycleLog(const LifecycleLog&) = delete;
    LifecycleLog& operator=(const LifecycleLog&) = delete;

    void projectTeardown() override {
        events.emplace_back("teardown");
        tracksAtTeardown.push_back(
            static_cast<int>(TrackManager::getInstance().getTracks().size()));
    }

    void projectOpened(const magda::ProjectInfo&) override {
        events.emplace_back("opened");
    }

    void projectClosed() override {
        events.emplace_back("closed");
    }

    std::vector<juce::String> events;
    std::vector<int> tracksAtTeardown;
};

juce::File tempProjectFile() {
    auto envTmp = juce::SystemStats::getEnvironmentVariable("TMPDIR", {});
    auto root = envTmp.isNotEmpty() ? juce::File(envTmp)
                                    : juce::File::getSpecialLocation(juce::File::tempDirectory);
    root.createDirectory();
    return root.getNonexistentChildFile("teardown", ".mgd");
}

/// saveProjectAs wraps the file in a directory named after the project.
juce::File wrappedPath(const juce::File& file) {
    const auto name = file.getFileNameWithoutExtension();
    return file.getParentDirectory().getChildFile(name).getChildFile(file.getFileName());
}

struct ProjectFixture {
    ProjectFixture() {
        TrackManager::getInstance().clearAllTracks();
    }

    ~ProjectFixture() {
        for (const auto& dir : dirs)
            dir.deleteRecursively();
        for (const auto& file : files)
            file.deleteFile();

        TrackManager::getInstance().clearAllTracks();
    }

    ProjectFixture(const ProjectFixture&) = delete;
    ProjectFixture& operator=(const ProjectFixture&) = delete;

    juce::File newTempProject() {
        auto file = tempProjectFile();
        files.push_back(file);
        dirs.push_back(file.getParentDirectory().getChildFile(file.getFileNameWithoutExtension()));
        return file;
    }

    std::vector<juce::File> files;
    std::vector<juce::File> dirs;
};

}  // namespace

TEST_CASE("A new project declares the previous one's teardown", "[project][teardown]") {
    ProjectFixture fixture;
    auto& projectManager = ProjectManager::getInstance();

    projectManager.newProject();
    TrackManager::getInstance().createTrack("Instrument");

    LifecycleLog log;
    REQUIRE(projectManager.newProject());

    REQUIRE(log.events == std::vector<juce::String>{"teardown", "opened"});
    REQUIRE(log.tracksAtTeardown == std::vector<int>{1});
}

TEST_CASE("Closing a project declares its teardown", "[project][teardown]") {
    ProjectFixture fixture;
    auto& projectManager = ProjectManager::getInstance();

    projectManager.newProject();
    TrackManager::getInstance().createTrack("Instrument");

    LifecycleLog log;
    REQUIRE(projectManager.closeProject());

    REQUIRE(log.events == std::vector<juce::String>{"teardown", "closed"});
    REQUIRE(log.tracksAtTeardown == std::vector<int>{1});
}

TEST_CASE("Loading declares the teardown before the restore", "[project][teardown]") {
    ProjectFixture fixture;
    auto& projectManager = ProjectManager::getInstance();

    projectManager.newProject();
    TrackManager::getInstance().createTrack("Saved");

    const auto file = fixture.newTempProject();
    REQUIRE(projectManager.saveProjectAs(file));

    // A second project, so the load has something of its own to tear down.
    projectManager.newProject();
    TrackManager::getInstance().createTrack("Outgoing");
    TrackManager::getInstance().createTrack("Also outgoing");

    LifecycleLog log;
    REQUIRE(projectManager.loadProject(wrappedPath(file)));

    REQUIRE(log.events == std::vector<juce::String>{"teardown", "opened"});

    // Two, not zero: the outgoing project is still the model when the teardown
    // is declared, which is what lets a listener walk what it has to drop.
    REQUIRE(log.tracksAtTeardown == std::vector<int>{2});
}

TEST_CASE("A load that never stages declares no teardown", "[project][teardown]") {
    ProjectFixture fixture;
    auto& projectManager = ProjectManager::getInstance();

    projectManager.newProject();
    TrackManager::getInstance().createTrack("Instrument");

    LifecycleLog log;
    REQUIRE_FALSE(projectManager.loadProject(juce::File("/nonexistent/project.mgd")));

    REQUIRE(log.events.empty());
    REQUIRE(TrackManager::getInstance().getTracks().size() == 1);
}
