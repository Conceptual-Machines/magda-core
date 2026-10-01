# Writes the parameter manifest of every base-pack device (#2939). Instantiates the devices, so
# it links the DAW library; built on request (`make device-manifests`), not with the app.
add_executable(magda_device_manifests EXCLUDE_FROM_ALL DeviceManifestsMain.cpp)
target_link_libraries(magda_device_manifests PRIVATE magda_daw magda_faust_backend)
target_include_directories(magda_device_manifests PRIVATE ${CMAKE_SOURCE_DIR})
