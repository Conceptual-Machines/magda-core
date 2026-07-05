#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace magda::track_controls {

/**
 * Shared sizing for track-control rows (arrange header + inspector). Each view
 * passes its own metrics; the row composition rules below are common, so the
 * two views can't drift apart geometrically.
 */
struct Metrics {
    int rowH = 18;      // control row height
    int buttonW = 26;   // mute/solo/record/monitor button width
    int buttonH = 18;   // button height, centred in the row
    int cellW = 26;     // pan / automation / audition cell width
    int gap = 4;        // horizontal gap between cells
    int rowGap = 4;     // vertical gap between wrapped rows
    int minGainW = 56;  // keep the gain fader readable ("-12.3")
    int iconW = 16;     // routing I/O icon width
    int ddGap = 6;      // gap between routing dropdowns (and before the icon)
};

/**
 * The mix-control cluster. Null members are skipped, so per-type membership
 * comes straight from TrackControlsPolicy — layout code never tests track
 * types. The pan slot doubles as the cell for a type's stand-in control
 * (master speaker mute, chord audition).
 */
struct MixControls {
    juce::Component* gain = nullptr;        // stretches to fill
    juce::Component* pan = nullptr;         // fixed cell riding right of the gain
    int panCellH = 0;                       // pan-slot control height; 0 = full row
    std::vector<juce::Component*> buttons;  // mute/solo/record/monitor cluster
    juce::Component* trailing = nullptr;    // automation — right-pinned on its row
};

// Width of the fixed cells (everything but the gain) on a single row,
// including the gaps that precede them.
int fixedClusterWidth(const MixControls& c, const Metrics& m);

// True when a single row of the given width holds every control and still
// leaves the gain fader at least minGainW.
bool fitsOneRow(int rowWidth, const MixControls& c, const Metrics& m);

// Gain (stretch) + pan-slot cell pinned right. Shows what it places.
void layoutGainRow(juce::Rectangle<int> row, const MixControls& c, const Metrics& m);

// Buttons left-flow (shrunk to fit, never clipped) + trailing right-pinned.
void layoutButtonRow(juce::Rectangle<int> row, const MixControls& c, const Metrics& m);

// Everything on one row: gain stretches, then pan, buttons, trailing.
void layoutSingleRow(juce::Rectangle<int> row, const MixControls& c, const Metrics& m);

// Width-responsive entry point: one row when the fixed cells leave the gain
// at least minGainW; otherwise gain+pan on the first row, buttons+trailing on
// the second. Consumes from the top of `area` and returns the height used.
int layoutMixControls(juce::Rectangle<int>& area, const MixControls& c, const Metrics& m);

// Width one dropdown gets in a routing row of the given width (for aligning
// column labels above the rows).
int routingDropdownWidth(int rowWidth, int numDropdowns, const Metrics& m);

// Routing row: the visible dropdowns split the width equally, icon pinned
// right. Pass null for an absent dropdown — the other takes the full width.
void layoutRoutingRow(juce::Rectangle<int> row, juce::Component* dd1, juce::Component* dd2,
                      juce::Component* icon, const Metrics& m);

}  // namespace magda::track_controls
