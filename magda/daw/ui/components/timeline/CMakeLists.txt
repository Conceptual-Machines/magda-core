# LoopMarkerInteraction and TimelineComponent are needed by tests, so
# they go into magda_daw too. Also compiled into magda_daw_app to
# preserve the existing duplication-tolerant layout (the linker takes
# magda_daw_app's own .o over the archive symbol).

target_sources(magda_daw PRIVATE
    LoopMarkerInteraction.cpp
    MarkerLaneComponent.cpp
    TimelineComponent.cpp
    LoopMarkerInteraction.hpp
    MarkerLaneComponent.hpp
    TimelineComponent.hpp
    TimelineFiller.hpp
    TimeRuler.hpp
    ZoomScrollBar.hpp
)

target_sources(magda_daw_app PRIVATE
    LoopMarkerInteraction.cpp
    MarkerLaneComponent.cpp
    TimelineComponent.cpp
    TimelineFiller.cpp
    TimeRuler.cpp
    ZoomScrollBar.cpp
)
