#include "slot/DeviceSlotMidiActivity.hpp"

#include <atomic>

#include "audio/plugins/ArpeggiatorPlugin.hpp"
#include "audio/plugins/MidiChordEnginePlugin.hpp"
#include "audio/plugins/MidiStrumPlugin.hpp"
#include "audio/plugins/PolyStepSequencerPlugin.hpp"
#include "audio/plugins/StepSequencerPlugin.hpp"
#include "slot/DeviceCustomUIManager.hpp"
#include "slot/DeviceSlotTraits.hpp"
#include "ui/components/mixer/MidiNoteStrip.hpp"

namespace magda::daw::ui {

namespace {

void refreshSingleNoteStrip(daw::audio::ArpeggiatorPlugin* plugin, magda::MidiNoteStrip& strip,
                            int& lastNote) {
    if (plugin == nullptr)
        return;

    const int note = plugin->midiOutNote_.load(std::memory_order_relaxed);
    const int vel = plugin->midiOutVelocity_.load(std::memory_order_relaxed);
    if (note != lastNote) {
        if (lastNote >= 0)
            strip.clearNote(lastNote);
        lastNote = note;
    }
    if (note >= 0)
        strip.setNote(note, vel);
}

void refreshSingleNoteStrip(daw::audio::StepSequencerPlugin* plugin, magda::MidiNoteStrip& strip,
                            int& lastNote) {
    if (plugin == nullptr)
        return;

    const int note = plugin->midiOutNote_.load(std::memory_order_relaxed);
    const int vel = plugin->midiOutVelocity_.load(std::memory_order_relaxed);
    if (note != lastNote) {
        if (lastNote >= 0)
            strip.clearNote(lastNote);
        lastNote = note;
    }
    if (note >= 0)
        strip.setNote(note, vel);
}

void refreshSingleNoteStrip(daw::audio::MidiStrumPlugin* plugin, magda::MidiNoteStrip& strip,
                            int& lastNote) {
    if (plugin == nullptr)
        return;

    const int note = plugin->midiOutNote_.load(std::memory_order_relaxed);
    const int vel = plugin->midiOutVelocity_.load(std::memory_order_relaxed);
    if (note != lastNote) {
        if (lastNote >= 0)
            strip.clearNote(lastNote);
        lastNote = note;
    }
    if (note >= 0)
        strip.setNote(note, vel);
}

void refreshSingleNoteStrip(daw::audio::PolyStepSequencerPlugin* plugin,
                            magda::MidiNoteStrip& strip, int& lastNote) {
    if (plugin == nullptr)
        return;

    const int note = plugin->midiOutNote_.load(std::memory_order_relaxed);
    const int vel = plugin->midiOutVelocity_.load(std::memory_order_relaxed);
    if (note != lastNote) {
        if (lastNote >= 0)
            strip.clearNote(lastNote);
        lastNote = note;
    }
    if (note >= 0)
        strip.setNote(note, vel);
}

void refreshChordStrip(daw::audio::MidiChordEnginePlugin* plugin, magda::MidiNoteStrip& strip,
                       std::array<int, 32>& lastChordNotes, int& lastChordCount) {
    if (plugin == nullptr)
        return;

    const int count = plugin->getHeldNoteCount();
    for (int i = 0; i < lastChordCount; ++i)
        strip.clearNote(lastChordNotes[static_cast<size_t>(i)]);

    for (int i = 0; i < count && i < static_cast<int>(lastChordNotes.size()); ++i) {
        const int note = plugin->getHeldNote(i);
        lastChordNotes[static_cast<size_t>(i)] = note;
        strip.setNote(note, 100);
    }
    lastChordCount = count;
}

}  // namespace

void refreshDeviceSlotMidiActivity(const DeviceSlotTraits& traits,
                                   const DeviceCustomUIManager& customUI,
                                   magda::MidiNoteStrip& midiNoteStrip, int& lastSingleNote,
                                   std::array<int, 32>& lastChordNotes, int& lastChordCount) {
    if (traits.isArpeggiator) {
        refreshSingleNoteStrip(customUI.getArpPlugin(), midiNoteStrip, lastSingleNote);
    } else if (traits.isStrum) {
        refreshSingleNoteStrip(customUI.getStrumPlugin(), midiNoteStrip, lastSingleNote);
    } else if (traits.isStepSequencer) {
        refreshSingleNoteStrip(customUI.getStepSeqPlugin(), midiNoteStrip, lastSingleNote);
    } else if (traits.isPolyStepSequencer) {
        refreshSingleNoteStrip(customUI.getPolyStepSeqPlugin(), midiNoteStrip, lastSingleNote);
    } else if (traits.isChordEngine) {
        refreshChordStrip(customUI.getChordPlugin(), midiNoteStrip, lastChordNotes, lastChordCount);
    }
}

}  // namespace magda::daw::ui
