target_sources(magda_daw_app PRIVATE
    TransportPanel.cpp
    TransportLayout.cpp
    TransportTextWidths.cpp
    TimelineHeaderPanel.cpp
    LeftPanel.cpp
    RightPanel.cpp
    BottomPanel.cpp
    FooterBar.cpp
    PanelTabBar.cpp
    TabbedPanel.cpp
    TransportPanel.hpp
    TransportLayout.hpp
    TransportTextWidths.hpp
    TimelineHeaderPanel.hpp
    LeftPanel.hpp
    RightPanel.hpp
    BottomPanel.hpp
    FooterBar.hpp
    PanelTabBar.hpp
    TabbedPanel.hpp
)

add_subdirectory(state)
add_subdirectory(content)
