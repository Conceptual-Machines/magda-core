target_sources(magda_daw_app PRIVATE
    RampCurveDisplay.cpp
    TimeBendPopup.cpp
    SvgButton.cpp
    QwertyKeyboardPopup.cpp
    IconSelector.cpp
    ZoomControls.cpp
    LayoutDebugPanel.cpp
    MixerDebugPanel.cpp
    GridOverlayComponent.cpp
    LinkableTextSlider.cpp
    Toast.cpp
    DraggableValueLabel.cpp
    BarsBeatsTicksLabel.cpp
    SvgButton.hpp
    IconSelector.hpp
    ZoomControls.hpp
    ModeSwitcher.hpp
    LayoutDebugPanel.hpp
    MixerDebugPanel.hpp
    GridOverlayComponent.hpp
    LinkableTextSlider.hpp
    DraggableValueLabel.hpp
    BarsBeatsTicksLabel.hpp
)

add_subdirectory(curve)
