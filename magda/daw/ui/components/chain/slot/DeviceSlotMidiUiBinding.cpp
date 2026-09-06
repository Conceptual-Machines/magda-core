#include "slot/DeviceSlotMidiUiBinding.hpp"

#include "audio/AudioBridge.hpp"
#include "audio/plugins/ArpeggiatorPlugin.hpp"
#include "audio/plugins/MidiChordEnginePlugin.hpp"
#include "audio/plugins/MidiStrumPlugin.hpp"
#include "audio/plugins/PolyStepSequencerPlugin.hpp"
#include "audio/plugins/StepSequencerPlugin.hpp"
#include "audio/plugins/tracktion/TracktionMagdaDevicePlugin.hpp"
#include "core/TrackManager.hpp"
#include "custom_ui/ArpeggiatorUI.hpp"
#include "custom_ui/PolyStepSequencerUI.hpp"
#include "custom_ui/StepSequencerUI.hpp"
#include "custom_ui/StrumUI.hpp"
#include "engine/AudioEngine.hpp"
#include "slot/DeviceCustomUIManager.hpp"
#include "ui/panels/content/ChordPanelContent.hpp"

namespace magda::daw::ui {

void bindDeviceSlotMidiCustomUIs(DeviceCustomUIManager& customUI,
                                 const magda::ChainNodePath& nodePath) {
    if (nodePath.trackId == magda::INVALID_TRACK_ID)
        return;

    auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
    if (audioEngine == nullptr)
        return;

    auto* bridge = audioEngine->getAudioBridge();
    if (bridge == nullptr)
        return;

    auto plugin = bridge->getPlugin(nodePath);

    if (auto* chordEngineUI = customUI.getChordEngineUI()) {
        if (auto* chordPlugin =
                daw::audio::tracktion_adapter::deviceFromPlugin<daw::audio::MidiChordEnginePlugin>(
                    plugin.get()))
            chordEngineUI->setChordEngine(chordPlugin, nodePath.trackId);
    }

    if (auto* arpeggiatorUI = customUI.getArpeggiatorUI()) {
        if (auto* arp =
                daw::audio::tracktion_adapter::deviceFromPlugin<daw::audio::ArpeggiatorPlugin>(
                    plugin.get()))
            arpeggiatorUI->setArpeggiator(arp);
    }

    if (customUI.getStrumUI() != nullptr) {
        // Strum is a MagdaDevice (#2299): the UI reads the model, so only the
        // note-strip binding needs the live device, through the host adapter.
        if (auto* strum =
                daw::audio::tracktion_adapter::deviceFromPlugin<daw::audio::MidiStrumPlugin>(
                    plugin.get()))
            customUI.bindStrumPlugin(strum);
    }

    // Both sequencers are MagdaDevices (#2299) whose patterns live in the model
    // (#2313): the UI reads the model, and the live device is needed only for
    // the play step, the note strip and the step recorder.
    if (auto* stepSequencerUI = customUI.getStepSequencerUI()) {
        if (auto* seq =
                daw::audio::tracktion_adapter::deviceFromPlugin<daw::audio::StepSequencerPlugin>(
                    plugin.get())) {
            stepSequencerUI->setSequencer(seq);
            customUI.bindStepSequencerPlugin(seq);
        }
    }

    if (auto* polyStepSequencerUI = customUI.getPolyStepSequencerUI()) {
        if (auto* seq = daw::audio::tracktion_adapter::deviceFromPlugin<
                daw::audio::PolyStepSequencerPlugin>(plugin.get())) {
            polyStepSequencerUI->setSequencer(seq);
            customUI.bindPolyStepSequencerPlugin(seq);
        }
    }
}

}  // namespace magda::daw::ui
