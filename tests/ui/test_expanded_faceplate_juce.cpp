// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

// Only one faceplate is ever out in the arrangement: lending a second gives the first back, and a
// slot rebuilt while its faceplate is out is asked to lend the same part again.

#include <juce_gui_basics/juce_gui_basics.h>

#include "magda/daw/ui/state/ExpandedFaceplate.hpp"

namespace {

using magda::ChainNodePath;
using magda::ExpandedFaceplate;

}  // namespace

class ExpandedFaceplateTest final : public juce::UnitTest {
  public:
    ExpandedFaceplateTest() : juce::UnitTest("Expanded Faceplate", "magda") {}

    void runTest() override {
        auto& expanded = ExpandedFaceplate::getInstance();
        const auto first = ChainNodePath::topLevelDevice(1, 10);
        const auto second = ChainNodePath::topLevelDevice(2, 20);
        juce::Component a, b;
        int aBack = 0, bBack = 0;

        beginTest("Lending a second faceplate gives the first back");
        expanded.expand(first, a, "A", [&] { ++aBack; });
        expect(expanded.holds(a));
        expanded.expand(second, b, "B", [&] { ++bBack; });
        expectEquals(aBack, 1);
        expect(!expanded.holds(a));
        expect(expanded.holds(b));
        expectEquals(expanded.title(), juce::String("B"));

        beginTest("Collapsing gives the faceplate back once");
        expanded.collapse();
        expectEquals(bBack, 1);
        expect(!expanded.isActive());
        expanded.collapse();
        expectEquals(bBack, 1);

        beginTest("A released faceplate waits for its rebuilt slot");
        expanded.expand(first, a, "A", [&] { ++aBack; });
        expanded.release(a);
        expectEquals(aBack, 1, "a released faceplate is not given back");
        expect(expanded.isActive());
        expect(expanded.isPendingFor(first));
        expect(!expanded.isPendingFor(second));
        expanded.expand(first, b, "A", [&] { ++bBack; });
        expect(!expanded.isPendingFor(first));
        expect(expanded.holds(b));
        expanded.collapse();

        beginTest("A released part keeps its ID for the rebuilt slot (#3208)");
        juce::Component part;
        part.setComponentID("TimeBend");
        expanded.expand(first, part, "A", {});
        expectEquals(expanded.partId(), juce::String("TimeBend"));
        expanded.release(part);
        expect(expanded.isPendingFor(first));
        expectEquals(expanded.partId(), juce::String("TimeBend"));
        expanded.expand(first, a, "A", {});
        expect(expanded.partId().isEmpty(), "an unnamed part kept the last one's ID");
        expanded.collapse();
        expect(expanded.partId().isEmpty());
    }
};

static ExpandedFaceplateTest expandedFaceplateTest;
