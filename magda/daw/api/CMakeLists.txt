# MagdaApi -- programmatic facade for the agent / scripting layer (issue #1113).

target_sources(magda_daw PRIVATE
    selection_api_live.cpp
    automation_api_live.cpp
    alias_api_live.cpp
    track_api_live.cpp
    clip_api_live.cpp
    session_api_live.cpp
    project_api_live.cpp
    undo_api_live.cpp
    midi_api_live.cpp
    transport_api_live.cpp
    focused_api_live.cpp
    # Headers (listed for IDE visibility)
    magda_api.hpp
    magda_api_live.hpp
    selection_api.hpp
    selection_api_live.hpp
    automation_api.hpp
    automation_api_live.hpp
    alias_api.hpp
    alias_api_live.hpp
    track_api.hpp
    track_api_live.hpp
    clip_api.hpp
    clip_api_live.hpp
    session_api.hpp
    project_api.hpp
    project_api_live.hpp
    undo_api.hpp
    undo_api_live.hpp
    midi_api.hpp
    midi_api_live.hpp
    transport_api.hpp
    transport_api_live.hpp
    focused_api.hpp
    focused_api_live.hpp
)
