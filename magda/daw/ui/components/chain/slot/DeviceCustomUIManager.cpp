#include "slot/DeviceCustomUIManager.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>

#include "audio/AudioBridge.hpp"
#include "audio/plugins/ArpeggiatorPlugin.hpp"
#include "audio/plugins/DrumGridPlugin.hpp"
#include "audio/plugins/DrumGridRoles.hpp"
#include "audio/plugins/FaustInstrumentPlugin.hpp"
#include "audio/plugins/FaustPlugin.hpp"
#include "audio/plugins/IFaustEditorModel.hpp"
#include "audio/plugins/MagdaSamplerPlugin.hpp"
#include "audio/plugins/MidiChordEnginePlugin.hpp"
#include "audio/plugins/OscilloscopePlugin.hpp"
#include "audio/plugins/PolyStepSequencerPlugin.hpp"
#include "audio/plugins/SpectrumAnalyzerPlugin.hpp"
#include "audio/plugins/StepSequencerPlugin.hpp"
#include "audio/plugins/compiled/CompiledPluginRegistry.hpp"
#include "audio/plugins/mutable/MutableCloudsPlugin.hpp"
#include "compiled/CompiledPluginPresentation.hpp"
#include "core/InternalDeviceKind.hpp"
#include "core/MidiFileWriter.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"
#include "custom_ui/ArpeggiatorUI.hpp"
#include "custom_ui/ChorusUI.hpp"
#include "custom_ui/CompressorUI.hpp"
#include "custom_ui/DelayUI.hpp"
#include "custom_ui/DrumVoiceUI.hpp"
#include "custom_ui/EqualiserUI.hpp"
#include "custom_ui/FMUI.hpp"
#include "custom_ui/FaustInstrumentTabbedUI.hpp"
#include "custom_ui/FaustUI.hpp"
#include "custom_ui/FilterUI.hpp"
#include "custom_ui/FourOscUI.hpp"
#include "custom_ui/HaloUI.hpp"
#include "custom_ui/ImpulseResponseUI.hpp"
#include "custom_ui/LevelsUI.hpp"
#include "custom_ui/MateriaUI.hpp"
#include "custom_ui/NimbusUI.hpp"
#include "custom_ui/OscilloscopeUI.hpp"
#include "custom_ui/PhaserUI.hpp"
#include "custom_ui/PitchShiftUI.hpp"
#include "custom_ui/PolyStepSequencerUI.hpp"
#include "custom_ui/PolySynthUI.hpp"
#include "custom_ui/ReverbUI.hpp"
#include "custom_ui/SamplerUI.hpp"
#include "custom_ui/SpectrumAnalyzerUI.hpp"
#include "custom_ui/StepSequencerUI.hpp"
#include "custom_ui/ToneGeneratorUI.hpp"
#include "drum_grid/DrumGridUI.hpp"
#include "engine/AudioEngine.hpp"
#include "engine/TracktionEngineWrapper.hpp"
#include "media_db/ClapAudioEncoder.hpp"
#include "media_db/ClapTextEncoder.hpp"
#include "media_db/MediaDbContext.hpp"
#include "media_db/RobertaTokenizer.hpp"
#include "project/ProjectManager.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"
#include "ui/panels/content/ChordPanelContent.hpp"

namespace magda::daw::ui {

namespace {

struct RolePrompt {
    const char* roleId;
    const char* prompt;
};

constexpr std::array<RolePrompt, 20> kDrumRolePrompts{{
    {"kick", "kick drum"},
    {"kick", "bass drum"},
    {"snare", "snare drum"},
    {"snare-rim", "snare rimshot"},
    {"clap", "hand clap drum sample"},
    {"hh-closed", "closed hi hat"},
    {"hh-closed", "tight closed hihat"},
    {"hh-open", "open hi hat"},
    {"hh-pedal", "pedal hi hat"},
    {"ride", "ride cymbal"},
    {"ride-bell", "ride cymbal bell"},
    {"crash", "crash cymbal"},
    {"tom-high", "high tom drum"},
    {"tom-mid", "mid tom drum"},
    {"tom-low", "low tom floor tom"},
    {"perc-1", "percussion drum hit"},
    {"perc-2", "conga bongo percussion"},
    {"perc-3", "shaker tambourine percussion"},
    {"perc-4", "cowbell percussion"},
    {"perc-4", "woodblock percussion"},
}};

struct CachedRoleEmbedding {
    juce::String roleId;
    std::vector<float> embedding;
};

std::optional<std::vector<float>> loadMono48kRegion(const juce::File& file, double startSeconds,
                                                    double endSeconds) {
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (!reader || reader->lengthInSamples <= 0 || reader->numChannels < 1 ||
        reader->sampleRate <= 0.0) {
        return std::nullopt;
    }

    const int srcSr = static_cast<int>(reader->sampleRate);
    const int srcChannels = static_cast<int>(reader->numChannels);
    const auto fullLen = reader->lengthInSamples;
    const auto startSample = juce::jlimit<juce::int64>(
        0, fullLen - 1, static_cast<juce::int64>(std::floor(startSeconds * srcSr)));

    juce::int64 endSample = fullLen;
    if (endSeconds > startSeconds) {
        endSample = juce::jlimit<juce::int64>(
            startSample + 1, fullLen, static_cast<juce::int64>(std::ceil(endSeconds * srcSr)));
    }

    const auto regionLen64 = endSample - startSample;
    if (regionLen64 <= 0 || regionLen64 > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }
    const int regionLen = static_cast<int>(regionLen64);

    juce::AudioBuffer<float> multi(srcChannels, regionLen);
    multi.clear();
    reader->read(&multi, 0, regionLen, startSample, true, true);

    std::vector<float> mono(static_cast<size_t>(regionLen), 0.0F);
    const float gain = 1.0F / static_cast<float>(srcChannels);
    for (int ch = 0; ch < srcChannels; ++ch) {
        const float* src = multi.getReadPointer(ch);
        for (int i = 0; i < regionLen; ++i)
            mono[static_cast<size_t>(i)] += src[i] * gain;
    }

    if (srcSr == 48000)
        return mono;

    const double ratio = static_cast<double>(srcSr) / 48000.0;
    const int dstLen = std::max(1, static_cast<int>(static_cast<double>(regionLen) / ratio));
    std::vector<float> dst(static_cast<size_t>(dstLen), 0.0F);
    juce::LagrangeInterpolator interp;
    interp.process(ratio, mono.data(), dst.data(), dstLen);
    return dst;
}

float dotProduct(const std::vector<float>& a, const std::vector<float>& b) {
    const auto n = std::min(a.size(), b.size());
    float sum = 0.0F;
    for (size_t i = 0; i < n; ++i)
        sum += a[i] * b[i];
    return sum;
}

std::vector<CachedRoleEmbedding> roleTextEmbeddings(magda::media::ClapTextEncoder& textEncoder,
                                                    magda::media::RobertaTokenizer& tokenizer) {
    static std::mutex cacheMutex;
    static const magda::media::ClapTextEncoder* cachedTextEncoder = nullptr;
    static const magda::media::RobertaTokenizer* cachedTokenizer = nullptr;
    static std::vector<CachedRoleEmbedding> cached;

    std::lock_guard<std::mutex> lock(cacheMutex);
    if (cachedTextEncoder == &textEncoder && cachedTokenizer == &tokenizer && !cached.empty())
        return cached;

    std::vector<CachedRoleEmbedding> next;
    next.reserve(kDrumRolePrompts.size());
    for (const auto& prompt : kDrumRolePrompts) {
        auto encoded = tokenizer.encode(prompt.prompt);
        next.push_back(
            {prompt.roleId, textEncoder.embedTokens(encoded.inputIds, encoded.attentionMask)});
    }

    cachedTextEncoder = &textEncoder;
    cachedTokenizer = &tokenizer;
    cached = std::move(next);
    return cached;
}

struct RoleAnalysisResult {
    bool ok = false;
    juce::String roleId;
    float score = 0.0F;
    juce::String error;
};

RoleAnalysisResult classifyDrumRole(const juce::File& file, double startSeconds, double endSeconds,
                                    magda::media::ClapAudioEncoder& audioEncoder,
                                    magda::media::ClapTextEncoder& textEncoder,
                                    magda::media::RobertaTokenizer& tokenizer) {
    try {
        auto mono = loadMono48kRegion(file, startSeconds, endSeconds);
        if (!mono || mono->empty())
            return {false, {}, 0.0F, "Could not read the pad sample."};

        auto audioEmbedding = audioEncoder.embed(mono->data(), static_cast<int>(mono->size()));
        if (audioEmbedding.empty())
            return {false, {}, 0.0F, "The sample analyzer returned an empty embedding."};

        auto roleEmbeddings = roleTextEmbeddings(textEncoder, tokenizer);
        juce::String bestRole;
        float bestScore = -1.0F;
        for (const auto& role : roleEmbeddings) {
            const float score = dotProduct(audioEmbedding, role.embedding);
            if (score > bestScore) {
                bestScore = score;
                bestRole = role.roleId;
            }
        }

        if (bestRole.isEmpty())
            return {false, {}, 0.0F, "Could not infer a drum role for this sample."};

        return {true, bestRole, bestScore, {}};
    } catch (const std::exception& e) {
        return {false, {}, 0.0F, juce::String("Sample role analysis failed: ") + e.what()};
    }
}

bool isLegacyTeCompressorPluginId(const juce::String& pluginId) {
    return magda::classifyInternalDevice(pluginId) == magda::InternalDeviceKind::TeCompressor;
}

struct InternalFxEntry {
    juce::String name;
    juce::String pluginId;
};

void addInternalFxEntry(std::vector<InternalFxEntry>& entries, juce::String name,
                        juce::String pluginId) {
    if (pluginId.isNotEmpty())
        entries.push_back({name, pluginId});
}

void addCompiledInternalFxEntry(std::vector<InternalFxEntry>& entries,
                                const juce::String& displayName) {
    for (auto* spec : audio::compiled::getAllCompiledPluginSpecs()) {
        if (spec != nullptr && displayName.equalsIgnoreCase(spec->displayName)) {
            addInternalFxEntry(entries, spec->displayName, spec->pluginId);
            return;
        }
    }
}

template <typename Ui>
void forwardParameterChanges(Ui& ui, const DeviceCustomUIManager::Callbacks& callbacks) {
    ui.onParameterChanged = [cb = callbacks](int paramIndex, float value) {
        if (cb.onParameterChanged)
            cb.onParameterChanged(paramIndex, value);
    };
}

}  // namespace

DeviceCustomUIManager::DeviceCustomUIManager() = default;
DeviceCustomUIManager::~DeviceCustomUIManager() = default;

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
    if (faustInstrumentUI_)
        return faustInstrumentUI_.get();
    if (polySynthUI_)
        return polySynthUI_.get();
    if (fmUI_)
        return fmUI_.get();
    if (materiaUI_)
        return materiaUI_.get();
    if (haloUI_)
        return haloUI_.get();
    if (nimbusUI_)
        return nimbusUI_.get();
    if (drumVoiceUI_)
        return drumVoiceUI_.get();
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
    if (faustUI_)
        return faustUI_.get();
    if (chordEngineUI_)
        return chordEngineUI_.get();
    if (arpeggiatorUI_)
        return arpeggiatorUI_.get();
    if (stepSequencerUI_)
        return stepSequencerUI_.get();
    if (polyStepSequencerUI_)
        return polyStepSequencerUI_.get();
    if (oscilloscopeUI_)
        return oscilloscopeUI_.get();
    if (spectrumAnalyzerUI_)
        return spectrumAnalyzerUI_.get();
    if (levelsUI_)
        return levelsUI_.get();
    return nullptr;
}

std::vector<LinkableTextSlider*> DeviceCustomUIManager::getLinkableSliders() const {
    if (eqUI_)
        return eqUI_->getLinkableSliders();
    if (fourOscUI_)
        return fourOscUI_->getLinkableSliders();
    if (faustInstrumentUI_)
        return faustInstrumentUI_->getLinkableSliders();
    if (polySynthUI_)
        return polySynthUI_->getLinkableSliders();
    if (fmUI_)
        return fmUI_->getLinkableSliders();
    if (materiaUI_)
        return materiaUI_->getLinkableSliders();
    if (haloUI_)
        return haloUI_->getLinkableSliders();
    if (nimbusUI_)
        return nimbusUI_->getLinkableSliders();
    if (drumVoiceUI_)
        return drumVoiceUI_->getLinkableSliders();
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
    if (samplerUI_)
        return samplerUI_->getLinkableSliders();
    if (arpeggiatorUI_)
        return arpeggiatorUI_->getLinkableSliders();
    if (stepSequencerUI_)
        return stepSequencerUI_->getLinkableSliders();
    if (polyStepSequencerUI_)
        return polyStepSequencerUI_->getLinkableSliders();
    return {};
}

bool DeviceCustomUIManager::hasAnyUI() const {
    return toneGeneratorUI_ || samplerUI_ || drumGridUI_ || fourOscUI_ || faustInstrumentUI_ ||
           polySynthUI_ || fmUI_ || materiaUI_ || haloUI_ || nimbusUI_ || eqUI_ || compressorUI_ ||
           reverbUI_ || delayUI_ || chorusUI_ || phaserUI_ || filterUI_ || pitchShiftUI_ ||
           impulseResponseUI_ || faustUI_ || chordEngineUI_ || arpeggiatorUI_ || stepSequencerUI_ ||
           polySynthUI_ || fmUI_ || drumVoiceUI_ || eqUI_ || compressorUI_ || reverbUI_ ||
           delayUI_ || chorusUI_ || phaserUI_ || filterUI_ || pitchShiftUI_ || impulseResponseUI_ ||
           faustUI_ || chordEngineUI_ || arpeggiatorUI_ || stepSequencerUI_ ||
           polyStepSequencerUI_ || oscilloscopeUI_ || spectrumAnalyzerUI_ || levelsUI_;
}

int DeviceCustomUIManager::getPreferredContentWidth(int drumGridFallback) const {
    if (fourOscUI_)
        return 500;
    if (faustInstrumentUI_)
        return 560;  // instruments render wider than effect slots
    if (polySynthUI_)
        return 720;  // osc + filter columns + stacked envelope column on one page
    if (fmUI_)
        return 740;  // 4x4 matrix + 4 operator columns + wider amp/right column
    if (materiaUI_)
        return 720;  // VOICE row + EXCITER | RESONATOR two-column faceplate
    if (haloUI_)
        return 760;  // modal-response spectrum + PARAMETERS | RESONATOR MODEL
    if (nimbusUI_)
        return 720;  // grain cloud + PARAMETERS | mode controls
    if (drumVoiceUI_)
        return drumVoiceUI_->preferredContentWidth();  // one labelled box per knob
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
    if (stepSequencerUI_)
        return 500;
    if (polyStepSequencerUI_)
        return 720;  // 560 grid + ~156 right-hand control panel
    if (oscilloscopeUI_)
        return 500;
    if (spectrumAnalyzerUI_)
        return 500;
    if (levelsUI_)
        return 460;
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
    if (faustInstrumentUI_)
        return faustInstrumentUI_->getCurrentTabIndex();
    return 0;
}

void DeviceCustomUIManager::setCustomUITabIndex(int index) {
    if (faustInstrumentUI_) {
        faustInstrumentUI_->setCurrentTabIndex(index);
    } else if (fourOscUI_) {
        fourOscUI_->setCurrentTabIndex(index);
    } else {
        pendingCustomUITabIndex_ = index;
    }
}

// =============================================================================
// readAndPushModMatrix
// =============================================================================

void DeviceCustomUIManager::readAndPushModMatrix(magda::DeviceId /*deviceId*/) {
    if (!fourOscUI_)
        return;
    auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
    if (!audioEngine)
        return;
    auto* bridge = audioEngine->getAudioBridge();
    if (!bridge)
        return;
    auto plugin = bridge->getPlugin(devicePath_);
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

void DeviceCustomUIManager::refreshParameterValues(const magda::DeviceInfo& device) {
    if (faustInstrumentUI_ &&
        device.pluginId.equalsIgnoreCase(daw::audio::FaustInstrumentPlugin::xmlTypeName))
        faustInstrumentUI_->updateFromParameters(device.parameters);
    if (polySynthUI_ && device.pluginId.equalsIgnoreCase("magda_polysynth"))
        polySynthUI_->updateFromParameters(device.parameters);
    if (drumVoiceUI_ && DrumVoiceUI::handles(device.pluginId))
        drumVoiceUI_->updateFromParameters(device.parameters);
    if (fmUI_ && device.pluginId.equalsIgnoreCase("magda_fm"))
        fmUI_->updateFromParameters(device.parameters);
    if (materiaUI_ && device.pluginId.equalsIgnoreCase("magda_elements"))
        materiaUI_->updateFromParameters(device.parameters);
    if (haloUI_ && device.pluginId.equalsIgnoreCase("magda_rings"))
        haloUI_->updateFromParameters(device.parameters);
    if (nimbusUI_ && device.pluginId.equalsIgnoreCase("magda_clouds"))
        nimbusUI_->updateFromParameters(device.parameters);
    if (eqUI_ && device.pluginId.equalsIgnoreCase("eq"))
        eqUI_->updateFromParameters(device.parameters);
    if (compressorUI_ && isLegacyTeCompressorPluginId(device.pluginId))
        compressorUI_->updateFromParameters(device.parameters);
    if (reverbUI_ && device.pluginId.containsIgnoreCase("reverb") &&
        !shouldSuppressLegacyUi(device.pluginId, LegacyUiKind::Reverb))
        reverbUI_->updateFromParameters(device.parameters);
    if (delayUI_ && device.pluginId.containsIgnoreCase("delay"))
        delayUI_->updateFromParameters(device.parameters);
    if (chorusUI_ && device.pluginId.containsIgnoreCase("chorus"))
        chorusUI_->updateFromParameters(device.parameters);
    if (phaserUI_ && device.pluginId.containsIgnoreCase("phaser"))
        phaserUI_->updateFromParameters(device.parameters);
    if (filterUI_ && device.pluginId.containsIgnoreCase("lowpass"))
        filterUI_->updateFromParameters(device.parameters);
    if (pitchShiftUI_ && device.pluginId.containsIgnoreCase("pitchshift"))
        pitchShiftUI_->updateFromParameters(device.parameters);
    if (impulseResponseUI_ && device.pluginId.containsIgnoreCase("impulseresponse"))
        impulseResponseUI_->updateFromParameters(device.parameters);
    if (fourOscUI_ && device.pluginId.containsIgnoreCase("4osc"))
        fourOscUI_->updateFromParameters(device.parameters);
}

// =============================================================================
// create
// =============================================================================

void DeviceCustomUIManager::createToneGeneratorUI(const magda::DeviceInfo& device,
                                                  juce::Component& parent,
                                                  const Callbacks& callbacks) {
    toneGeneratorUI_ = std::make_unique<ToneGeneratorUI>();
    forwardParameterChanges(*toneGeneratorUI_, callbacks);
    parent.addAndMakeVisible(*toneGeneratorUI_);
    update(device);
}

bool DeviceCustomUIManager::createSamplerUI(const magda::DeviceInfo& device,
                                            juce::Component& parent, const Callbacks& callbacks) {
    if (!device.pluginId.containsIgnoreCase(daw::audio::MagdaSamplerPlugin::xmlTypeName))
        return false;

    samplerUI_ = std::make_unique<SamplerUI>();
    samplerUI_->onParameterChanged = [cb = callbacks](int paramIndex, float value) {
        if (cb.onParameterChanged)
            cb.onParameterChanged(paramIndex, value);
    };

    samplerUI_->onLoopEnabledChanged = [this](bool enabled) {
        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (!audioEngine)
            return;
        auto* bridge = audioEngine->getAudioBridge();
        if (!bridge)
            return;
        auto plugin = bridge->getPlugin(devicePath_);
        if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get())) {
            sampler->loopEnabledAtomic.store(enabled, std::memory_order_relaxed);
            sampler->loopEnabledValue = enabled;
        }
    };

    samplerUI_->onRootNoteChanged = [this](int note) {
        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (!audioEngine)
            return;
        auto* bridge = audioEngine->getAudioBridge();
        if (!bridge)
            return;
        auto plugin = bridge->getPlugin(devicePath_);
        if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get())) {
            sampler->setRootNote(note);
        }
    };

    samplerUI_->getPlaybackPosition = [this]() -> double {
        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (!audioEngine)
            return 0.0;
        auto* bridge = audioEngine->getAudioBridge();
        if (!bridge)
            return 0.0;
        auto plugin = bridge->getPlugin(devicePath_);
        if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get())) {
            return sampler->getPlaybackPosition();
        }
        return 0.0;
    };

    // Shared logic for loading a sample file and refreshing the UI
    auto loadFile = [this](const juce::File& file) {
        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (!audioEngine)
            return;
        auto* bridge = audioEngine->getAudioBridge();
        if (!bridge)
            return;
        if (bridge->loadSamplerSample(devicePath_, file)) {
            auto plugin = bridge->getPlugin(devicePath_);
            if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get())) {
                samplerUI_->updateParameters(
                    sampler->attackValue.get(), sampler->decayValue.get(),
                    sampler->sustainValue.get(), sampler->releaseValue.get(),
                    sampler->pitchValue.get(), sampler->fineValue.get(), sampler->levelValue.get(),
                    sampler->sampleStartValue.get(), sampler->sampleEndValue.get(),
                    sampler->loopEnabledValue.get(), sampler->loopStartValue.get(),
                    sampler->loopEndValue.get(), sampler->velAmountValue.get(),
                    file.getFileNameWithoutExtension());
                samplerUI_->setWaveformData(sampler->getWaveform(), sampler->getSampleRate(),
                                            sampler->getSampleLengthSeconds());
            }
        }
    };

    samplerUI_->onLoadSampleRequested = [loadFile]() {
        auto chooser = std::make_shared<juce::FileChooser>("Load Sample", juce::File(),
                                                           "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles,
                             [loadFile, chooser](const juce::FileChooser&) {
                                 auto result = chooser->getResult();
                                 if (result.existsAsFile())
                                     loadFile(result);
                             });
    };

    samplerUI_->onFileDropped = loadFile;

    parent.addAndMakeVisible(*samplerUI_);
    update(device);

    return true;
}

bool DeviceCustomUIManager::createAnalyzerUI(const magda::DeviceInfo& device,
                                             juce::Component& parent) {
    if (device.pluginId.containsIgnoreCase(daw::audio::OscilloscopePlugin::xmlTypeName)) {
        oscilloscopeUI_ = std::make_unique<OscilloscopeUI>();
        parent.addAndMakeVisible(*oscilloscopeUI_);
        // Plugin binding is deferred to bindAnalyzerPlugins(), re-run from
        // setDevicePath(): create() runs before the slot's path is valid.
        bindAnalyzerPlugins();
        return true;
    }

    if (device.pluginId.containsIgnoreCase(daw::audio::SpectrumAnalyzerPlugin::xmlTypeName)) {
        spectrumAnalyzerUI_ = std::make_unique<SpectrumAnalyzerUI>();
        parent.addAndMakeVisible(*spectrumAnalyzerUI_);
        bindAnalyzerPlugins();
        return true;
    }

    if (device.pluginId.containsIgnoreCase(daw::audio::LevelsPlugin::xmlTypeName)) {
        levelsUI_ = std::make_unique<LevelsUI>();
        parent.addAndMakeVisible(*levelsUI_);
        bindAnalyzerPlugins();
        return true;
    }

    return false;
}

bool DeviceCustomUIManager::createMidiUtilityUI(const magda::DeviceInfo& device,
                                                juce::Component& parent) {
    if (device.pluginId.containsIgnoreCase(daw::audio::MidiChordEnginePlugin::xmlTypeName)) {
        chordEngineUI_ = std::make_unique<ChordPanelContent>();
        parent.addAndMakeVisible(*chordEngineUI_);
        // Connect to the plugin instance
        if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(devicePath_);
                if (auto* cp = dynamic_cast<daw::audio::MidiChordEnginePlugin*>(plugin.get())) {
                    chordEngineUI_->setChordEngine(cp, magda::INVALID_TRACK_ID);
                    chordPlugin_ = cp;
                }
            }
        }
        return true;
    }

    if (device.pluginId.containsIgnoreCase(daw::audio::ArpeggiatorPlugin::xmlTypeName)) {
        arpeggiatorUI_ = std::make_unique<ArpeggiatorUI>();
        parent.addAndMakeVisible(*arpeggiatorUI_);
        if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(devicePath_);
                if (auto* arp = dynamic_cast<daw::audio::ArpeggiatorPlugin*>(plugin.get())) {
                    arpeggiatorUI_->setArpeggiator(arp);
                    arpPlugin_ = arp;
                }
            }
        }
        return true;
    }

    if (device.pluginId.containsIgnoreCase(daw::audio::PolyStepSequencerPlugin::xmlTypeName)) {
        // NB: checked before the mono sequencer — "polystepsequencer" also
        // contains "stepsequencer", so the order of these branches matters.
        polyStepSequencerUI_ = std::make_unique<PolyStepSequencerUI>();
        parent.addAndMakeVisible(*polyStepSequencerUI_);
        if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(devicePath_);
                if (auto* seq = dynamic_cast<daw::audio::PolyStepSequencerPlugin*>(plugin.get())) {
                    polyStepSequencerUI_->setPlugin(seq);
                    polyStepSeqPlugin_ = seq;
                }
            }
        }
        return true;
    }

    if (device.pluginId.containsIgnoreCase(daw::audio::StepSequencerPlugin::xmlTypeName)) {
        stepSequencerUI_ = std::make_unique<StepSequencerUI>();
        parent.addAndMakeVisible(*stepSequencerUI_);
        if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
            if (auto* bridge = audioEngine->getAudioBridge()) {
                auto plugin = bridge->getPlugin(devicePath_);
                if (auto* seq = dynamic_cast<daw::audio::StepSequencerPlugin*>(plugin.get())) {
                    stepSequencerUI_->setPlugin(seq);
                    stepSeqPlugin_ = seq;
                }
            }
        }
        return true;
    }

    return false;
}

bool DeviceCustomUIManager::createFourOscUI(const magda::DeviceInfo& device,
                                            juce::Component& parent, const Callbacks& callbacks) {
    if (!device.pluginId.containsIgnoreCase("4osc"))
        return false;

    fourOscUI_ = std::make_unique<FourOscUI>();
    fourOscUI_->onParameterChanged = [cb = callbacks](int paramIndex, float value) {
        if (cb.onParameterChanged)
            cb.onParameterChanged(paramIndex, value);
    };
    fourOscUI_->onPluginStateChanged = [this](const juce::String& propertyId, juce::var value) {
        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (!audioEngine)
            return;
        auto* bridge = audioEngine->getAudioBridge();
        if (!bridge)
            return;
        auto plugin = bridge->getPlugin(devicePath_);
        if (auto* fourOsc = dynamic_cast<te::FourOscPlugin*>(plugin.get()))
            fourOsc->state.setProperty(juce::Identifier(propertyId), value, nullptr);
    };
    fourOscUI_->onModDepthChanged = [this](int paramIndex, int modSourceId, float depth) {
        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (!audioEngine)
            return;
        auto* bridge = audioEngine->getAudioBridge();
        if (!bridge)
            return;
        auto plugin = bridge->getPlugin(devicePath_);
        if (auto* fourOsc = dynamic_cast<te::FourOscPlugin*>(plugin.get())) {
            auto params = fourOsc->getAutomatableParameters();
            if (paramIndex >= 0 && paramIndex < params.size()) {
                auto src = static_cast<te::FourOscPlugin::ModSource>(modSourceId);
                fourOsc->setModulationDepth(src, params[paramIndex], depth);
                static_cast<te::Plugin*>(fourOsc)->flushPluginStateToValueTree();
            }
        }
    };
    fourOscUI_->onModEntryRemoved = [this](int paramIndex, int modSourceId) {
        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (!audioEngine)
            return;
        auto* bridge = audioEngine->getAudioBridge();
        if (!bridge)
            return;
        auto plugin = bridge->getPlugin(devicePath_);
        if (auto* fourOsc = dynamic_cast<te::FourOscPlugin*>(plugin.get())) {
            auto params = fourOsc->getAutomatableParameters();
            if (paramIndex >= 0 && paramIndex < params.size()) {
                auto src = static_cast<te::FourOscPlugin::ModSource>(modSourceId);
                fourOsc->clearModulation(src, params[paramIndex]);
                static_cast<te::Plugin*>(fourOsc)->flushPluginStateToValueTree();
            }
            readAndPushModMatrix(devicePath_.getDeviceId());
        }
    };
    fourOscUI_->onModMatrixStructureChanged = [this]() {
        readAndPushModMatrix(devicePath_.getDeviceId());
    };
    parent.addAndMakeVisible(*fourOscUI_);
    update(device);
    readAndPushModMatrix(device.id);
    // Restore saved tab index after rebuild
    if (pendingCustomUITabIndex_ != NO_PENDING_TAB) {
        fourOscUI_->setCurrentTabIndex(pendingCustomUITabIndex_);
        pendingCustomUITabIndex_ = NO_PENDING_TAB;
    }

    return true;
}

bool DeviceCustomUIManager::createCustomInstrumentUI(const magda::DeviceInfo& device,
                                                     juce::Component& parent,
                                                     const Callbacks& callbacks) {
    if (device.pluginId.equalsIgnoreCase(daw::audio::FaustInstrumentPlugin::xmlTypeName)) {
        faustInstrumentUI_ = std::make_unique<FaustInstrumentTabbedUI>();
        forwardParameterChanges(*faustInstrumentUI_, callbacks);
        faustInstrumentUI_->setDevicePath(devicePath_);
        parent.addAndMakeVisible(*faustInstrumentUI_);
        refreshLivePluginBindings();
        update(device);
        if (pendingCustomUITabIndex_ != NO_PENDING_TAB) {
            faustInstrumentUI_->setCurrentTabIndex(pendingCustomUITabIndex_);
            pendingCustomUITabIndex_ = NO_PENDING_TAB;
        }
        return true;
    }

    if (device.pluginId.equalsIgnoreCase("magda_polysynth")) {
        polySynthUI_ = std::make_unique<PolySynthUI>();
        forwardParameterChanges(*polySynthUI_, callbacks);
        parent.addAndMakeVisible(*polySynthUI_);
        update(device);
        return true;
    }

    if (device.pluginId.equalsIgnoreCase("magda_fm")) {
        fmUI_ = std::make_unique<FMUI>();
        forwardParameterChanges(*fmUI_, callbacks);
        parent.addAndMakeVisible(*fmUI_);
        update(device);
        return true;
    }

    if (device.pluginId.equalsIgnoreCase("magda_elements")) {
        materiaUI_ = std::make_unique<MateriaUI>();
        forwardParameterChanges(*materiaUI_, callbacks);
        parent.addAndMakeVisible(*materiaUI_);
        update(device);
        return true;
    }

    if (device.pluginId.equalsIgnoreCase("magda_rings")) {
        haloUI_ = std::make_unique<HaloUI>();
        forwardParameterChanges(*haloUI_, callbacks);
        parent.addAndMakeVisible(*haloUI_);
        update(device);
        return true;
    }

    if (device.pluginId.equalsIgnoreCase("magda_clouds")) {
        nimbusUI_ = std::make_unique<NimbusUI>();
        forwardParameterChanges(*nimbusUI_, callbacks);
        parent.addAndMakeVisible(*nimbusUI_);
        refreshLivePluginBindings();
        update(device);
        return true;
    }

    if (DrumVoiceUI::handles(device.pluginId)) {
        drumVoiceUI_ = std::make_unique<DrumVoiceUI>(device.pluginId);
        forwardParameterChanges(*drumVoiceUI_, callbacks);
        parent.addAndMakeVisible(*drumVoiceUI_);
        update(device);
        return true;
    }

    return false;
}

bool DeviceCustomUIManager::createSimpleEffectUI(const magda::DeviceInfo& device,
                                                 juce::Component& parent,
                                                 const Callbacks& callbacks) {
    if (device.pluginId.equalsIgnoreCase("eq")) {
        eqUI_ = std::make_unique<EqualiserUI>();
        forwardParameterChanges(*eqUI_, callbacks);
        eqUI_->getDBGainAtFrequency = [this](float freq) -> float {
            auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
            if (!audioEngine)
                return 0.0f;
            auto* bridge = audioEngine->getAudioBridge();
            if (!bridge)
                return 0.0f;
            auto plugin = bridge->getPlugin(devicePath_);
            if (auto* eq = dynamic_cast<te::EqualiserPlugin*>(plugin.get()))
                return eq->getDBGainAtFrequency(freq);
            return 0.0f;
        };
        parent.addAndMakeVisible(*eqUI_);
        update(device);
        return true;
    }

    if (isLegacyTeCompressorPluginId(device.pluginId)) {
        compressorUI_ = std::make_unique<CompressorUI>();
        forwardParameterChanges(*compressorUI_, callbacks);
        parent.addAndMakeVisible(*compressorUI_);
        update(device);
        return true;
    }

    if (device.pluginId.containsIgnoreCase("reverb") &&
        !shouldSuppressLegacyUi(device.pluginId, LegacyUiKind::Reverb)) {
        reverbUI_ = std::make_unique<ReverbUI>();
        forwardParameterChanges(*reverbUI_, callbacks);
        parent.addAndMakeVisible(*reverbUI_);
        update(device);
        return true;
    }

    if (device.pluginId.containsIgnoreCase("delay") &&
        !shouldSuppressLegacyUi(device.pluginId, LegacyUiKind::Delay)) {
        delayUI_ = std::make_unique<DelayUI>();
        forwardParameterChanges(*delayUI_, callbacks);
        parent.addAndMakeVisible(*delayUI_);
        update(device);
        return true;
    }

    if (device.pluginId.containsIgnoreCase("chorus") &&
        !shouldSuppressLegacyUi(device.pluginId, LegacyUiKind::Chorus)) {
        chorusUI_ = std::make_unique<ChorusUI>();
        forwardParameterChanges(*chorusUI_, callbacks);
        parent.addAndMakeVisible(*chorusUI_);
        update(device);
        return true;
    }

    if (device.pluginId.containsIgnoreCase("phaser") &&
        !shouldSuppressLegacyUi(device.pluginId, LegacyUiKind::Phaser)) {
        phaserUI_ = std::make_unique<PhaserUI>();
        forwardParameterChanges(*phaserUI_, callbacks);
        parent.addAndMakeVisible(*phaserUI_);
        update(device);
        return true;
    }

    if (device.pluginId.containsIgnoreCase("lowpass")) {
        filterUI_ = std::make_unique<FilterUI>();
        forwardParameterChanges(*filterUI_, callbacks);
        parent.addAndMakeVisible(*filterUI_);
        update(device);
        return true;
    }

    if (device.pluginId.containsIgnoreCase("pitchshift")) {
        pitchShiftUI_ = std::make_unique<PitchShiftUI>();
        forwardParameterChanges(*pitchShiftUI_, callbacks);
        parent.addAndMakeVisible(*pitchShiftUI_);
        update(device);
        return true;
    }

    return false;
}

bool DeviceCustomUIManager::createImpulseResponseUI(const magda::DeviceInfo& device,
                                                    juce::Component& parent,
                                                    const Callbacks& callbacks) {
    if (!device.pluginId.containsIgnoreCase("impulseresponse"))
        return false;

    impulseResponseUI_ = std::make_unique<ImpulseResponseUI>();
    impulseResponseUI_->onParameterChanged = [cb = callbacks](int paramIndex, float value) {
        if (cb.onParameterChanged)
            cb.onParameterChanged(paramIndex, value);
    };

    // Helper to load an IR file into the plugin
    auto loadIR = [this](const juce::File& file) {
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
        auto plugin = bridge->getPlugin(devicePath_);
        if (!plugin) {
            DBG("IR load: no plugin found for device " << devicePath_.getDeviceId());
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
            bridge->getPluginManager().capturePluginState(devicePath_);
        } else {
            DBG("IR load: loadImpulseResponse returned false for: " << file.getFullPathName());
        }
    };

    impulseResponseUI_->onLoadIRRequested = [loadIR]() {
        DBG("IR: LOAD button clicked, opening file chooser");
        auto chooser = std::make_shared<juce::FileChooser>("Load Impulse Response", juce::File(),
                                                           "*.wav;*.aif;*.aiff;*.flac;*.ogg");
        chooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [loadIR, chooser](const juce::FileChooser&) {
                auto result = chooser->getResult();
                DBG("IR: file chooser callback, result=" << result.getFullPathName() << " exists="
                                                         << (int)result.existsAsFile());
                if (result.existsAsFile())
                    loadIR(result);
            });
    };

    impulseResponseUI_->onFileDropped = [loadIR](const juce::File& file) {
        DBG("IR: file dropped: " << file.getFullPathName());
        loadIR(file);
    };

    parent.addAndMakeVisible(*impulseResponseUI_);
    update(device);

    return true;
}

bool DeviceCustomUIManager::createDrumGridUI(const magda::DeviceInfo& device,
                                             juce::Component& parent, const Callbacks& callbacks) {
    if (!device.pluginId.containsIgnoreCase(daw::audio::DrumGridPlugin::xmlTypeName))
        return false;

    drumGridUI_ = std::make_unique<DrumGridUI>();

    // Helper to get DrumGridPlugin pointer
    auto getDrumGrid = [this]() -> daw::audio::DrumGridPlugin* {
        auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
        if (!audioEngine)
            return nullptr;
        auto* bridge = audioEngine->getAudioBridge();
        if (!bridge)
            return nullptr;
        auto plugin = bridge->getPlugin(devicePath_);
        return dynamic_cast<daw::audio::DrumGridPlugin*>(plugin.get());
    };

    // Helper to get display name for first plugin in chain
    auto getChainDisplayName = [](const daw::audio::DrumGridPlugin::Chain& chain) -> juce::String {
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
        auto chooser = std::make_shared<juce::FileChooser>("Load Sample", juce::File(),
                                                           "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, padIndex, chooser, getDrumGrid, updatePadFromChain](const juce::FileChooser&) {
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

    drumGridUI_->onAnalyzePadRoleRequested = [this, cb = callbacks, getDrumGrid](int padIndex) {
        auto* dg = getDrumGrid();
        if (!dg)
            return;

        if (!cb.getNodePath)
            return;
        auto nodePath = cb.getNodePath();
        if (!nodePath.isValid())
            return;

        daw::audio::MagdaSamplerPlugin* sampler = nullptr;
        const int pluginCount = dg->getPadPluginCount(padIndex);
        for (int i = 0; i < pluginCount; ++i) {
            if (auto* plugin = dg->getPadPlugin(padIndex, i)) {
                sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin);
                if (sampler != nullptr)
                    break;
            }
        }

        if (sampler == nullptr || !sampler->getSampleFile().existsAsFile()) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Analyze pad role",
                                                   "This pad does not have a loaded sample.");
            return;
        }

        auto& mediaCtx = magda::media::MediaDbContext::getInstance();
        if (!mediaCtx.isAudioEncoderLoaded() || !mediaCtx.isTextEncoderLoaded() ||
            !mediaCtx.isTokenizerLoaded()) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Analyze pad role",
                "Sample Analyzer models are not loaded. Load them first, then run this "
                "manual analysis again.");
            return;
        }

        auto* audioEncoder = mediaCtx.audioEncoder();
        auto* textEncoder = mediaCtx.textEncoder();
        auto* tokenizer = mediaCtx.tokenizer();
        if (audioEncoder == nullptr || textEncoder == nullptr || tokenizer == nullptr)
            return;

        const auto file = sampler->getSampleFile();
        const double startSeconds = sampler->sampleStartParam != nullptr
                                        ? sampler->sampleStartParam->getCurrentValue()
                                        : 0.0;
        const double endSeconds = sampler->sampleEndParam != nullptr
                                      ? sampler->sampleEndParam->getCurrentValue()
                                      : sampler->getSampleLengthSeconds();
        const auto trackId = nodePath.trackId;
        const auto deviceId = nodePath.getDeviceId();
        const int noteNumber = daw::audio::DrumGridPlugin::baseNote + padIndex;
        const juce::Component::SafePointer<DrumGridUI> safeUi(drumGridUI_.get());

        std::thread([safeUi, file, startSeconds, endSeconds, trackId, deviceId, noteNumber,
                     padIndex, audioEncoder, textEncoder, tokenizer]() {
            auto result = classifyDrumRole(file, startSeconds, endSeconds, *audioEncoder,
                                           *textEncoder, *tokenizer);

            juce::MessageManager::callAsync([safeUi, result, trackId, deviceId, noteNumber,
                                             padIndex]() {
                if (safeUi == nullptr)
                    return;

                if (!result.ok) {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                           "Analyze pad role", result.error);
                    return;
                }

                auto roleLabel = daw::audio::drum_grid_roles::displayLabelForRole(result.roleId);
                if (roleLabel.isEmpty())
                    roleLabel = result.roleId;

                auto& tm = magda::TrackManager::getInstance();
                tm.setDeviceKitRowLabel(trackId, deviceId, noteNumber, roleLabel);
                tm.setDeviceKitRowRole(trackId, deviceId, noteNumber, result.roleId);

                DBG("DrumGridUI: analyzed pad " << padIndex << " as " << result.roleId
                                                << " score=" << result.score);
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::InfoIcon, "Analyze pad role",
                    "Pad " + juce::String(padIndex) + " set to " + roleLabel + ".");
            });
        }).detach();
    };

    // Pad swap via drag-and-drop
    drumGridUI_->onPadsSwapped = [this, getDrumGrid, updatePadFromChain](int srcPad, int dstPad) {
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

        for (int pluginIndex = 0; pluginIndex < static_cast<int>(chain->plugins.size());
             ++pluginIndex) {
            auto& plugin = chain->plugins[static_cast<size_t>(pluginIndex)];
            if (!plugin)
                continue;
            PadChainPanel::PluginSlotInfo info;
            info.plugin = plugin.get();
            info.deviceId = dg->getPluginDeviceId(chain->index, pluginIndex);
            info.isSampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get()) != nullptr;
            info.name = plugin->getName();
            result.push_back(info);
        }
        return result;
    };

    // FX plugin drop onto chain area
    padChain.onPluginDropped = [this, getDrumGrid, updatePadFromChain](
                                   int padIndex, const juce::DynamicObject& obj, int insertIdx) {
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
    padChain.onPluginRemoved = [getDrumGrid, updatePadFromChain](int padIndex, int pluginIndex) {
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
        auto chooser = std::make_shared<juce::FileChooser>("Load Sample", juce::File(),
                                                           "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, padIndex, chooser, getDrumGrid, updatePadFromChain](const juce::FileChooser&) {
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
        std::vector<InternalFxEntry> internals;
        addInternalFxEntry(internals, "Equaliser", "eq");
        addCompiledInternalFxEntry(internals, "Compressor");
        addInternalFxEntry(internals, "Reverb", "reverb");
        addInternalFxEntry(internals, "Delay", "delay");
        addInternalFxEntry(internals, "Chorus", "chorus");
        addCompiledInternalFxEntry(internals, "Phaser");
        addInternalFxEntry(internals, "Filter", "lowpass");
        addInternalFxEntry(internals, "Pitch Shift", "pitchshift");
        addInternalFxEntry(internals, "IR Reverb", "impulseresponse");
        addCompiledInternalFxEntry(internals, "Utility");
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
        auto capturedInternals =
            std::make_shared<std::vector<InternalFxEntry>>(std::move(internals));

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

    parent.addAndMakeVisible(*drumGridUI_);
    update(device);
    return true;
}

void DeviceCustomUIManager::create(const magda::DeviceInfo& device, juce::Component* parent,
                                   const Callbacks& callbacks) {
    if (device.pluginId.containsIgnoreCase("tone")) {
        createToneGeneratorUI(device, *parent, callbacks);
    } else if (createSamplerUI(device, *parent, callbacks)) {
        // handled by helper
    } else if (createDrumGridUI(device, *parent, callbacks)) {
        // handled by helper
    } else if (createFourOscUI(device, *parent, callbacks)) {
        // handled by helper
    } else if (createCustomInstrumentUI(device, *parent, callbacks)) {
        // handled by helper
    } else if (createSimpleEffectUI(device, *parent, callbacks)) {
        // handled by helper
    } else if (createImpulseResponseUI(device, *parent, callbacks)) {
        // handled by helper
    } else if (!createMidiUtilityUI(device, *parent)) {
        createAnalyzerUI(device, *parent);
    }
}

void DeviceCustomUIManager::setDevicePath(const magda::ChainNodePath& path) {
    devicePath_ = path;
    // create() bound the analyzer UIs while the path was still invalid; now that
    // it is set, resolve their plugin for real.
    refreshLivePluginBindings();

    // 4OSC's modulation destination dropdown is built from the live TE plugin,
    // and create() can run before the slot has a valid path. Repopulate it once
    // the path is bound so LFO/Mod Env destination lists are not left empty.
    if (fourOscUI_ && devicePath_.isValid())
        readAndPushModMatrix(devicePath_.getDeviceId());
}

void DeviceCustomUIManager::refreshLivePluginBindings() {
    bindAnalyzerPlugins();

    if (faustInstrumentUI_ != nullptr) {
        faustInstrumentUI_->setDevicePath(devicePath_);
        magda::daw::audio::IFaustEditorModel* model = nullptr;
        if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine())
            if (auto* bridge = audioEngine->getAudioBridge())
                if (auto plugin = bridge->getPlugin(devicePath_))
                    model = dynamic_cast<magda::daw::audio::IFaustEditorModel*>(plugin.get());
        faustInstrumentUI_->setPlugin(model);
    }
}

void DeviceCustomUIManager::bindAnalyzerPlugins() {
    if (oscilloscopeUI_ == nullptr && spectrumAnalyzerUI_ == nullptr && levelsUI_ == nullptr &&
        nimbusUI_ == nullptr)
        return;
    auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
    if (audioEngine == nullptr)
        return;
    auto* bridge = audioEngine->getAudioBridge();
    if (bridge == nullptr)
        return;
    auto plugin = bridge->getPlugin(devicePath_);
    if (oscilloscopeUI_ != nullptr)
        if (auto* scope = dynamic_cast<daw::audio::OscilloscopePlugin*>(plugin.get()))
            oscilloscopeUI_->setPlugin(scope);
    if (spectrumAnalyzerUI_ != nullptr)
        if (auto* sa = dynamic_cast<daw::audio::SpectrumAnalyzerPlugin*>(plugin.get())) {
            spectrumAnalyzerUI_->setPlugin(sa);
            spectrumAnalyzerUI_->setTrackId(devicePath_.trackId);  // enables masking overlay
        }
    if (levelsUI_ != nullptr)
        if (auto* lv = dynamic_cast<daw::audio::LevelsPlugin*>(plugin.get()))
            levelsUI_->setPlugin(lv);
    if (nimbusUI_ != nullptr)
        if (auto* cl = dynamic_cast<daw::audio::MutableCloudsPlugin*>(plugin.get()))
            nimbusUI_->setPlugin(cl);
}

// =============================================================================
// update
// =============================================================================

void DeviceCustomUIManager::update(const magda::DeviceInfo& device) {
    if (toneGeneratorUI_ && device.pluginId.containsIgnoreCase("tone")) {
        float frequency = 440.0f;
        float level = -12.0f;
        int waveform = 0;

        // ToneGeneratorProcessor exposes params in TE order: 0=oscType, 1=bandLimit,
        // 2=frequency, 3=level. Match that here.
        if (device.parameters.size() >= 4) {
            waveform = static_cast<int>(device.parameters[0].currentValue);
            frequency = device.parameters[2].currentValue;
            level = device.parameters[3].currentValue;
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
                auto plugin = bridge->getPlugin(devicePath_);
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
                auto plugin = bridge->getPlugin(devicePath_);
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
                auto plugin = bridge->getPlugin(devicePath_);
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

    if (faustInstrumentUI_ &&
        device.pluginId.equalsIgnoreCase(daw::audio::FaustInstrumentPlugin::xmlTypeName)) {
        faustInstrumentUI_->updateFromParameters(device.parameters);
    }

    if (polySynthUI_ && device.pluginId.equalsIgnoreCase("magda_polysynth")) {
        polySynthUI_->updateFromParameters(device.parameters);
    }

    if (fmUI_ && device.pluginId.equalsIgnoreCase("magda_fm")) {
        fmUI_->updateFromParameters(device.parameters);
    }

    if (materiaUI_ && device.pluginId.equalsIgnoreCase("magda_elements")) {
        materiaUI_->updateFromParameters(device.parameters);
    }

    if (haloUI_ && device.pluginId.equalsIgnoreCase("magda_rings")) {
        haloUI_->updateFromParameters(device.parameters);
    }

    if (nimbusUI_ && device.pluginId.equalsIgnoreCase("magda_clouds")) {
        nimbusUI_->updateFromParameters(device.parameters);
    }

    if (drumVoiceUI_ && DrumVoiceUI::handles(device.pluginId)) {
        drumVoiceUI_->updateFromParameters(device.parameters);
    }

    if (eqUI_ && device.pluginId.equalsIgnoreCase("eq")) {
        eqUI_->updateFromParameters(device.parameters);
    }

    if (compressorUI_ && isLegacyTeCompressorPluginId(device.pluginId)) {
        compressorUI_->updateFromParameters(device.parameters);
    }

    if (reverbUI_ && device.pluginId.containsIgnoreCase("reverb") &&
        !shouldSuppressLegacyUi(device.pluginId, LegacyUiKind::Reverb)) {
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
                auto plugin = bridge->getPlugin(devicePath_);
                if (auto* ir = dynamic_cast<te::ImpulseResponsePlugin*>(plugin.get())) {
                    impulseResponseUI_->setIRName(ir->name.get());
                }
            }
        }
    }
}

}  // namespace magda::daw::ui
