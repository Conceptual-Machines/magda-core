// The Settings window's pending-change count and Restore defaults (#3162),
// both built on a section writing its edits to a scratch Config.

#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/Config.hpp"
#include "magda/daw/ui/dialogs/settings/SettingsSection.hpp"

using magda::Config;
using magda::SettingsSection;
using magda::SettingsSectionId;

namespace {

/// One edited value, standing in for a page's controls.
class ZoomSection : public SettingsSection {
  public:
    ZoomSection() : SettingsSection(SettingsSectionId::General) {}

    juce::String getTitle() const override {
        return "Zoom";
    }
    std::vector<Tab> getTabs() override {
        return {};
    }
    void load(Config& config) override {
        zoomIn = config.getZoomInSensitivity();
    }
    void writeTo(Config& config) override {
        config.setZoomInSensitivity(zoomIn);
    }

    double zoomIn = 0.0;
};

class LiveSection : public ZoomSection {
  public:
    bool appliesImmediately() const override {
        return true;
    }
};

}  // namespace

TEST_CASE("Config clone is independent and round-trips", "[settings]") {
    auto original = Config::createDetached();
    original->setZoomInSensitivity(42.0);

    auto copy = original->clone();
    CHECK(copy->getZoomInSensitivity() == 42.0);
    CHECK(SettingsSection::countDifferences(original->toVar(), copy->toVar()) == 0);

    copy->setZoomInSensitivity(7.0);
    CHECK(original->getZoomInSensitivity() == 42.0);
    CHECK(SettingsSection::countDifferences(original->toVar(), copy->toVar()) == 1);
}

TEST_CASE("countDifferences compares nested values, not object identity", "[settings]") {
    auto a = Config::createDetached();
    auto b = Config::createDetached();
    std::vector<Config::TrackColourEntry> palette{{0xFF112233, "Red"}, {0xFF445566, "Blue"}};
    a->setTrackColourPalette(palette);
    b->setTrackColourPalette(palette);
    CHECK(SettingsSection::countDifferences(a->toVar(), b->toVar()) == 0);

    palette[1].name = "Teal";
    b->setTrackColourPalette(palette);
    CHECK(SettingsSection::countDifferences(a->toVar(), b->toVar()) == 1);
}

TEST_CASE("A buffered section counts edits against what it loaded", "[settings]") {
    auto& config = Config::getInstance();
    const double saved = config.getZoomInSensitivity();
    config.setZoomInSensitivity(31.0);

    ZoomSection section;
    section.load(config);
    section.captureBaseline();
    CHECK(section.countChanges() == 0);

    section.zoomIn = 12.0;
    CHECK(section.countChanges() == 1);

    section.zoomIn = 31.0;
    CHECK(section.countChanges() == 0);

    config.setZoomInSensitivity(saved);
}

TEST_CASE("Restore defaults shows the defaults as pending edits", "[settings]") {
    auto& config = Config::getInstance();
    const double saved = config.getZoomInSensitivity();
    const double fallback = Config::createDetached()->getZoomInSensitivity();
    config.setZoomInSensitivity(fallback + 5.0);

    ZoomSection section;
    section.load(config);
    section.captureBaseline();
    section.restoreDefaults();
    CHECK(section.zoomIn == fallback);
    CHECK(section.countChanges() == 1);
    CHECK(config.getZoomInSensitivity() == fallback + 5.0);

    config.setZoomInSensitivity(saved);
}

TEST_CASE("A live section never has pending edits", "[settings]") {
    LiveSection section;
    section.load(Config::getInstance());
    section.captureBaseline();
    section.zoomIn = -1.0;
    CHECK(section.countChanges() == 0);
}
