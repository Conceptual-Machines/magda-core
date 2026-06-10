#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

/**
 * Manages custom mouse cursors for the DAW UI.
 * Draws crisp cursors programmatically at pixel-perfect sizes.
 */
class CursorManager {
  public:
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

  private:
    CursorManager();
    ~CursorManager() = default;

    CursorManager(const CursorManager&) = delete;
    CursorManager& operator=(const CursorManager&) = delete;

    // Draw a magnifying glass cursor with optional +/- glyph
    enum class ZoomGlyph { None, Plus, Minus };
    static juce::MouseCursor createZoomCursor(ZoomGlyph glyph);
    static juce::MouseCursor createNoteDrawCursor();
    static juce::MouseCursor createEraseCursor();
    static juce::MouseCursor createNoteRepeatCursor();
    static juce::MouseCursor createBladeCursor();

    juce::MouseCursor zoomCursor;
    juce::MouseCursor zoomInCursor;
    juce::MouseCursor zoomOutCursor;
    juce::MouseCursor noteDrawCursor;
    juce::MouseCursor eraseCursor;
    juce::MouseCursor noteRepeatCursor;
    juce::MouseCursor bladeCursor;
};

}  // namespace magda
