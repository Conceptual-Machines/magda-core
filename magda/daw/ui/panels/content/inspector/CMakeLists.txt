target_sources(magda_daw_app PRIVATE
    TrackInspector.cpp
    MasterInspectorView.cpp
    NoteInspector.cpp
    AutomationClipInspector.cpp
    AutomationPointInspector.cpp
    DeviceInspector.cpp
    InspectorFactory.cpp
    InspectorContainer.cpp
    ModulationSection.cpp
    BaseInspector.hpp
    TrackInspector.hpp
    ClipInspector.hpp
    NoteInspector.hpp
    AutomationPointInspector.hpp
    DeviceInspector.hpp
    InspectorFactory.hpp
    InspectorContainer.hpp
    ModulationSection.hpp
)

add_subdirectory(clip)
