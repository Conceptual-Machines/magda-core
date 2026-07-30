add_library(magda_pro_devices STATIC
    ProDevicePack.cpp
    ProStubPlugin.cpp
    ProStubPlugin.hpp
)

target_compile_features(magda_pro_devices PRIVATE cxx_std_20)
target_include_directories(magda_pro_devices PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(magda_pro_devices PRIVATE
    magda::device_sdk
)

magda_validate_device_pack_sources(
    "MAGDA pro stub device pack"
    ENGINE_NEUTRAL
    "${CMAKE_CURRENT_SOURCE_DIR}/ProDevicePack.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ProStubPlugin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ProStubPlugin.hpp"
)
