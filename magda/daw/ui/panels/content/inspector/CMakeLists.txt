target_sources(magda_daw_app PRIVATE
    TrackInspector.cpp
    NoteInspector.cpp
    AutomationPointInspector.cpp
    DeviceInspector.cpp
    InspectorFactory.cpp
    InspectorContainer.cpp
    BaseInspector.hpp
    TrackInspector.hpp
    ClipInspector.hpp
    NoteInspector.hpp
    AutomationPointInspector.hpp
    DeviceInspector.hpp
    InspectorFactory.hpp
    InspectorContainer.hpp
)

add_subdirectory(clip)
