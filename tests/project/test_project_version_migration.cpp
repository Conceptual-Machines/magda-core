#include <catch2/catch_test_macros.hpp>

#include "magda/daw/project/ProjectVersionMigration.hpp"

TEST_CASE("Only projects saved in v0 need a separate v1 copy", "[project][v1-migration]") {
    using magda::project_version::needsV1Copy;
    CHECK(needsV1Copy("0.20.0", "1.0.0"));
    CHECK(needsV1Copy("v0.20.0-rc2-15-g1391223", "v1.0.0-rc1"));
    CHECK(needsV1Copy("0.20.0", "2.0.0"));
    CHECK_FALSE(needsV1Copy("0.20.0", "0.20.1"));
    CHECK_FALSE(needsV1Copy("1.0.0", "1.0.1"));
    CHECK_FALSE(needsV1Copy("2.0.0", "1.0.0"));
    CHECK_FALSE(needsV1Copy("", "1.0.0"));
    CHECK_FALSE(needsV1Copy("unknown", "1.0.0"));
    CHECK_FALSE(needsV1Copy(".20.0", "1.0.0"));
}

TEST_CASE("A v1 copy cannot reuse the v0 project folder", "[project][v1-migration]") {
    using magda::project_version::isSeparateProject;
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const auto source = root.getChildFile("Song/Song.mgd");
    CHECK_FALSE(isSeparateProject(source, source));
    CHECK_FALSE(isSeparateProject(source, root.getChildFile("Song/Renamed.mgd")));
    CHECK_FALSE(isSeparateProject(source, root.getChildFile("Song/Nested/Nested.mgd")));
    CHECK(isSeparateProject(source, root.getChildFile("Song (v1)/Song (v1).mgd")));
    CHECK(isSeparateProject(source, root.getChildFile("Elsewhere/New/New.mgd")));
    const auto loose = root.getChildFile("Loose.mgd");
    CHECK_FALSE(isSeparateProject(loose, loose));
    CHECK(isSeparateProject(loose, root.getChildFile("Loose (v1)/Loose (v1).mgd")));
    CHECK_FALSE(isSeparateProject(loose, root.getChildFile("Loose_Media/Nested/Nested.mgd")));
}

TEST_CASE("Migration names a new sibling project without reusing an existing copy",
          "[project][v1-migration]") {
    using magda::project_version::newProjectFileFor;
    juce::TemporaryFile temporary;
    const auto root = temporary.getFile();
    REQUIRE(root.createDirectory());
    const auto source = root.getChildFile("Song/Song.mgd");
    REQUIRE(source.getParentDirectory().createDirectory());
    const auto first = newProjectFileFor(source, " (v1)");
    CHECK(first == root.getChildFile("Song (v1).mgd"));
    CHECK(newProjectFileFor(root.getChildFile("Loose.mgd"), " (v1)") ==
          root.getChildFile("Loose (v1).mgd"));
    CHECK(newProjectFileFor(source, " (MAGDA Engine)") ==
          root.getChildFile("Song (MAGDA Engine).mgd"));
    REQUIRE(root.getChildFile("Song (v1)").createDirectory());
    const auto second = newProjectFileFor(source, " (v1)");
    CHECK(second != first);
    CHECK(second.getParentDirectory() == root);
    CHECK_FALSE(root.getChildFile(second.getFileNameWithoutExtension()).exists());
    CHECK(newProjectFileFor({}, " (v1)") == juce::File{});
    REQUIRE(root.deleteRecursively());
}
