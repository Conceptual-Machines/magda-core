// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_gui_basics/juce_gui_basics.h>

#include "magda/daw/core/DeviceInfo.hpp"
#include "magda/daw/core/RackInfo.hpp"
#include "magda/daw/core/TrackInfo.hpp"
#include "magda/daw/ui/components/common/CollapsibleSectionHeader.hpp"

using namespace magda;
using magda::daw::ui::CollapsibleSectionHeader;

namespace {

/** Mods and macros panels start hidden, and a section's starting fold never undoes the user's. */
class PanelDefaultsTest final : public juce::UnitTest {
  public:
    PanelDefaultsTest() : juce::UnitTest("Panel Defaults Tests", "magda") {}

    void runTest() override {
        testChainPanelsStartClosed();
        testSectionStartsAsAsked();
        testRebuiltSectionKeepsItsFold();
    }

  private:
    void testChainPanelsStartClosed() {
        beginTest("Device, rack and track mods and macros panels start closed");
        const DeviceInfo device;
        expect(!device.modPanelOpen);
        expect(!device.paramPanelOpen);
        const RackInfo rack;
        expect(!rack.modPanelOpen);
        expect(!rack.paramPanelOpen);
        const TrackInfo track;
        expect(!track.globalModsPanelOpen);
        expect(!track.globalMacrosPanelOpen);
    }

    void testSectionStartsAsAsked() {
        beginTest("A section starts folded only when asked");
        const CollapsibleSectionHeader folded{"A", "test.panelDefaults.folded", true};
        expect(folded.isCollapsed());
        const CollapsibleSectionHeader open{"B", "test.panelDefaults.open"};
        expect(!open.isCollapsed());
    }

    void testRebuiltSectionKeepsItsFold() {
        beginTest("A section built again keeps what the user chose");
        {
            CollapsibleSectionHeader header{"C", "test.panelDefaults.unfolded", true};
            header.setCollapsed(false);
        }
        const CollapsibleSectionHeader rebuilt{"C", "test.panelDefaults.unfolded", true};
        expect(!rebuilt.isCollapsed());
    }
};

PanelDefaultsTest panelDefaultsTest;

}  // namespace
