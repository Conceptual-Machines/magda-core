# Patches a submodule's checkout at configure time, once: the reverse check skips one already in.
# A patch that no longer applies fails the configure. --ignore-whitespace copes with CRLF checkouts.
function(magda_apply_submodule_patch source_dir patch)
    execute_process(
        COMMAND git apply --reverse --check --ignore-whitespace "${patch}"
        WORKING_DIRECTORY "${source_dir}"
        RESULT_VARIABLE applied
        OUTPUT_QUIET ERROR_QUIET
    )
    if(applied EQUAL 0)
        return()
    endif()

    execute_process(
        COMMAND git apply --ignore-whitespace "${patch}"
        WORKING_DIRECTORY "${source_dir}"
        RESULT_VARIABLE result
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Could not apply ${patch} to ${source_dir}: ${error}")
    endif()
    message(STATUS "Applied ${patch}")
endfunction()
