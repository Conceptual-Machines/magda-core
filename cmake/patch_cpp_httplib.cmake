file(READ "${HTTPLIB_SOURCE_DIR}/httplib.h" HTTPLIB_HEADER)

# FetchContent may run PATCH_COMMAND again when CMake regenerates. Keep the
# pinned dependency patch idempotent so an already-populated build tree remains
# configurable.
if(NOT HTTPLIB_HEADER MATCHES "void shutdown\\(\\);[\r\n]+  const Request &request")
    execute_process(
        COMMAND git apply --unidiff-zero --ignore-space-change "${HTTPLIB_PATCH}"
        WORKING_DIRECTORY "${HTTPLIB_SOURCE_DIR}"
        RESULT_VARIABLE PATCH_RESULT
        ERROR_VARIABLE PATCH_ERROR
    )
    if(NOT PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not patch cpp-httplib: ${PATCH_ERROR}")
    endif()
endif()
