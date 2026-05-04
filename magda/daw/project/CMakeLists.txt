# Project save/load and serialization helpers.

target_sources(magda_daw PRIVATE
    ProjectManager.cpp
    serialization/ProjectSerializer.cpp
    serialization/TrackSerializer.cpp
    serialization/ClipSerializer.cpp
    serialization/AutomationModSerializer.cpp
    # Headers (listed for IDE visibility)
    ProjectInfo.hpp
    ProjectManager.hpp
    serialization/ProjectSerializer.hpp
)
