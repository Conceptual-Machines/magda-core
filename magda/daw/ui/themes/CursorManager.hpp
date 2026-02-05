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

  private:
    CursorManager();
    ~CursorManager() = default;

    CursorManager(const CursorManager&) = delete;
    CursorManager& operator=(const CursorManager&) = delete;

    // Draw a magnifying glass cursor with optional +/- glyph
    enum class ZoomGlyph { None, Plus, Minus };
    static juce::MouseCursor createZoomCursor(ZoomGlyph glyph);

    juce::MouseCursor zoomCursor;
    juce::MouseCursor zoomInCursor;
    juce::MouseCursor zoomOutCursor;
};

}  // namespace magda
