# Pure pointer hit-test model for the arrangement (#1718, #1719). The hit
# tester goes into magda_daw so tests can link it (same pattern as
# components/timeline); the CursorKind -> juce::MouseCursor mapping needs
# CursorManager (app resources), so it is app-only.

target_sources(magda_daw PRIVATE
    ArrangementHitTester.cpp
    ArrangementHitTester.hpp
    ClipNudge.hpp
)

target_sources(magda_daw_app PRIVATE
    ArrangementHitTester.cpp
    ArrangementCursorMap.cpp
)
