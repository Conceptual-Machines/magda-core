// Tests for restoring a persisted panel tab layout (#2103). The panel's tab
// order and front tab are saved as stable content-type ids, so the restore has
// to survive a config written by a build whose tab set differs from this one.
// Pure state manipulation, no Config and no UI.

#include <catch2/catch_test_macros.hpp>
#include <set>

#include "magda/daw/ui/panels/state/PanelController.hpp"

using magda::daw::ui::allContentTypes;
using magda::daw::ui::applyPersistedActiveTab;
using magda::daw::ui::applyPersistedTabOrder;
using magda::daw::ui::contentTypeFromId;
using magda::daw::ui::getContentTypeId;
using magda::daw::ui::PanelContentType;
using magda::daw::ui::PanelState;

namespace {

// The shipped left panel: Plugins then Samples, Plugins in front.
PanelState leftPanelDefaults() {
    PanelState panel;
    panel.location = magda::daw::ui::PanelLocation::Left;
    panel.tabs = {PanelContentType::PluginBrowser, PanelContentType::MediaExplorer};
    panel.activeTabIndex = 0;
    return panel;
}

// The shipped bottom panel: Empty in front, then the clip editors. Its front
// tab is deliberately never persisted, so a reorder must not shift it.
PanelState bottomPanelDefaults() {
    PanelState panel;
    panel.location = magda::daw::ui::PanelLocation::Bottom;
    panel.tabs = {PanelContentType::Empty, PanelContentType::PianoRoll,
                  PanelContentType::DrumGridClipView, PanelContentType::TrackChain};
    panel.activeTabIndex = 0;
    return panel;
}

std::vector<std::string> idsOf(const PanelState& panel) {
    std::vector<std::string> ids;
    for (auto type : panel.tabs)
        ids.push_back(getContentTypeId(type).toStdString());
    return ids;
}

}  // namespace

TEST_CASE("Every content type has a unique id that round-trips", "[panels]") {
    // The ids go to config.json, so a type that serialises to nothing, or two
    // that collide, would silently lose a tab's saved position.
    std::set<std::string> seen;

    for (auto type : allContentTypes()) {
        const auto id = getContentTypeId(type);
        INFO("content type id: " << id);
        CHECK(id.isNotEmpty());
        CHECK(seen.insert(id.toStdString()).second);
        CHECK(contentTypeFromId(id) == type);
    }
}

TEST_CASE("An id no build ever wrote resolves to nothing", "[panels]") {
    CHECK(contentTypeFromId("") == std::nullopt);
    CHECK(contentTypeFromId("somethingFromAnotherBuild") == std::nullopt);
}

TEST_CASE("An empty saved layout leaves the panel defaults alone", "[panels]") {
    auto panel = leftPanelDefaults();

    applyPersistedTabOrder(panel, {});
    applyPersistedActiveTab(panel, "");

    CHECK(idsOf(panel) == std::vector<std::string>{"pluginBrowser", "mediaExplorer"});
    CHECK(panel.activeTabIndex == 0);
}

TEST_CASE("A saved order reorders the panel's tabs", "[panels]") {
    auto panel = leftPanelDefaults();

    applyPersistedTabOrder(panel, {"mediaExplorer", "pluginBrowser"});

    CHECK(idsOf(panel) == std::vector<std::string>{"mediaExplorer", "pluginBrowser"});
}

TEST_CASE("A saved front tab is brought forward", "[panels]") {
    auto panel = leftPanelDefaults();

    applyPersistedActiveTab(panel, "mediaExplorer");

    CHECK(panel.getActiveContentType() == PanelContentType::MediaExplorer);
}

TEST_CASE("The front tab follows the tab it names, not the index it had", "[panels]") {
    auto panel = leftPanelDefaults();

    // Saved with Samples in front, and the order reversed since. The front tab
    // has to end up on Samples, which is now index 0, not on whatever index 1
    // holds.
    applyPersistedTabOrder(panel, {"mediaExplorer", "pluginBrowser"});
    applyPersistedActiveTab(panel, "mediaExplorer");

    CHECK(panel.activeTabIndex == 0);
    CHECK(panel.getActiveContentType() == PanelContentType::MediaExplorer);
}

TEST_CASE("Reordering carries the front tab with it, not the index it sat at", "[panels]") {
    auto panel = leftPanelDefaults();
    panel.activeTabIndex = 1;  // Samples in front

    // No saved front tab to fall back on, so the reorder alone has to keep
    // Samples in front rather than leaving index 1 pointing at Plugins.
    applyPersistedTabOrder(panel, {"mediaExplorer", "pluginBrowser"});

    CHECK(panel.getActiveContentType() == PanelContentType::MediaExplorer);
    CHECK(panel.activeTabIndex == 0);
}

TEST_CASE("A reordered bottom panel still opens on Empty", "[panels]") {
    auto panel = bottomPanelDefaults();

    // The bottom panel's front tab is never persisted, so applyPersistedTabOrder
    // is the only thing that runs on it. Moving Empty off index 0 must not
    // leave an editor in front with nothing selected behind it.
    applyPersistedTabOrder(panel, {"trackChain", "pianoRoll", "drumGridClipView", "empty"});

    CHECK(idsOf(panel) ==
          std::vector<std::string>{"trackChain", "pianoRoll", "drumGridClipView", "empty"});
    CHECK(panel.getActiveContentType() == PanelContentType::Empty);
    CHECK(panel.activeTabIndex == 3);
}

TEST_CASE("An unknown saved id is ignored rather than opening an empty panel", "[panels]") {
    auto panel = leftPanelDefaults();

    // A tab from a build this one does not have, plus a tab that exists but
    // does not belong to this panel.
    applyPersistedTabOrder(panel, {"somethingFromAnotherBuild", "pianoRoll", "mediaExplorer"});
    applyPersistedActiveTab(panel, "somethingFromAnotherBuild");

    // Only the known, in-panel tab was honoured for the order. With no usable
    // front tab to restore, the default one stays in front -- at its new
    // position, not at the index it used to hold.
    CHECK(idsOf(panel) == std::vector<std::string>{"mediaExplorer", "pluginBrowser"});
    CHECK(panel.getActiveContentType() == PanelContentType::PluginBrowser);
    CHECK(panel.activeTabIndex == 1);
}

TEST_CASE("A tab the saved order never mentions is kept, not dropped", "[panels]") {
    auto panel = leftPanelDefaults();

    // A config from before Samples existed in this panel.
    applyPersistedTabOrder(panel, {"pluginBrowser"});

    CHECK(idsOf(panel) == std::vector<std::string>{"pluginBrowser", "mediaExplorer"});
}

TEST_CASE("A duplicated saved id does not duplicate the tab", "[panels]") {
    auto panel = leftPanelDefaults();

    applyPersistedTabOrder(panel, {"mediaExplorer", "mediaExplorer", "pluginBrowser"});

    CHECK(idsOf(panel) == std::vector<std::string>{"mediaExplorer", "pluginBrowser"});
}

TEST_CASE("A front tab the panel does not offer leaves the default in front", "[panels]") {
    auto panel = leftPanelDefaults();

    applyPersistedActiveTab(panel, "pianoRoll");

    CHECK(panel.getActiveContentType() == PanelContentType::PluginBrowser);
}
