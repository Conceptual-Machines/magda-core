#include "slot/DeviceSlotMidiUiBinding.hpp"

#include "audio/AudioBridge.hpp"
#include "audio/plugins/ArpeggiatorPlugin.hpp"
#include "audio/plugins/MidiChordEnginePlugin.hpp"
#include "audio/plugins/StepSequencerPlugin.hpp"
#include "core/TrackManager.hpp"
#include "custom_ui/ArpeggiatorUI.hpp"
#include "custom_ui/StepSequencerUI.hpp"
#include "engine/AudioEngine.hpp"
#include "slot/DeviceCustomUIManager.hpp"
#include "ui/panels/content/ChordPanelContent.hpp"

namespace magda::daw::ui {

void bindDeviceSlotMidiCustomUIs(DeviceCustomUIManager& customUI, magda::DeviceId deviceId,
                                 const magda::ChainNodePath& nodePath) {
    if (nodePath.trackId == magda::INVALID_TRACK_ID)
        return;

    auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
    if (audioEngine == nullptr)
        return;

    auto* bridge = audioEngine->getAudioBridge();
    if (bridge == nullptr)
        return;

    auto plugin = bridge->getPlugin(deviceId);

    if (auto* chordEngineUI = customUI.getChordEngineUI()) {
        if (auto* chordPlugin = dynamic_cast<daw::audio::MidiChordEnginePlugin*>(plugin.get()))
            chordEngineUI->setChordEngine(chordPlugin, nodePath.trackId);
    }

    if (auto* arpeggiatorUI = customUI.getArpeggiatorUI()) {
        if (auto* arp = dynamic_cast<daw::audio::ArpeggiatorPlugin*>(plugin.get()))
            arpeggiatorUI->setArpeggiator(arp);
    }

    if (auto* stepSequencerUI = customUI.getStepSequencerUI()) {
        if (auto* seq = dynamic_cast<daw::audio::StepSequencerPlugin*>(plugin.get())) {
            stepSequencerUI->setPlugin(seq);
            customUI.setStepSeqPlugin(seq);
        }
    }
}

}  // namespace magda::daw::ui
