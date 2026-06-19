# Project save/load and serialization helpers.

target_sources(magda_daw PRIVATE
    ProjectManager.cpp
    MediaCollector.cpp
    serialization/ProjectSerializer.cpp
    serialization/NativeProjectDocumentAdapter.cpp
    serialization/DawProjectXmlAdapter.cpp
    serialization/DawProjectValidator.cpp
    serialization/DawProjectArchive.cpp
    serialization/TrackSerializer.cpp
    serialization/ClipSerializer.cpp
    serialization/AutomationModSerializer.cpp
    # Headers (listed for IDE visibility)
    ProjectInfo.hpp
    ProjectManager.hpp
    MediaCollector.hpp
    serialization/ProjectSerializer.hpp
    serialization/ProjectDocument.hpp
    serialization/NativeProjectDocumentAdapter.hpp
    serialization/DawProjectXmlAdapter.hpp
    serialization/DawProjectValidator.hpp
    serialization/DawProjectArchive.hpp
)
