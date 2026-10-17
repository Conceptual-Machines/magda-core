# App extensions (magda/daw/extensions/Extension.hpp): each is a directory named in
# MAGDA_EXTENSION_DIRS whose CMakeLists hands its targets over with the functions below.

# Links target whole into the app and the test binary, so its static registrars survive.
function(magda_add_extension target)
    set_property(GLOBAL APPEND PROPERTY MAGDA_EXTENSION_TARGETS ${target})
endfunction()

# Compiles the sources into magda_tests, beside core's own.
function(magda_add_extension_tests)
    foreach(source IN LISTS ARGN)
        get_filename_component(path "${source}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        set_property(GLOBAL APPEND PROPERTY MAGDA_EXTENSION_TEST_SOURCES "${path}")
    endforeach()
endfunction()

# The frozen parameter order (#1887) of the extension's devices, read after core's.
function(magda_add_extension_param_schema file)
    get_filename_component(path "${file}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set_property(GLOBAL APPEND PROPERTY MAGDA_EXTENSION_PARAM_SCHEMA_FILES "${path}")
endfunction()

# After magda/daw, so the app target exists, and before tests, which read what is collected.
function(magda_add_extension_dirs)
    foreach(dir IN LISTS MAGDA_EXTENSION_DIRS)
        get_filename_component(path "${dir}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
        if(NOT EXISTS "${path}/CMakeLists.txt")
            message(FATAL_ERROR "MAGDA_EXTENSION_DIRS names ${path}, which has no CMakeLists.txt")
        endif()
        get_filename_component(name "${path}" NAME)
        message(STATUS "MAGDA extension: ${path}")
        add_subdirectory("${path}" "${CMAKE_BINARY_DIR}/extensions/${name}")
    endforeach()

    get_property(targets GLOBAL PROPERTY MAGDA_EXTENSION_TARGETS)
    foreach(target IN LISTS targets)
        target_link_libraries(magda_daw_app PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,${target}>")
    endforeach()
endfunction()
