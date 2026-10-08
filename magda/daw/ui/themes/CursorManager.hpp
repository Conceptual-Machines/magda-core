#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

/**
 * Manages custom mouse cursors for the DAW UI.
 * Draws crisp cursors programmatically at pixel-perfect sizes.
 */
class CursorManager {
  public:
    CursorManager(const CursorManager&) = delete;
    CursorManager& operator=(const CursorManager&) = delete;

    static CursorManager& getInstance();

    // Get zoom cursors
    const juce::MouseCursor& getZoomCursor() const {
        return zoomCursor;
    }
    const juce::MouseCursor& getZoomInCursor() const {
        return zoomInCursor;
    }
    const juce::MouseCursor& getZoomOutCursor() const {
        return zoomOutCursor;
    }
    const juce::MouseCursor& getNoteDrawCursor() const {
        return noteDrawCursor;
    }
    const juce::MouseCursor& getEraseCursor() const {
        return eraseCursor;
    }
    const juce::MouseCursor& getNoteRepeatCursor() const {
        return noteRepeatCursor;
    }
    const juce::MouseCursor& getBladeCursor() const {
        return bladeCursor;
    }
    const juce::MouseCursor& getGlueCursor() const {
        return glueCursor;
    }
    const juce::MouseCursor& getGhostCopyCursor() const {
        return ghostCopyCursor;
    }
    /// Shown while Alt is held over a glide segment that can be bent (#2198).
    const juce::MouseCursor& getCurveBendCursor() const {
        return curveBendCursor;
    }

  private:
    CursorManager();
    ~CursorManager() = default;

    // Draw a magnifying glass cursor with optional +/- glyph
    enum class ZoomGlyph { None, Plus, Minus };
    static juce::MouseCursor createZoomCursor(ZoomGlyph glyph);
    // A cursor from a 48-unit MIDI editor icon; @p hotspot is in the icon's units.
    static juce::MouseCursor createIconCursor(const char* svg, int svgSize,
                                              juce::Point<float> hotspot);
    static juce::MouseCursor createNoteRepeatCursor();
    static juce::MouseCursor createGhostCopyCursor();
    static juce::MouseCursor createCurveBendCursor();

    juce::MouseCursor zoomCursor;
    juce::MouseCursor zoomInCursor;
    juce::MouseCursor zoomOutCursor;
    juce::MouseCursor noteDrawCursor;
    juce::MouseCursor eraseCursor;
    juce::MouseCursor noteRepeatCursor;
    juce::MouseCursor bladeCursor;
    juce::MouseCursor glueCursor;
    juce::MouseCursor ghostCopyCursor;
    juce::MouseCursor curveBendCursor;
};

}  // namespace magda
