# Upstream JUCE fixes our pin predates, applied in place at configure time. Each
# is a no-op once the source already has it, so a JUCE bump retires it silently.

# juce-framework/JUCE@b96c59cfe6 (#3178): the Win32 removeConsumer() re-added the
# consumer, so closing one of two MidiInputs on a port left a dangling consumer
# that the next incoming message called into.
set(JUCE_MIDI_WINDOWS
    "${CMAKE_CURRENT_LIST_DIR}/../third_party/JUCE/modules/juce_audio_devices/native/juce_Midi_windows.cpp")
file(READ "${JUCE_MIDI_WINDOWS}" JUCE_MIDI_WINDOWS_SOURCE)
string(REGEX REPLACE
    "(void removeConsumer \\(ump::Consumer& c\\)[\r\n]+ +{[\r\n]+ +consumers\\.)add \\(c\\);"
    "\\1remove (c);"
    JUCE_MIDI_WINDOWS_PATCHED "${JUCE_MIDI_WINDOWS_SOURCE}")
if(NOT JUCE_MIDI_WINDOWS_PATCHED STREQUAL JUCE_MIDI_WINDOWS_SOURCE)
    file(WRITE "${JUCE_MIDI_WINDOWS}" "${JUCE_MIDI_WINDOWS_PATCHED}")
    message(STATUS "Patched JUCE Win32 MIDI removeConsumer (JUCE@b96c59cfe6)")
endif()
