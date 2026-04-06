#include "DeviceCustomUIManager.hpp"

#include "audio/AudioBridge.hpp"
#include "audio/DrumGridPlugin.hpp"
#include "audio/MagdaSamplerPlugin.hpp"
#include "core/MidiFileWriter.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"
#include "engine/AudioEngine.hpp"
#include "engine/TracktionEngineWrapper.hpp"
#include "project/ProjectManager.hpp"

namespace magda::daw::ui {

// =============================================================================
// Queries
// =============================================================================

juce::Component* DeviceCustomUIManager::getActiveUI() const {
    if (toneGeneratorUI_)
        return toneGeneratorUI_.get();
    if (samplerUI_)
        return samplerUI_.get();
    if (drumGridUI_)
        return drumGridUI_.get();
    if (fourOscUI_)
        return fourOscUI_.get();
    if (eqUI_)
        return eqUI_.get();
    if (compressorUI_)
        return compressorUI_.get();
    if (reverbUI_)
        return reverbUI_.get();
    if (delayUI_)
        return delayUI_.get();
    if (chorusUI_)
        return chorusUI_.get();
    if (phaserUI_)
        return phaserUI_.get();
    if (filterUI_)
        return filterUI_.get();
    if (pitchShiftUI_)
        return pitchShiftUI_.get();
    if (impulseResponseUI_)
        return impulseResponseUI_.get();
    if (utilityUI_)
        return utilityUI_.get();
    if (chordEngineUI_)
        return chordEngineUI_.get();
    if (arpeggiatorUI_)
        return arpeggiatorUI_.get();
    if (stepSequencerUI_)
        return stepSequencerUI_.get();
    return nullptr;
}

std::vector<LinkableTextSlider*> DeviceCustomUIManager::getLinkableSliders() const {
    if (eqUI_)
        return eqUI_->getLinkableSliders();
    if (fourOscUI_)
        return fourOscUI_->getLinkableSliders();
    if (toneGeneratorUI_)
        return toneGeneratorUI_->getLinkableSliders();
    if (compressorUI_)
        return compressorUI_->getLinkableSliders();
    if (reverbUI_)
        return reverbUI_->getLinkableSliders();
    if (delayUI_)
        return delayUI_->getLinkableSliders();
    if (chorusUI_)
        return chorusUI_->getLinkableSliders();
    if (phaserUI_)
        return phaserUI_->getLinkableSliders();
    if (filterUI_)
        return filterUI_->getLinkableSliders();
    if (pitchShiftUI_)
        return pitchShiftUI_->getLinkableSliders();
    if (impulseResponseUI_)
        return impulseResponseUI_->getLinkableSliders();
    if (utilityUI_)
        return utilityUI_->getLinkableSliders();
    if (samplerUI_)
        return samplerUI_->getLinkableSliders();
    if (arpeggiatorUI_)
        return arpeggiatorUI_->getLinkableSliders();
    if (stepSequencerUI_)
        return stepSequencerUI_->getLinkableSliders();
    return {};
}

bool DeviceCustomUIManager::hasAnyUI() const {
    return toneGeneratorUI_ || samplerUI_ || drumGridUI_ || fourOscUI_ || eqUI_ || compressorUI_ ||
           reverbUI_ || delayUI_ || chorusUI_ || phaserUI_ || filterUI_ || pitchShiftUI_ ||
           impulseResponseUI_ || utilityUI_ || chordEngineUI_ || arpeggiatorUI_ || stepSequencerUI_;
}

int DeviceCustomUIManager::getPreferredContentWidth(int drumGridFallback) const {
    if (fourOscUI_)
        return 500;
    if (eqUI_)
        return 400;
    if (compressorUI_)
        return 350;
    if (reverbUI_)
        return 350;
    if (delayUI_)
        return 300;
    if (chorusUI_)
        return 350;
    if (phaserUI_)
        return 300;
    if (filterUI_)
        return 250;
    if (pitchShiftUI_)
        return 200;
    if (impulseResponseUI_)
        return 350;
    if (utilityUI_)
        return 300;
    if (stepSequencerUI_)
        return 500;
    if (chordEngineUI_)
        return 800;  // 400 (BASE_SLOT_WIDTH) * 2
    if (samplerUI_)
        return 800;  // 400 (BASE_SLOT_WIDTH) * 2
    if (drumGridUI_)
        return drumGridFallback;
    return 0;
}

int DeviceCustomUIManager::getCustomUITabIndex() const {
    if (fourOscUI_)
        return fourOscUI_->getCurrentTabIndex();
    return 0;
}

void DeviceCustomUIManager::setCustomUITabIndex(int index) {
    if (fourOscUI_) {
        fourOscUI_->setCurrentTabIndex(index);
    } else {
        pendingCustomUITabIndex_ = index;
    }
}

// =============================================================================
// readAndPushModMatrix
// =============================================================================

void DeviceCustomUIManager::readAndPushModMatrix(magda::DeviceId deviceId) {
    if (!fourOscUI_)
        return;
    auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
    if (!audioEngine)
        return;
    auto* bridge = audioEngine->getAudioBridge();
    if (!bridge)
        return;
    auto plugin = bridge->getPlugin(deviceId);
    auto* fourOsc = dynamic_cast<te::FourOscPlugin*>(plugin.get());
    if (!fourOsc)
        return;

    auto autoParams = fourOsc->getAutomatableParameters();

    // Build parameter name list for the add-popup destination dropdown
    std::vector<std::pair<int, juce::String>> paramNames;
    for (int pi = 0; pi < autoParams.size(); ++pi)
        paramNames.push_back({pi, autoParams[pi]->getParameterName()});
    fourOscUI_->setModMatrixParameterNames(paramNames);

    // Read mod matrix entries
    std::vector<ModMatrixEntry> matrixEntries;
    for (auto& [param, assign] : fourOsc->modMatrix) {
        if (!assign.isModulated())
            continue;
        int paramIdx = autoParams.indexOf(param);
        if (paramIdx < 0)
            continue;
        for (int s = 0; s < static_cast<int>(te::FourOscPlugin::numModSources); ++s) {
            if (assign.depths[s] >= -1.0f) {
                auto src = static_cast<te::FourOscPlugin::ModSource>(s);
                matrixEntries.push_back({paramIdx, autoParams[paramIdx]->getParameterName(), s,
                                         fourOsc->modulationSourceToName(src), assign.depths[s]});
            }
        }
    }
    fourOscUI_->updateModMatrix(matrixEntries);
}

// =============================================================================
// create
// =============================================================================

void DeviceCustomUIManager::create(const magda::DeviceInfo& device, juce::Component* parent,
                                   const Callbacks& callbacks) {
    if (device.pluginId.containsIgnoreCase("tone")) {
        toneGeneratorUI_ = std::make_unique<ToneGeneratorUI>();
        toneGeneratorUI_->onParameterChanged = [cb = callbacks](int paramIndex,
                                                                float normalizedValue) {
            if (cb.onParameterChanged)
                cb.onParameterChanged(paramIndex, normalizedValue);
        };
        parent->addAndMakeVisible(*toneGeneratorUI_);
        update(device);
    } else if (device.pluginId.containsIgnoreCase(daw::audio::MagdaSamplerPlugin::xmlTypeName)) {
        samplerUI_ = std::make_unique<SamplerUI>();
        samplerUI_->onParameterChanged = [cb = callbacks](int paramIndex, float value) {
            if (cb.onParameterChanged)
                cb.onParameterChanged(paramIndex, value);
        };

        samplerUI_->onLoopEnabledChanged = [deviceId = device.id](bool enabled) {
            auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
            if (!audioEngine)
                return;
            auto* bridge = audioEngine->getAudioBridge();
            if (!bridge)
                return;
            auto plugin = bridge->getPlugin(deviceId);
            if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get())) {
                sampler->loopEnabledAtomic.store(enabled, std::memory_order_relaxed);
                sampler->loopEnabledValue = enabled;
            }
        };

        samplerUI_->onRootNoteChanged = [deviceId = device.id](int note) {
            auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
            if (!audioEngine)
                return;
            auto* bridge = audioEngine->getAudioBridge();
            if (!bridge)
                return;
            auto plugin = bridge->getPlugin(deviceId);
            if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get())) {
                sampler->setRootNote(note);
            }
        };

        samplerUI_->getPlaybackPosition = [deviceId = device.id]() -> double {
            auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
            if (!audioEngine)
                return 0.0;
            auto* bridge = audioEngine->getAudioBridge();
            if (!bridge)
                return 0.0;
            auto plugin = bridge->getPlugin(deviceId);
            if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get())) {
                return sampler->getPlaybackPosition();
            }
            return 0.0;
        };

        // Shared logic for loading a sample file and refreshing the UI
        auto loadFile = [this, deviceId = device.id](const juce::File& file) {
            auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
            if (!audioEngine)
                return;
            auto* bridge = audioEngine->getAudioBridge();
            if (!bridge)
                return;
            if (bridge->loadSamplerSample(deviceId, file)) {
                auto plugin = bridge->getPlugin(deviceId);
                if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get())) {
                    samplerUI_->updateParameters(
                        sampler->attackValue.get(), sampler->decayValue.get(),
                        sampler->sustainValue.get(), sampler->releaseValue.get(),
                        sampler->pitchValue.get(), sampler->fineValue.get(),
                        sampler->levelValue.get(), sampler->sampleStartValue.get(),
                        sampler->sampleEndValue.get(), sampler->loopEnabledValue.get(),
                        sampler->loopStartValue.get(), sampler->loopEndValue.get(),
                        sampler->velAmountValue.get(), file.getFileNameWithoutExtension());
                    samplerUI_->setWaveformData(sampler->getWaveform(), sampler->getSampleRate(),
                                                sampler->getSampleLengthSeconds());
                }
            }
        };

        samplerUI_->onLoadSampleRequested = [loadFile]() {
            auto chooser = std::make_shared<juce::FileChooser>(
                "Load Sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                     juce::FileBrowserComponent::canSelectFiles,
                                 [loadFile, chooser](const juce::FileChooser&) {
                                     auto result = chooser->getResult();
                                     if (result.existsAsFile())
                                         loadFile(result);
                                 });
        };

        samplerUI_->onFileDropped = loadFile;

        parent->addAndMakeVisible(*samplerUI_);
        update(device);
    } else if (device.pluginId.containsIgnoreCase(daw::audio::DrumGridPlugin::xmlTypeName)) {
        drumGridUI_ = std::make_unique<DrumGridUI>();

        // Helper to get DrumGridPlugin pointer
        auto getDrumGrid = [deviceId = device.id]() -> daw::audio::DrumGridPlugin* {
            auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
            if (!audioEngine)
                return nullptr;
            auto* bridge = audioEngine->getAudioBridge();
            if (!bridge)
                return nullptr;
            auto plugin = bridge->getPlugin(deviceId);
            return dynamic_cast<daw::audio::DrumGridPlugin*>(plugin.get());
        };

        // Helper to get display name for first plugin in chain
        auto getChainDisplayName =
            [](const daw::audio::DrumGridPlugin::Chain& chain) -> juce::String {
            if (chain.plugins.empty())
                return {};
            auto& firstPlugin = chain.plugins[0];
            if (firstPlugin == nullptr)
                return {};
            if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(firstPlugin.get())) {
                auto f = sampler->getSampleFile();
                if (f.existsAsFile())
                    return f.getFileNameWithoutExtension();
                return "Sampler";
            }
            return firstPlugin->getName();
        };

        // Helper to update pad info from a chain covering a specific pad
        auto updatePadFromChain = [this, getChainDisplayName](daw::audio::DrumGridPlugin* dg,
                                                              int padIndex) {
            int midiNote = daw::audio::DrumGridPlugin::baseNote + padIndex;
            if (auto* chain = dg->getChainForNote(midiNote)) {
                drumGridUI_->updatePadInfo(padIndex, getChainDisplayName(*chain), chain->mute.get(),
                                           chain->solo.get(), chain->level.get(), chain->pan.get(),
                                           chain->index, chain->bypassed.get());
            } else {
                drumGridUI_->updatePadInfo(padIndex, "", false, false, 0.0f, 0.0f, -1);
            }
        };

        // Sample drop callback
        drumGridUI_->onSampleDropped = [getDrumGrid, updatePadFromChain](int padIndex,
                                                                         const juce::File& file) {
            if (auto* dg = getDrumGrid()) {
                dg->loadSampleToPad(padIndex, file);
                updatePadFromChain(dg, padIndex);
            }
        };

        // Load button callback (file chooser)
        drumGridUI_->onLoadRequested = [this, getDrumGrid, updatePadFromChain](int padIndex) {
            auto chooser = std::make_shared<juce::FileChooser>(
                "Load Sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                     juce::FileBrowserComponent::canSelectFiles,
                                 [this, padIndex, chooser, getDrumGrid,
                                  updatePadFromChain](const juce::FileChooser&) {
                                     if (!drumGridUI_)
                                         return;
                                     auto result = chooser->getResult();
                                     if (result.existsAsFile()) {
                                         if (auto* dg = getDrumGrid()) {
                                             dg->loadSampleToPad(padIndex, result);
                                             updatePadFromChain(dg, padIndex);
                                         }
                                     }
                                 });
        };

        // Clear callback
        drumGridUI_->onClearRequested = [this, getDrumGrid](int padIndex) {
            if (auto* dg = getDrumGrid()) {
                dg->clearPad(padIndex);
                drumGridUI_->updatePadInfo(padIndex, "", false, false, 0.0f, 0.0f, -1);
            }
        };

        // Level/pan/mute/solo callbacks - write directly to chain CachedValues
        drumGridUI_->onPadLevelChanged = [getDrumGrid](int padIndex, float levelDb) {
            if (auto* dg = getDrumGrid()) {
                int midiNote = daw::audio::DrumGridPlugin::baseNote + padIndex;
                if (auto* chain = dg->getChainForNote(midiNote))
                    const_cast<daw::audio::DrumGridPlugin::Chain*>(chain)->level = levelDb;
            }
        };

        drumGridUI_->onPadPanChanged = [getDrumGrid](int padIndex, float pan) {
            if (auto* dg = getDrumGrid()) {
                int midiNote = daw::audio::DrumGridPlugin::baseNote + padIndex;
                if (auto* chain = dg->getChainForNote(midiNote))
                    const_cast<daw::audio::DrumGridPlugin::Chain*>(chain)->pan = pan;
            }
        };

        drumGridUI_->onPadMuteChanged = [getDrumGrid](int padIndex, bool muted) {
            if (auto* dg = getDrumGrid()) {
                int midiNote = daw::audio::DrumGridPlugin::baseNote + padIndex;
                if (auto* chain = dg->getChainForNote(midiNote))
                    const_cast<daw::audio::DrumGridPlugin::Chain*>(chain)->mute = muted;
            }
        };

        drumGridUI_->onPadSoloChanged = [getDrumGrid](int padIndex, bool soloed) {
            if (auto* dg = getDrumGrid()) {
                int midiNote = daw::audio::DrumGridPlugin::baseNote + padIndex;
                if (auto* chain = dg->getChainForNote(midiNote))
                    const_cast<daw::audio::DrumGridPlugin::Chain*>(chain)->solo = soloed;
            }
        };

        drumGridUI_->onPadBypassChanged = [getDrumGrid](int padIndex, bool bypassed) {
            if (auto* dg = getDrumGrid()) {
                int midiNote = daw::audio::DrumGridPlugin::baseNote + padIndex;
                if (auto* chain = dg->getChainForNote(midiNote))
                    const_cast<daw::audio::DrumGridPlugin::Chain*>(chain)->bypassed = bypassed;
            }
        };

        // Plugin drag & drop onto pads (instrument slot — replaces all plugins)
        drumGridUI_->onPluginDropped =
            [getDrumGrid, updatePadFromChain](int padIndex, const juce::DynamicObject& obj) {
                auto* dg = getDrumGrid();
                if (!dg)
                    return;

                bool isExternal = obj.getProperty("isExternal");
                juce::String uniqueId = obj.getProperty("uniqueId").toString();

                // Handle internal plugins (MagdaSampler, etc.)
                if (!isExternal) {
                    if (uniqueId.containsIgnoreCase(daw::audio::MagdaSamplerPlugin::xmlTypeName)) {
                        dg->loadSampleToPad(padIndex, juce::File());
                        updatePadFromChain(dg, padIndex);
                    }
                    return;
                }

                // External plugin — look up in KnownPluginList
                juce::String fileOrId = obj.getProperty("fileOrIdentifier").toString();

                auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
                if (!audioEngine)
                    return;

                auto* teWrapper = dynamic_cast<magda::TracktionEngineWrapper*>(audioEngine);
                if (!teWrapper)
                    return;

                auto& knownPlugins = teWrapper->getKnownPluginList();
                for (const auto& desc : knownPlugins.getTypes()) {
                    if (desc.fileOrIdentifier == fileOrId ||
                        (uniqueId.isNotEmpty() && juce::String(desc.uniqueId) == uniqueId)) {
                        dg->loadPluginToPad(padIndex, desc);
                        updatePadFromChain(dg, padIndex);
                        return;
                    }
                }
                DBG("DrumGridUI: Plugin not found in KnownPluginList: " + fileOrId);
            };

        // Layout change notification (e.g., chains panel toggled)
        drumGridUI_->onLayoutChanged = [cb = callbacks]() {
            if (cb.onLayoutChanged)
                cb.onLayoutChanged();
        };

        // Delete from chain row — same as clear
        drumGridUI_->onPadDeleteRequested = [this, getDrumGrid](int padIndex) {
            if (auto* dg = getDrumGrid()) {
                dg->clearPad(padIndex);
                drumGridUI_->updatePadInfo(padIndex, "", false, false, 0.0f, 0.0f, -1);
            }
        };

        // Pad swap via drag-and-drop
        drumGridUI_->onPadsSwapped = [this, getDrumGrid, updatePadFromChain](int srcPad,
                                                                             int dstPad) {
            if (auto* dg = getDrumGrid()) {
                dg->swapPadChains(srcPad, dstPad);
                updatePadFromChain(dg, srcPad);
                updatePadFromChain(dg, dstPad);
                drumGridUI_->rebuildChainRows();
            }
        };

        // Set plugin pointer for trigger polling
        drumGridUI_->setDrumGridPlugin(getDrumGrid());

        // Play button callback — preview note via TrackManager (mouse-down/up)
        drumGridUI_->onNotePreview = [cb = callbacks, getDrumGrid](int padIndex, bool isNoteOn) {
            auto* dg = getDrumGrid();
            if (!dg)
                return;
            if (!cb.getNodePath)
                return;
            auto nodePath = cb.getNodePath();
            if (!nodePath.isValid())
                return;
            int noteNumber = daw::audio::DrumGridPlugin::baseNote + padIndex;
            magda::TrackManager::getInstance().previewNote(nodePath.trackId, noteNumber,
                                                           isNoteOn ? 100 : 0, isNoteOn);
        };

        // =========================================================================
        // PadChainPanel callbacks — per-pad FX chain management
        // =========================================================================

        auto& padChain = drumGridUI_->getPadChainPanel();

        // Provide plugin slot info for each pad (via its chain)
        padChain.getPluginSlots =
            [getDrumGrid](int padIndex) -> std::vector<PadChainPanel::PluginSlotInfo> {
            std::vector<PadChainPanel::PluginSlotInfo> result;
            auto* dg = getDrumGrid();
            if (!dg)
                return result;

            int midiNote = daw::audio::DrumGridPlugin::baseNote + padIndex;
            auto* chain = dg->getChainForNote(midiNote);
            if (!chain)
                return result;

            for (auto& plugin : chain->plugins) {
                if (!plugin)
                    continue;
                PadChainPanel::PluginSlotInfo info;
                info.plugin = plugin.get();
                info.isSampler =
                    dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get()) != nullptr;
                info.name = plugin->getName();
                result.push_back(info);
            }
            return result;
        };

        // FX plugin drop onto chain area
        padChain.onPluginDropped =
            [this, getDrumGrid, updatePadFromChain](int padIndex, const juce::DynamicObject& obj,
                                                    int insertIdx) {
                auto* dg = getDrumGrid();
                if (!dg)
                    return;

                bool isExternal = obj.getProperty("isExternal");
                juce::String uniqueId = obj.getProperty("uniqueId").toString();

                // Handle internal plugins (MagdaSampler as instrument on the pad)
                if (!isExternal) {
                    if (uniqueId.containsIgnoreCase(daw::audio::MagdaSamplerPlugin::xmlTypeName)) {
                        dg->loadSampleToPad(padIndex, juce::File());
                        updatePadFromChain(dg, padIndex);
                        drumGridUI_->getPadChainPanel().refresh();
                    }
                    return;
                }

                // External plugin — look up in KnownPluginList
                juce::String fileOrId = obj.getProperty("fileOrIdentifier").toString();

                auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
                if (!audioEngine)
                    return;
                auto* teWrapper = dynamic_cast<magda::TracktionEngineWrapper*>(audioEngine);
                if (!teWrapper)
                    return;

                auto& knownPlugins = teWrapper->getKnownPluginList();
                for (const auto& desc : knownPlugins.getTypes()) {
                    if (desc.fileOrIdentifier == fileOrId ||
                        (uniqueId.isNotEmpty() && juce::String(desc.uniqueId) == uniqueId)) {
                        dg->addPluginToPad(padIndex, desc, insertIdx);
                        drumGridUI_->getPadChainPanel().refresh();
                        return;
                    }
                }
            };

        // Remove plugin from chain
        padChain.onPluginRemoved = [getDrumGrid, updatePadFromChain](int padIndex,
                                                                     int pluginIndex) {
            auto* dg = getDrumGrid();
            if (!dg)
                return;
            dg->removePluginFromPad(padIndex, pluginIndex);
            updatePadFromChain(dg, padIndex);
        };

        // Reorder plugins in chain
        padChain.onPluginMoved = [getDrumGrid](int padIndex, int fromIdx, int toIdx) {
            if (auto* dg = getDrumGrid())
                dg->movePluginInPad(padIndex, fromIdx, toIdx);
        };

        // Forward sample operations from PadDeviceSlot -> DrumGrid
        padChain.onSampleDropped = [getDrumGrid, updatePadFromChain](int padIndex,
                                                                     const juce::File& file) {
            if (auto* dg = getDrumGrid()) {
                dg->loadSampleToPad(padIndex, file);
                updatePadFromChain(dg, padIndex);
            }
        };

        padChain.onLoadSampleRequested = [this, getDrumGrid, updatePadFromChain](int padIndex) {
            auto chooser = std::make_shared<juce::FileChooser>(
                "Load Sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                     juce::FileBrowserComponent::canSelectFiles,
                                 [this, padIndex, chooser, getDrumGrid,
                                  updatePadFromChain](const juce::FileChooser&) {
                                     if (!drumGridUI_)
                                         return;
                                     auto result = chooser->getResult();
                                     if (result.existsAsFile()) {
                                         if (auto* dg = getDrumGrid()) {
                                             dg->loadSampleToPad(padIndex, result);
                                             updatePadFromChain(dg, padIndex);
                                         }
                                     }
                                 });
        };

        padChain.onLayoutChanged = [cb = callbacks]() {
            if (cb.onLayoutChanged)
                cb.onLayoutChanged();
        };

        padChain.onDeviceClicked = [cb = callbacks](const juce::String& pluginName,
                                                    const juce::String& pluginType) {
            DBG("DeviceCustomUIManager: padChain.onDeviceClicked fired, plugin=" + pluginName +
                " type=" + pluginType);
            if (!cb.getNodePath)
                return;
            auto nodePath = cb.getNodePath();
            if (nodePath.isValid()) {
                magda::SelectionManager::getInstance().selectChainNode(nodePath, pluginName,
                                                                       pluginType);
            }
        };

        // "+" button — show plugin picker popup (same as ChainPanel)
        padChain.onAddDeviceClicked = [this, getDrumGrid](int padIndex) {
            auto* dg = getDrumGrid();
            if (!dg)
                return;

            juce::PopupMenu menu;

            // Internal FX plugins (no instruments — pad already has a sampler)
            juce::PopupMenu internalMenu;
            struct InternalEntry {
                juce::String name;
                juce::String pluginId;
            };
            const InternalEntry internals[] = {
                {"Equaliser", "eq"},
                {"Compressor", "compressor"},
                {"Reverb", "reverb"},
                {"Delay", "delay"},
                {"Chorus", "chorus"},
                {"Phaser", "phaser"},
                {"Filter", "lowpass"},
                {"Pitch Shift", "pitchshift"},
                {"IR Reverb", "impulseresponse"},
                {"Utility", "utility"},
            };
            int itemId = 1;
            for (const auto& entry : internals)
                internalMenu.addItem(itemId++, entry.name);
            menu.addSubMenu("Internal", internalMenu);

            // External plugins from KnownPluginList
            juce::Array<juce::PluginDescription> externalPlugins;
            if (auto* engine = dynamic_cast<magda::TracktionEngineWrapper*>(
                    magda::TrackManager::getInstance().getAudioEngine())) {
                auto& knownPlugins = engine->getKnownPluginList();
                externalPlugins = knownPlugins.getTypes();
            }

            if (!externalPlugins.isEmpty()) {
                std::map<juce::String, juce::PopupMenu> byManufacturer;
                for (int i = 0; i < externalPlugins.size(); ++i) {
                    const auto& desc = externalPlugins[i];
                    // Skip instruments — only show FX
                    if (desc.isInstrument)
                        continue;
                    auto manufacturer =
                        desc.manufacturerName.isEmpty() ? "Unknown" : desc.manufacturerName;
                    byManufacturer[manufacturer].addItem(1000 + i, desc.name);
                }
                for (auto& [manufacturer, subMenu] : byManufacturer)
                    menu.addSubMenu(manufacturer, subMenu);
            }

            auto capturedPlugins =
                std::make_shared<juce::Array<juce::PluginDescription>>(std::move(externalPlugins));
            auto capturedInternals = std::make_shared<std::vector<InternalEntry>>(
                std::begin(internals), std::end(internals));

            menu.showMenuAsync(
                juce::PopupMenu::Options(),
                [this, padIndex, getDrumGrid, capturedPlugins, capturedInternals](int result) {
                    if (result == 0 || !drumGridUI_)
                        return;

                    auto* dg2 = getDrumGrid();
                    if (!dg2)
                        return;

                    if (result >= 1 && result <= static_cast<int>(capturedInternals->size())) {
                        auto& entry = (*capturedInternals)[static_cast<size_t>(result - 1)];
                        int midiNote = daw::audio::DrumGridPlugin::baseNote + padIndex;
                        if (auto* chain = dg2->getChainForNote(midiNote))
                            dg2->addInternalPluginToChain(chain->index, entry.pluginId);
                        drumGridUI_->getPadChainPanel().refresh();
                    } else if (result >= 1000) {
                        int pluginIdx = result - 1000;
                        if (pluginIdx < capturedPlugins->size()) {
                            dg2->addPluginToPad(padIndex, (*capturedPlugins)[pluginIdx]);
                            drumGridUI_->getPadChainPanel().refresh();
                        }
                    }
                });
        };

        parent->addAndMakeVisible(*drumGridUI_);
        update(device);
    } else if (device.pluginId.containsIgnoreCase("4osc")) {
        fourOscUI_ = std::make_unique<FourOscUI>();
        fourOscUI_->onParameterChanged = [cb = callbacks](int paramIndex, float value) {
            if (cb.onParameterChanged)
                cb.onParameterChanged(paramIndex, value);
        };
        fourOscUI_->onPluginStateChanged = [deviceId = device.id](const juce::String& propertyId,
                                                                  juce::var value) {
            auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
            if (!audioEngine)
                return;
            auto* bridge = audioEngine->getAudioBridge();
            if (!bridge)
                return;
            auto plugin = bridge->getPlugin(deviceId);
            if (auto* fourOsc = dynamic_cast<te::FourOscPlugin*>(plugin.get()))
                fourOsc->state.setProperty(juce::Identifier(propertyId), value, nullptr);
        };
        fourOscUI_->onModDepthChanged = [deviceId = device.id](int paramIndex, int modSourceId,
                                                               float depth) {
            auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
            if (!audioEngine)
                return;
            auto* bridge = audioEngine->getAudioBridge();
            if (!bridge)
                return;
            auto plugin = bridge->getPlugin(deviceId);
            if (auto* fourOsc = dynamic_cast<te::FourOscPlugin*>(plugin.get())) {
                auto params = fourOsc->getAutomatableParameters();
                if (paramIndex >= 0 && paramIndex < params.size()) {
                    auto src = static_cast<te::FourOscPlugin::ModSource>(modSourceId);
                    fourOsc->setModulationDepth(src, params[paramIndex], depth);
                    static_cast<te::Plugin*>(fourOsc)->flushPluginStateToValueTree();
                }
            }
        };
        fourOscUI_->onModEntryRemoved = [this, deviceId = device.id](int paramIndex,
                                                                     int modSourceId) {
            auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
            if (!audioEngine)
                return;
            auto* bridge = audioEngine->getAudioBridge();
            if (!bridge)
                return;
            auto plugin = bridge->getPlugin(deviceId);
            if (auto* fourOsc = dynamic_cast<te::FourOscPlugin*>(plugin.get())) {
                auto params = fourOsc->getAutomatableParameters();
                if (paramIndex >= 0 && paramIndex < params.size()) {
                    auto src = static_cast<te::FourOscPlugin::ModSource>(modSourceId);
                    fourOsc->clearModulation(src, params[paramIndex]);
                    static_cast<te::Plugin*>(fourOsc)->flushPluginStateToValueTree();
                }
                readAndPushModMatrix(deviceId);
            }
        };
        fourOscUI_->onModMatrixStructureChanged = [this, deviceId = device.id]() {
            readAndPushModMatrix(deviceId);
        };
        parent->addAndMakeVisible(*fourOscUI_);
        update(device);
        readAndPushModMatrix(device.id);
        // Restore saved tab index after rebuild
        if (pendingCustomUITabIndex_ != NO_PENDING_TAB) {
            fourOscUI_->setCurrentTabIndex(pendingCustomUITabIndex_);
            pendingCustomUITabIndex_ = NO_PENDING_TAB;
        }
    } else if (device.pluginId.equalsIgnoreCase("eq")) {
        eqUI_ = std::make_unique<EqualiserUI>();
        eqUI_->onParameterChanged = [cb = callbacks](int paramIndex, float value) {
            if (cb.onParameterChanged)
                cb.onParameterChanged(paramIndex, value);
        };
        eqUI_->getDBGainAtFrequency = [deviceId = device.id](float freq) -> float {
            auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
            if (!audioEngine)
                return 0.0f;
            auto* bridge = audioEngine->getAudioBridge();
            if (!bridge)
                return 0.0f;
            auto plugin = bridge->getPlugin(deviceId);
            if (auto* eq = dynamic_cast<te::EqualiserPlugin*>(plugin.get()))
                return eq->getDBGainAtFrequency(freq);
            return 0.0f;
        };
        parent->addAndMakeVisible(*eqUI_);
        update(device);
    } else if (device.pluginId.containsIgnoreCase("compressor")) {
        compressorUI_ = std::make_unique<CompressorUI>();
        compressorUI_->onParameterChanged = [cb = callbacks](int paramIndex, float value) {
            if (cb.onParameterChanged)
                cb.onParameterChanged(paramIndex, value);
        };
        parent->addAndMakeVisible(*compressorUI_);
        update(device);
    } else if (device.pluginId.containsIgnoreCase("reverb")) {
        reverbUI_ = std::make_unique<ReverbUI>();
        reverbUI_->onParameterChanged = [cb = callbacks](int paramIndex, float value) {
            if (cb.onParameterChanged)
                cb.onParameterChanged(paramIndex, value);
        };
        parent->addAndMakeVisible(*reverbUI_);
        update(device);
    } else if (device.pluginId.containsIgnoreCase("delay")) {
        delayUI_ = std::make_unique<DelayUI>();
        delayUI_->onParameterChanged = [cb = callbacks](int paramIndex, float value) {
            if (cb.onParameterChanged)
                cb.onParameterChanged(paramIndex, value);
        };
        parent->addAndMakeVisible(*delayUI_);
        update(device);
    } else if (device.pluginId.containsIgnoreCase("chorus")) {
        chorusUI_ = std::make_unique<ChorusUI>();
        chorusUI_->onParameterChanged = [cb = callbacks](int paramIndex, float value) {
            if (cb.onParameterChanged)
                cb.onParameterChanged(paramIndex, value);
        };
        parent->addAndMakeVisible(*chorusUI_);
        update(device);
    } else if (device.pluginId.containsIgnoreCase("phaser")) {
        phaserUI_ = std::make_unique<PhaserUI>();
        phaserUI_->onParameterChanged = [cb = callbacks](int paramIndex, float value) {
            if (cb.onParameterChanged)
                cb.onParameterChanged(paramIndex, value);
        };
        parent->addAndMakeVisible(*phaserUI_);
        update(device);
    } else if (device.pluginId.containsIgnoreCase("lowpass")) {
        filterUI_ = std::make_unique<FilterUI>();
        filterUI_->onParameterChanged = [cb = callbacks](int paramIndex, float value) {
            if (cb.onParameterChanged)
                cb.onParameterChanged(paramIndex, value);
        };
        parent->addAndMakeVisible(*filterUI_);
        update(device);
    } else if (device.pluginId.containsIgnoreCase("pitchshift")) {
        pitchShiftUI_ = std::make_unique<PitchShiftUI>();
        pitchShiftUI_->onParameterChanged = [cb = callbacks](int paramIndex, float value) {
            if (cb.onParameterChanged)
                cb.onParameterChanged(paramIndex, value);
        };
        parent->addAndMakeVisible(*pitchShiftUI_);
        update(device);
    } else if (device.pluginId.containsIgnoreCase("impulseresponse")) {
        impulseResponseUI_ = std::make_unique<ImpulseResponseUI>();
        impulseResponseUI_->onParameterChanged = [cb = callbacks](int paramIndex, float value) {
            if (cb.onParameterChanged)
                cb.onParameterChanged(paramIndex, value);
        };

        // Helper to load an IR file into the plugin
        auto loadIR = [this, deviceId = device.id](const juce::File& file) {
            if (!file.existsAsFile()) {
                DBG("IR load: file does not exist: " << file.getFullPathName());
                return;
            }

            auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
            if (!audioEngine) {
                DBG("IR load: no audio engine");
                return;
            }
            auto* bridge = audioEngine->getAudioBridge();
            if (!bridge) {
                DBG("IR load: no audio bridge");
                return;
            }
            auto plugin = bridge->getPlugin(deviceId);
            if (!plugin) {
                DBG("IR load: no plugin found for device " << deviceId);
                return;
            }
            auto* ir = dynamic_cast<te::ImpulseResponsePlugin*>(plugin.get());
            if (!ir) {
                DBG("IR load: plugin is not ImpulseResponsePlugin, type: " << plugin->getName());
                return;
            }
            if (ir->loadImpulseResponse(file)) {
                ir->name = file.getFileNameWithoutExtension();
                if (impulseResponseUI_)
                    impulseResponseUI_->setIRName(file.getFileNameWithoutExtension());

                // Capture plugin state so the IR persists in the project
                bridge->getPluginManager().capturePluginState(deviceId);
            } else {
                DBG("IR load: loadImpulseResponse returned false for: " << file.getFullPathName());
            }
        };

        impulseResponseUI_->onLoadIRRequested = [loadIR]() {
            DBG("IR: LOAD button clicked, opening file chooser");
            auto chooser = std::make_shared<juce::FileChooser>(
                "Load Impulse Response", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
            chooser->launchAsync(
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [loadIR, chooser](const juce::FileChooser&) {
                    auto result = chooser->getResult();
                    DBG("IR: file chooser callback, result="
                        << result.getFullPathName() << " exists=" << (int)result.existsAsFile());
                    if (result.existsAsFile())
                        loadIR(result);
                });
        };

        impulseResponseUI_->onFileDropped = [loadIR](const juce::File& file) {
            DBG("IR: file dropped: " << file.getFullPathName());
            loadIR(file);
        };

        parent->addAndMakeVisible(*impulseResponseUI_);
        update(device);
    } else if (device.pluginId.containsIgnoreCase("utility")) {
        utilityUI_ = std::make_unique<UtilityUI>();
        utilityUI_->onParameterChanged = [cb = callbacks](int paramIndex, float value) {
            if (cb.onParameterChanged)
                cb.onParameterChanged(paramIndex, value);
        };
        parent->addAndMakeVisible(*utilityUI_);
        update(device);
    } else if (device.pluginId.containsIgnoreCase(daw::audio::MidiChordEnginePlugin::xmlTypeName)) {
        chordEngineUI_ = std::make_unique<ChordPanelContent>();
        parent->addAndMakeVisible(*chordEngineUI_);
        // Connect to the plugin instance
        if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(device.id);
                if (auto* cp = dynamic_cast<daw::audio::MidiChordEnginePlugin*>(plugin.get())) {
                    chordEngineUI_->setChordEngine(cp, magda::INVALID_TRACK_ID);
                    chordPlugin_ = cp;
                }
            }
        }
    } else if (device.pluginId.containsIgnoreCase(daw::audio::ArpeggiatorPlugin::xmlTypeName)) {
        arpeggiatorUI_ = std::make_unique<ArpeggiatorUI>();
        parent->addAndMakeVisible(*arpeggiatorUI_);
        if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(device.id);
                if (auto* arp = dynamic_cast<daw::audio::ArpeggiatorPlugin*>(plugin.get())) {
                    arpeggiatorUI_->setArpeggiator(arp);
                    arpPlugin_ = arp;
                }
            }
        }
    } else if (device.pluginId.containsIgnoreCase(daw::audio::StepSequencerPlugin::xmlTypeName)) {
        stepSequencerUI_ = std::make_unique<StepSequencerUI>();
        parent->addAndMakeVisible(*stepSequencerUI_);
        if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(device.id);
                if (auto* seq = dynamic_cast<daw::audio::StepSequencerPlugin*>(plugin.get())) {
                    stepSequencerUI_->setPlugin(seq);
                    stepSeqPlugin_ = seq;
                }
            }
        }
    }
}

// =============================================================================
// update
// =============================================================================

void DeviceCustomUIManager::update(const magda::DeviceInfo& device) {
    if (toneGeneratorUI_ && device.pluginId.containsIgnoreCase("tone")) {
        float frequency = 440.0f;
        float level = -12.0f;
        int waveform = 0;

        if (device.parameters.size() >= 3) {
            frequency = device.parameters[0].currentValue;
            level = device.parameters[1].currentValue;
            waveform = static_cast<int>(device.parameters[2].currentValue);
        }

        toneGeneratorUI_->updateParameters(frequency, level, waveform);
    }

    if (samplerUI_ &&
        device.pluginId.containsIgnoreCase(daw::audio::MagdaSamplerPlugin::xmlTypeName)) {
        float attack = 0.001f, decay = 0.1f, sustain = 1.0f, release = 0.1f;
        float pitch = 0.0f, fine = 0.0f, level = 0.0f;
        float sampleStart = 0.0f, sampleEnd = 0.0f;
        float loopStart = 0.0f, loopEnd = 0.0f;
        float velAmount = 1.0f;
        bool loopEnabled = false;
        int rootNote = 60;
        juce::String sampleName;

        if (device.parameters.size() >= 7) {
            attack = device.parameters[0].currentValue;
            decay = device.parameters[1].currentValue;
            sustain = device.parameters[2].currentValue;
            release = device.parameters[3].currentValue;
            pitch = device.parameters[4].currentValue;
            fine = device.parameters[5].currentValue;
            level = device.parameters[6].currentValue;
        }
        if (device.parameters.size() >= 11) {
            sampleStart = device.parameters[7].currentValue;
            sampleEnd = device.parameters[8].currentValue;
            loopStart = device.parameters[9].currentValue;
            loopEnd = device.parameters[10].currentValue;
        }
        if (device.parameters.size() >= 12) {
            velAmount = device.parameters[11].currentValue;
        }

        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (audioEngine) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(device.id);
                if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get())) {
                    auto file = sampler->getSampleFile();
                    if (file.existsAsFile())
                        sampleName = file.getFileNameWithoutExtension();
                    loopEnabled = sampler->loopEnabledValue.get();
                    sampleStart = sampler->sampleStartParam->getCurrentValue();
                    sampleEnd = sampler->sampleEndParam->getCurrentValue();
                    loopStart = sampler->loopStartParam->getCurrentValue();
                    loopEnd = sampler->loopEndParam->getCurrentValue();
                    rootNote = sampler->getRootNote();
                    if (!samplerUI_->hasWaveform())
                        samplerUI_->setWaveformData(sampler->getWaveform(),
                                                    sampler->getSampleRate(),
                                                    sampler->getSampleLengthSeconds());
                }
            }
        }

        samplerUI_->updateParameters(attack, decay, sustain, release, pitch, fine, level,
                                     sampleStart, sampleEnd, loopEnabled, loopStart, loopEnd,
                                     velAmount, sampleName, rootNote);
    }

    if (drumGridUI_ &&
        device.pluginId.containsIgnoreCase(daw::audio::DrumGridPlugin::xmlTypeName)) {
        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (audioEngine) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(device.id);
                if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(plugin.get())) {
                    for (int i = 0; i < daw::audio::DrumGridPlugin::maxPads; ++i) {
                        drumGridUI_->updatePadInfo(i, "", false, false, 0.0f, 0.0f, -1);
                    }

                    for (const auto& chain : dg->getChains()) {
                        juce::String displayName;
                        if (!chain->plugins.empty() && chain->plugins[0] != nullptr) {
                            if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(
                                    chain->plugins[0].get())) {
                                auto file = sampler->getSampleFile();
                                if (file.existsAsFile())
                                    displayName = file.getFileNameWithoutExtension();
                                else
                                    displayName = "Sampler";
                            } else {
                                displayName = chain->plugins[0]->getName();
                            }
                        }

                        for (int note = chain->lowNote; note <= chain->highNote; ++note) {
                            int padIdx = note - daw::audio::DrumGridPlugin::baseNote;
                            if (padIdx >= 0 && padIdx < daw::audio::DrumGridPlugin::maxPads) {
                                drumGridUI_->updatePadInfo(padIdx, displayName, chain->mute.get(),
                                                           chain->solo.get(), chain->level.get(),
                                                           chain->pan.get(), chain->index,
                                                           chain->bypassed.get());
                            }
                        }
                    }

                    int selectedPad = drumGridUI_->getSelectedPad();
                    drumGridUI_->getPadChainPanel().showPadChain(selectedPad);
                }
            }
        }
    }

    if (fourOscUI_ && device.pluginId.containsIgnoreCase("4osc")) {
        fourOscUI_->updateFromParameters(device.parameters);

        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (audioEngine) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(device.id);
                if (auto* fourOsc = dynamic_cast<te::FourOscPlugin*>(plugin.get())) {
                    FourOscPluginState state;
                    for (int i = 0; i < 4; ++i) {
                        state.oscWaveShape[i] = fourOsc->oscParams[i]->waveShapeValue.get();
                        state.oscVoices[i] = fourOsc->oscParams[i]->voicesValue.get();
                    }
                    state.filterType = fourOsc->filterTypeValue.get();
                    state.filterSlope = fourOsc->filterSlopeValue.get();
                    state.ampAnalog = fourOsc->ampAnalogValue.get();
                    for (int i = 0; i < 2; ++i) {
                        state.lfoWaveShape[i] = fourOsc->lfoParams[i]->waveShapeValue.get();
                        state.lfoSync[i] = fourOsc->lfoParams[i]->syncValue.get();
                    }
                    state.distortionOn = fourOsc->distortionOnValue.get();
                    state.reverbOn = fourOsc->reverbOnValue.get();
                    state.delayOn = fourOsc->delayOnValue.get();
                    state.chorusOn = fourOsc->chorusOnValue.get();
                    state.voiceMode = fourOsc->voiceModeValue.get();
                    state.globalVoices = fourOsc->voicesValue.get();
                    fourOscUI_->updatePluginState(state);
                }
            }
        }
    }

    if (eqUI_ && device.pluginId.equalsIgnoreCase("eq")) {
        eqUI_->updateFromParameters(device.parameters);
    }

    if (compressorUI_ && device.pluginId.containsIgnoreCase("compressor")) {
        compressorUI_->updateFromParameters(device.parameters);
    }

    if (reverbUI_ && device.pluginId.containsIgnoreCase("reverb")) {
        reverbUI_->updateFromParameters(device.parameters);
    }

    if (delayUI_ && device.pluginId.containsIgnoreCase("delay")) {
        delayUI_->updateFromParameters(device.parameters);
    }

    if (chorusUI_ && device.pluginId.containsIgnoreCase("chorus")) {
        chorusUI_->updateFromParameters(device.parameters);
    }

    if (phaserUI_ && device.pluginId.containsIgnoreCase("phaser")) {
        phaserUI_->updateFromParameters(device.parameters);
    }

    if (filterUI_ && device.pluginId.containsIgnoreCase("lowpass")) {
        filterUI_->updateFromParameters(device.parameters);
    }

    if (pitchShiftUI_ && device.pluginId.containsIgnoreCase("pitchshift")) {
        pitchShiftUI_->updateFromParameters(device.parameters);
    }

    if (impulseResponseUI_ && device.pluginId.containsIgnoreCase("impulseresponse")) {
        impulseResponseUI_->updateFromParameters(device.parameters);

        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (audioEngine) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(device.id);
                if (auto* ir = dynamic_cast<te::ImpulseResponsePlugin*>(plugin.get())) {
                    impulseResponseUI_->setIRName(ir->name.get());
                }
            }
        }
    }

    if (utilityUI_ && device.pluginId.containsIgnoreCase("utility")) {
        utilityUI_->updateFromParameters(device.parameters);
    }
}

}  // namespace magda::daw::ui
