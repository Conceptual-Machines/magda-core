if(NOT DEFINED MAGDA_CLI)
    message(FATAL_ERROR "MAGDA_CLI is required")
endif()

if(NOT DEFINED MAGDA_CLI_WORK_DIR)
    message(FATAL_ERROR "MAGDA_CLI_WORK_DIR is required")
endif()

file(REMOVE_RECURSE "${MAGDA_CLI_WORK_DIR}")
file(MAKE_DIRECTORY "${MAGDA_CLI_WORK_DIR}")

set(project_request "${MAGDA_CLI_WORK_DIR}/headless_fixture.mgd")
set(project_file "${MAGDA_CLI_WORK_DIR}/headless_fixture/headless_fixture.mgd")
set(mutated_request "${MAGDA_CLI_WORK_DIR}/headless_fixture_mutated.mgd")
set(mutated_file "${MAGDA_CLI_WORK_DIR}/headless_fixture_mutated/headless_fixture_mutated.mgd")
set(json_file "${MAGDA_CLI_WORK_DIR}/state.json")
set(wav_file "${MAGDA_CLI_WORK_DIR}/render.wav")

execute_process(
    COMMAND "${MAGDA_CLI}" init "${project_request}"
    RESULT_VARIABLE init_result
    OUTPUT_VARIABLE init_stdout
    ERROR_VARIABLE init_stderr
)
if(NOT init_result EQUAL 0)
    message(FATAL_ERROR "magda-cli init failed: ${init_stderr}\n${init_stdout}")
endif()
if(NOT EXISTS "${project_file}")
    message(FATAL_ERROR "magda-cli init did not create ${project_file}")
endif()

execute_process(
    COMMAND "${MAGDA_CLI}" exec "${project_file}"
            set-tempo 96
            add-track audio Lead
            add-internal-instrument 1 4osc 4OSC
            add-midi-clip 1 0 4
            add-midi-note 1 0.13 60 1.87 111
            quantize-notes 1 0.25 both all
            slice-notes 1 2 all
            transpose-midi-clip 1 12
            add-track audio Follow
            add-track audio FollowMidi
            set-track-input 2 audio track:1
            set-track-input 3 midi track:1
            dump --json
            --out "${mutated_request}"
    RESULT_VARIABLE exec_result
    OUTPUT_VARIABLE exec_stdout
    ERROR_VARIABLE exec_stderr
)
if(NOT exec_result EQUAL 0)
    message(FATAL_ERROR "magda-cli exec failed: ${exec_stderr}\n${exec_stdout}")
endif()
if(NOT EXISTS "${mutated_file}")
    message(FATAL_ERROR "magda-cli exec did not create ${mutated_file}")
endif()

string(FIND "${exec_stdout}" "\"tempo\": 96" tempo_pos)
string(FIND "${exec_stdout}" "\"name\": \"Lead\"" track_pos)
string(FIND "${exec_stdout}" "\"lengthBeats\": 4" clip_pos)
string(FIND "${exec_stdout}" "\"note\": 72" note_pos)
string(FIND "${exec_stdout}" "\"velocity\": 111" velocity_pos)
string(FIND "${exec_stdout}" "\"lengthBeats\": 0.875" sliced_length_pos)
string(FIND "${exec_stdout}" "\"audioInputDevice\": \"track:1\"" audio_input_pos)
string(FIND "${exec_stdout}" "\"midiInputDevice\": \"track:1\"" midi_input_pos)
if(tempo_pos EQUAL -1 OR track_pos EQUAL -1 OR clip_pos EQUAL -1 OR note_pos EQUAL -1
   OR velocity_pos EQUAL -1 OR sliced_length_pos EQUAL -1
   OR audio_input_pos EQUAL -1 OR midi_input_pos EQUAL -1)
    file(WRITE "${json_file}" "${exec_stdout}")
    message(FATAL_ERROR "magda-cli JSON state did not match expected markers; wrote ${json_file}")
endif()

# Reload the saved project and confirm internal track-to-track routing
# (issue #1690) survives the save -> load round trip.
set(reloaded_request "${MAGDA_CLI_WORK_DIR}/headless_fixture_reloaded.mgd")
execute_process(
    COMMAND "${MAGDA_CLI}" exec "${mutated_file}"
            dump --json
            --out "${reloaded_request}"
    RESULT_VARIABLE reload_result
    OUTPUT_VARIABLE reload_stdout
    ERROR_VARIABLE reload_stderr
)
if(NOT reload_result EQUAL 0)
    message(FATAL_ERROR "magda-cli reload dump failed: ${reload_stderr}\n${reload_stdout}")
endif()

file(WRITE "${json_file}" "${reload_stdout}")
string(FIND "${reload_stdout}" "\"audioInputDevice\": \"track:1\"" reload_audio_input_pos)
string(FIND "${reload_stdout}" "\"midiInputDevice\": \"track:1\"" reload_midi_input_pos)
if(reload_audio_input_pos EQUAL -1 OR reload_midi_input_pos EQUAL -1)
    message(FATAL_ERROR
        "Reloaded project lost track input routing; see ${json_file}")
endif()

# Cycle rejection: track 2 already listens to track 1, so routing track 1's
# audio input back to track 2 must fail (model left unchanged, non-zero exit).
execute_process(
    COMMAND "${MAGDA_CLI}" exec "${mutated_file}"
            set-track-input 1 audio track:2
            --out "${MAGDA_CLI_WORK_DIR}/headless_fixture_cycle.mgd"
    RESULT_VARIABLE cycle_result
    OUTPUT_VARIABLE cycle_stdout
    ERROR_VARIABLE cycle_stderr
)
if(cycle_result EQUAL 0)
    message(FATAL_ERROR
        "set-track-input cycle (1 audio track:2) was not rejected: ${cycle_stdout}")
endif()

# Self-routing rejection: a track may not take its own output as input.
execute_process(
    COMMAND "${MAGDA_CLI}" exec "${mutated_file}"
            set-track-input 1 audio track:1
            --out "${MAGDA_CLI_WORK_DIR}/headless_fixture_selfroute.mgd"
    RESULT_VARIABLE selfroute_result
    OUTPUT_VARIABLE selfroute_stdout
    ERROR_VARIABLE selfroute_stderr
)
if(selfroute_result EQUAL 0)
    message(FATAL_ERROR
        "set-track-input self-routing (1 audio track:1) was not rejected: ${selfroute_stdout}")
endif()

execute_process(
    COMMAND "${MAGDA_CLI}" render "${mutated_file}" --wav "${wav_file}" --from 0 --to 1bar
    RESULT_VARIABLE render_result
    OUTPUT_VARIABLE render_stdout
    ERROR_VARIABLE render_stderr
)
if(NOT render_result EQUAL 0)
    message(FATAL_ERROR "magda-cli render failed: ${render_stderr}\n${render_stdout}")
endif()
if(NOT EXISTS "${wav_file}")
    message(FATAL_ERROR "magda-cli render did not create ${wav_file}")
endif()

file(SIZE "${wav_file}" wav_size)
if(wav_size LESS 100000)
    message(FATAL_ERROR "Rendered WAV is too small: ${wav_size} bytes")
endif()

file(READ "${wav_file}" wav_header_hex HEX LIMIT 12)
string(SUBSTRING "${wav_header_hex}" 0 8 riff)
string(SUBSTRING "${wav_header_hex}" 16 8 wave)
if(NOT riff STREQUAL "52494646" OR NOT wave STREQUAL "57415645")
    message(FATAL_ERROR "Rendered output does not have a RIFF/WAVE header")
endif()
