// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_graphics/juce_graphics.h>

namespace magda::daw::ui {

/// The multiband dynamics' body: the selected band's knobs on the left, the graph over a row of
/// band tabs beside them, and past a divider a column of whole-device knobs. The faceplate
/// and the grid both place from this over the same bounds.
struct MultibandEditorGeometry {
    juce::Rectangle<int> plot;
    juce::Rectangle<int> tabs;
    juce::Rectangle<int> editor;
    juce::Rectangle<int> editorTitle;
    juce::Rectangle<int> knobs;
    juce::Rectangle<int> globals;

    static constexpr int kKnobWidth = 68;
    static constexpr int kKnobColumns = 5;
    static constexpr int kKnobRows = 2;
    static constexpr int kGlobalCount = 5;
    /// Two columns, three rows: five rows left the 200px chain no room for their values (#3205).
    static constexpr int kGlobalColumns = 2;
    static constexpr int kGlobalRows = 3;
    static constexpr int kGlobalWidth = 76;
    /// What each side panel takes from the slot, with its gap to the graph.
    static constexpr int kEditorSlotWidth = kKnobColumns * kKnobWidth + 24 + 10;
    static constexpr int kGlobalsSlotWidth = kGlobalColumns * kGlobalWidth + 6 + 25;

    /// The sidebar hides either side panel; the editor keeps the left edge and the globals the
    /// right, so the grid's cells land where the view's do.
    static MultibandEditorGeometry of(juce::Rectangle<int> bounds, bool editorShown = true,
                                      bool globalsShown = true) {
        MultibandEditorGeometry geometry;
        auto area = bounds.reduced(14, 0).withTrimmedTop(8).withTrimmedBottom(14);
        if (globalsShown) {
            geometry.globals = area.removeFromRight(kGlobalColumns * kGlobalWidth + 6);
            area.removeFromRight(25);  // The divider and its margins.
        }
        if (editorShown) {
            geometry.editor = area.removeFromLeft(kKnobColumns * kKnobWidth + 24);
            area.removeFromLeft(10);
        }
        geometry.tabs = area.removeFromBottom(26);
        area.removeFromBottom(8);
        geometry.plot = area;

        auto editor = geometry.editor.reduced(12, 6);
        geometry.editorTitle = editor.removeFromTop(24);
        geometry.knobs = editor;
        return geometry;
    }

    /// Band knob @p index, five to a row, then the whole-device knobs two to a row.
    juce::Rectangle<int> knob(int index) const {
        constexpr int kGap = 6;
        if (index >= kKnobColumns * kKnobRows) {
            const int global = index - kKnobColumns * kKnobRows;
            const int width = (globals.getWidth() - kGap * (kGlobalColumns - 1)) / kGlobalColumns;
            const int height = (globals.getHeight() - kGap * (kGlobalRows - 1)) / kGlobalRows;
            return {globals.getX() + (global % kGlobalColumns) * (width + kGap),
                    globals.getY() + (global / kGlobalColumns) * (height + kGap), width, height};
        }
        const int width = (knobs.getWidth() - kGap * (kKnobColumns - 1)) / kKnobColumns;
        const int height = (knobs.getHeight() - kGap * (kKnobRows - 1)) / kKnobRows;
        return {knobs.getX() + (index % kKnobColumns) * (width + kGap),
                knobs.getY() + (index / kKnobColumns) * (height + kGap), width, height};
    }
};

}  // namespace magda::daw::ui
