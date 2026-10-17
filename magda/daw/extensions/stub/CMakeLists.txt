# The in-tree proof extension: what CI builds in place of a private one.
add_library(magda_stub_devices STATIC
    StubDevicePack.cpp
    StubDevice.hpp
)
target_compile_features(magda_stub_devices PRIVATE cxx_std_23)
target_link_libraries(magda_stub_devices PRIVATE magda::device_sdk)
magda_validate_device_pack_sources(
    "MAGDA stub extension device pack"
    ENGINE_NEUTRAL
    "${CMAKE_CURRENT_SOURCE_DIR}/StubDevicePack.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/StubDevice.hpp"
)

add_library(magda_stub_extension STATIC StubExtension.cpp)
target_link_libraries(magda_stub_extension PRIVATE magda_daw)
target_compile_definitions(magda_stub_extension INTERFACE MAGDA_STUB_EXTENSION=1)

magda_add_extension(magda_stub_devices)
magda_add_extension(magda_stub_extension)
magda_add_extension_param_schema(device_param_schema.txt)
