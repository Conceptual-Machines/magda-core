// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_gui_basics/juce_gui_basics.h>

#include "magda/daw/ui/panels/PanelFocusTracker.hpp"
#include "magda/daw/ui/themes/FocusHighlight.hpp"

using namespace magda::daw::ui;

// The focused-panel outline and the region lookup behind Ctrl+A (#3157).
class PanelFocusTrackerTests : public juce::UnitTest {
  public:
    PanelFocusTrackerTests() : juce::UnitTest("Panel focus tracker", "magda") {}

    void runTest() override {
        juce::Component host, content, left, outside, leaf;
        juce::Component nested;
        host.setBounds(0, 0, 400, 300);
        host.addAndMakeVisible(content);
        host.addAndMakeVisible(left);
        host.addAndMakeVisible(outside);
        content.addAndMakeVisible(nested);
        nested.addAndMakeVisible(leaf);

        PanelFocusTracker tracker(host);
        tracker.addRegion(content, FocusRegion::Content);
        tracker.addRegion(left, FocusRegion::Left);

        beginTest("A component maps to the region that contains it");
        {
            expect(tracker.regionOf(&content) == FocusRegion::Content);
            expect(tracker.regionOf(&leaf) == FocusRegion::Content);
            expect(tracker.regionOf(&left) == FocusRegion::Left);
            expect(tracker.regionOf(&outside) == FocusRegion::None);
            expect(tracker.regionOf(&host) == FocusRegion::None);
            expect(tracker.regionOf(nullptr) == FocusRegion::None);
        }

        beginTest("Regions take focus so a click inside them lands there");
        {
            expect(content.getWantsKeyboardFocus());
            expect(left.getWantsKeyboardFocus());
        }

        beginTest("The outline lies over the host and ignores the mouse");
        {
            const auto& outline = tracker.getOutline();
            expect(outline.getParentComponent() == &host);
            bool allowsClick = true, allowsChildren = true;
            outline.getInterceptsMouseClicks(allowsClick, allowsChildren);
            expect(!allowsClick && !allowsChildren);
        }

        beginTest("No outline while nothing has focus");
        {
            tracker.update();
            expect(!tracker.getOutline().isVisible());
            expect(tracker.getActiveRegion() == FocusRegion::None);
        }

        beginTest("The highlight is an accent line fading inward (#3232)");
        {
            constexpr int size = 64;
            constexpr float glow = 6.0f;
            constexpr float radius = 4.0f;
            juce::Image image(juce::Image::ARGB, size, size, true);
            {
                juce::Graphics g(image);
                paintFocusHighlight(g, image.getBounds(), glow, radius);
            }

            const auto alphaAt = [&](int x, int y) { return image.getPixelAt(x, y).getAlpha(); };
            expectEquals(static_cast<int>(alphaAt(size / 2, 0)), 255, "the line is opaque");
            expect(alphaAt(size / 2, 1) > alphaAt(size / 2, 3), "the glow fades inward");
            expect(alphaAt(size / 2, 3) > alphaAt(size / 2, 5), "the glow fades inward");
            expectEquals(static_cast<int>(alphaAt(size / 2, 8)), 0, "the glow stops at its width");
            expectEquals(static_cast<int>(alphaAt(size / 2, size / 2)), 0, "the middle is clear");
            expectEquals(static_cast<int>(alphaAt(0, 0)), 0, "the corner is rounded off");
        }
    }
};

static PanelFocusTrackerTests panelFocusTrackerTests;
