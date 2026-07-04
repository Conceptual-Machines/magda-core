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
            # Track ids are sequential by creation order: 1=Lead, 2=Follow,
            # 3=FollowMidi, then 4=Bus (group), 5=GroupMember, 6=MidiSrc,
            # 7=MidiDstB, 8=MidiDstC.
            add-track group Bus
            add-track audio GroupMember
            add-track audio MidiSrc
            add-track audio MidiDstB
            add-track audio MidiDstC
            # Group output routing: GroupMember(5) into Bus(4) sets its
            # audioOutputDevice to track:4.
            group-track 5 4
            # MIDI To track: MidiSrc(6) -> MidiDstB(7) sets 7's
            # midiInputDevice to track:6.
            route-midi-to 6 7
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

# Group output routing: GroupMember (5) was grouped under Bus (4), which must
# set its audio output to the group. Only track 5 routes to track:4, so a
# plain FIND is unambiguous. parentId 4 likewise only exists on track 5.
string(FIND "${exec_stdout}" "\"audioOutputDevice\": \"track:4\"" group_out_pos)
string(FIND "${exec_stdout}" "\"parentId\": 4" group_parent_pos)
if(group_out_pos EQUAL -1 OR group_parent_pos EQUAL -1)
    file(WRITE "${json_file}" "${exec_stdout}")
    message(FATAL_ERROR
        "group-track 5 4 did not route GroupMember's output to the Bus group; see ${json_file}")
endif()

# MIDI To track: route-midi-to 6 7 must make MidiDstB (7) listen to MidiSrc
# (6). Bind the assertion to track 7's JSON object by slicing the dump
# between the two dest track names (tracks appear in id order).
string(FIND "${exec_stdout}" "\"name\": \"MidiDstB\"" dstb_pos)
string(FIND "${exec_stdout}" "\"name\": \"MidiDstC\"" dstc_pos)
if(dstb_pos EQUAL -1 OR dstc_pos EQUAL -1)
    file(WRITE "${json_file}" "${exec_stdout}")
    message(FATAL_ERROR "MidiDstB/MidiDstC tracks missing from dump; see ${json_file}")
endif()
math(EXPR dstb_len "${dstc_pos} - ${dstb_pos}")
string(SUBSTRING "${exec_stdout}" ${dstb_pos} ${dstb_len} dstb_json)
string(FIND "${dstb_json}" "\"midiInputDevice\": \"track:6\"" dstb_midi_pos)
if(dstb_midi_pos EQUAL -1)
    file(WRITE "${json_file}" "${exec_stdout}")
    message(FATAL_ERROR
        "route-midi-to 6 7 did not set MidiDstB's midi input to track:6; see ${json_file}")
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

# Group routing must also survive the save -> load round trip: GroupMember
# (5) still outputs to Bus (4) and stays parented to it, and the MIDI To
# routing (6 -> 7) is still in place.
string(FIND "${reload_stdout}" "\"audioOutputDevice\": \"track:4\"" reload_group_out_pos)
string(FIND "${reload_stdout}" "\"parentId\": 4" reload_group_parent_pos)
string(FIND "${reload_stdout}" "\"midiInputDevice\": \"track:6\"" reload_midi_to_pos)
if(reload_group_out_pos EQUAL -1 OR reload_group_parent_pos EQUAL -1
   OR reload_midi_to_pos EQUAL -1)
    message(FATAL_ERROR
        "Reloaded project lost group output / MIDI To routing; see ${json_file}")
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

# Ungroup: removing GroupMember (5) from the Bus reverts its audio output to
# master. No track routes to track:4 afterwards, so asserting the marker is
# gone is unambiguous; the GroupMember JSON slice (between the GroupMember
# and MidiSrc track objects) must say master.
execute_process(
    COMMAND "${MAGDA_CLI}" exec "${mutated_file}"
            ungroup-track 5
            dump --json
            --out "${MAGDA_CLI_WORK_DIR}/headless_fixture_ungrouped.mgd"
    RESULT_VARIABLE ungroup_result
    OUTPUT_VARIABLE ungroup_stdout
    ERROR_VARIABLE ungroup_stderr
)
if(NOT ungroup_result EQUAL 0)
    message(FATAL_ERROR "magda-cli ungroup-track failed: ${ungroup_stderr}\n${ungroup_stdout}")
endif()
string(FIND "${ungroup_stdout}" "\"audioOutputDevice\": \"track:4\"" ungroup_stale_pos)
if(NOT ungroup_stale_pos EQUAL -1)
    file(WRITE "${json_file}" "${ungroup_stdout}")
    message(FATAL_ERROR
        "ungroup-track 5 left GroupMember routed to the Bus group; see ${json_file}")
endif()
string(FIND "${ungroup_stdout}" "\"name\": \"GroupMember\"" member_pos)
string(FIND "${ungroup_stdout}" "\"name\": \"MidiSrc\"" midisrc_pos)
if(member_pos EQUAL -1 OR midisrc_pos EQUAL -1)
    file(WRITE "${json_file}" "${ungroup_stdout}")
    message(FATAL_ERROR "GroupMember/MidiSrc tracks missing from ungroup dump; see ${json_file}")
endif()
math(EXPR member_len "${midisrc_pos} - ${member_pos}")
string(SUBSTRING "${ungroup_stdout}" ${member_pos} ${member_len} member_json)
string(FIND "${member_json}" "\"audioOutputDevice\": \"master\"" member_master_pos)
if(member_master_pos EQUAL -1)
    file(WRITE "${json_file}" "${ungroup_stdout}")
    message(FATAL_ERROR
        "ungroup-track 5 did not revert GroupMember's output to master; see ${json_file}")
endif()

# MIDI To is single-destination: re-routing MidiSrc (6) to MidiDstC (8) must
# make 8 the listener and clear MidiDstB (7). MidiDstC is the last track, so
# its JSON slice runs to the end of the dump.
execute_process(
    COMMAND "${MAGDA_CLI}" exec "${mutated_file}"
            route-midi-to 6 8
            dump --json
            --out "${MAGDA_CLI_WORK_DIR}/headless_fixture_rewired.mgd"
    RESULT_VARIABLE rewire_result
    OUTPUT_VARIABLE rewire_stdout
    ERROR_VARIABLE rewire_stderr
)
if(NOT rewire_result EQUAL 0)
    message(FATAL_ERROR "magda-cli route-midi-to rewire failed: ${rewire_stderr}\n${rewire_stdout}")
endif()
string(FIND "${rewire_stdout}" "\"name\": \"MidiDstB\"" rewire_dstb_pos)
string(FIND "${rewire_stdout}" "\"name\": \"MidiDstC\"" rewire_dstc_pos)
if(rewire_dstb_pos EQUAL -1 OR rewire_dstc_pos EQUAL -1)
    file(WRITE "${json_file}" "${rewire_stdout}")
    message(FATAL_ERROR "MidiDstB/MidiDstC tracks missing from rewire dump; see ${json_file}")
endif()
string(SUBSTRING "${rewire_stdout}" ${rewire_dstc_pos} -1 rewire_dstc_json)
string(FIND "${rewire_dstc_json}" "\"midiInputDevice\": \"track:6\"" rewire_dstc_midi_pos)
if(rewire_dstc_midi_pos EQUAL -1)
    file(WRITE "${json_file}" "${rewire_stdout}")
    message(FATAL_ERROR
        "route-midi-to 6 8 did not set MidiDstC's midi input to track:6; see ${json_file}")
endif()
math(EXPR rewire_dstb_len "${rewire_dstc_pos} - ${rewire_dstb_pos}")
string(SUBSTRING "${rewire_stdout}" ${rewire_dstb_pos} ${rewire_dstb_len} rewire_dstb_json)
string(FIND "${rewire_dstb_json}" "\"midiInputDevice\": \"track:6\"" rewire_dstb_midi_pos)
if(NOT rewire_dstb_midi_pos EQUAL -1)
    file(WRITE "${json_file}" "${rewire_stdout}")
    message(FATAL_ERROR
        "route-midi-to 6 8 left MidiDstB still listening to track:6; see ${json_file}")
endif()

# MIDI To self-routing rejection: a track may not feed its own MIDI input.
execute_process(
    COMMAND "${MAGDA_CLI}" exec "${mutated_file}"
            route-midi-to 6 6
            --out "${MAGDA_CLI_WORK_DIR}/headless_fixture_midiself.mgd"
    RESULT_VARIABLE midiself_result
    OUTPUT_VARIABLE midiself_stdout
    ERROR_VARIABLE midiself_stderr
)
if(midiself_result EQUAL 0)
    message(FATAL_ERROR
        "route-midi-to self-routing (6 6) was not rejected: ${midiself_stdout}")
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
