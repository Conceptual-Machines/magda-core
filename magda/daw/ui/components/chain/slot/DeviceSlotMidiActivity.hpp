#pragma once

#include <array>

namespace magda {
class MidiNoteStrip;
}

namespace magda::daw::ui {

class DeviceCustomUIManager;
struct DeviceSlotTraits;

void refreshDeviceSlotMidiActivity(const DeviceSlotTraits& traits,
                                   const DeviceCustomUIManager& customUI,
                                   magda::MidiNoteStrip& midiNoteStrip, int& lastSingleNote,
                                   std::array<int, 32>& lastChordNotes, int& lastChordCount);

}  // namespace magda::daw::ui
