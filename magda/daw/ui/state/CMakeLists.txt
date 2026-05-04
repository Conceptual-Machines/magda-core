# TimelineController is part of the magda_daw library because tests
# depend on it; the headers go to both the lib (for tests) and the app.

target_sources(magda_daw PRIVATE
    TimelineController.cpp
    TimelineState.hpp
    TimelineEvents.hpp
    TimelineController.hpp
)
