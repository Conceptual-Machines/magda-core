#include "../themes/CursorManager.hpp"
#include "ArrangementHitTester.hpp"

namespace magda::interaction {

// App-target-only mapping (see header): CursorManager lives in
// magda_daw_app, so this TU stays out of magda_daw / the test binaries.
juce::MouseCursor toJuceCursor(CursorKind kind) {
    switch (kind) {
        case CursorKind::Normal:
            return juce::MouseCursor::NormalCursor;
        case CursorKind::LeftRightResize:
            return juce::MouseCursor::LeftRightResizeCursor;
        case CursorKind::UpDownResize:
            return juce::MouseCursor::UpDownResizeCursor;
        case CursorKind::UpDownLeftRightResize:
            return juce::MouseCursor::UpDownLeftRightResizeCursor;
        case CursorKind::DraggingHand:
            return juce::MouseCursor::DraggingHandCursor;
        case CursorKind::PointingHand:
            return juce::MouseCursor::PointingHandCursor;
        case CursorKind::Crosshair:
            return juce::MouseCursor::CrosshairCursor;
        case CursorKind::IBeam:
            return juce::MouseCursor::IBeamCursor;
        case CursorKind::Copying:
            return juce::MouseCursor::CopyingCursor;
        case CursorKind::NoteDraw:
            return CursorManager::getInstance().getNoteDrawCursor();
        case CursorKind::Erase:
            return CursorManager::getInstance().getEraseCursor();
        case CursorKind::Blade:
            return CursorManager::getInstance().getBladeCursor();
    }
    return juce::MouseCursor::NormalCursor;
}

}  // namespace magda::interaction
