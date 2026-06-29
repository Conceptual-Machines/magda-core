#include "slot/DeviceSlotMidiActivity.hpp"

#include "slot/DeviceCustomUIManager.hpp"
#include "slot/DeviceSlotTraits.hpp"
#include "ui/components/mixer/MidiNoteStrip.hpp"

namespace magda::daw::ui {

void refreshDeviceSlotMidiActivity(const DeviceSlotTraits& traits,
                                   const DeviceCustomUIManager& customUI,
                                   magda::MidiNoteStrip& midiNoteStrip, int& lastSingleNote,
                                   std::array<int, 32>& lastChordNotes, int& lastChordCount) {
    if (traits.isArpeggiator) {
        customUI.refreshArpeggiatorMidiActivity(midiNoteStrip, lastSingleNote);
    } else if (traits.isStrum) {
        customUI.refreshStrumMidiActivity(midiNoteStrip, lastSingleNote);
    } else if (traits.isStepSequencer) {
        customUI.refreshStepSequencerMidiActivity(midiNoteStrip, lastSingleNote);
    } else if (traits.isPolyStepSequencer) {
        customUI.refreshPolyStepSequencerMidiActivity(midiNoteStrip, lastSingleNote);
    } else if (traits.isChordEngine) {
        customUI.refreshChordEngineMidiActivity(midiNoteStrip, lastChordNotes, lastChordCount);
    }
}

}  // namespace magda::daw::ui
