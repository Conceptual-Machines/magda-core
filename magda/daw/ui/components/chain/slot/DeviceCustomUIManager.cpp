#include "slot/DeviceCustomUIManager.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>
#include <tuple>

#include "audio/AudioBridge.hpp"
#include "audio/plugins/ArpeggiatorPlugin.hpp"
#include "audio/plugins/DrumGridPlugin.hpp"
#include "audio/plugins/DrumGridRoles.hpp"
#include "audio/plugins/InternalPluginRegistry.hpp"
#include "audio/plugins/MagdaConvolutionPlugin.hpp"
#include "audio/plugins/MagdaSamplerPlugin.hpp"
#include "audio/plugins/MidiChordEnginePlugin.hpp"
#include "audio/plugins/MidiStrumPlugin.hpp"
#include "audio/plugins/OscilloscopePlugin.hpp"
#include "audio/plugins/PolyStepSequencerPlugin.hpp"
#include "audio/plugins/SidechainPlugin.hpp"
#include "audio/plugins/SpectrumAnalyzerPlugin.hpp"
#include "audio/plugins/StepSequencerPlugin.hpp"
#include "audio/plugins/compiled/CompiledPluginRegistry.hpp"
#include "audio/plugins/compiled/MagdaCompiledPolyInstrument.hpp"
#include "audio/plugins/compiled/MagdaPolySynthCompiledPlugin.hpp"
#include "audio/plugins/mutable/MutableCloudsPlugin.hpp"
#include "audio/plugins/tracktion/TracktionMagdaDevicePlugin.hpp"
#include "audio/processors/DeviceProcessorFactory.hpp"
#include "audio/processors/base/DeviceProcessor.hpp"
#include "compiled/CompiledPluginPresentation.hpp"
#include "core/DeviceStateCommands.hpp"
#include "core/DrumGridPads.hpp"
#include "core/MidiFileWriter.hpp"
#include "core/PadCommands.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"
#include "custom_ui/ArpeggiatorUI.hpp"
#include "custom_ui/DrumVoiceUI.hpp"
#include "custom_ui/FMUI.hpp"
#include "custom_ui/FourOscUI.hpp"
#include "custom_ui/HaloUI.hpp"
#include "custom_ui/ImpulseResponseUI.hpp"
#include "custom_ui/LevelsUI.hpp"
#include "custom_ui/MateriaUI.hpp"
#include "custom_ui/NimbusUI.hpp"
#include "custom_ui/OscilloscopeUI.hpp"
#include "custom_ui/PluginTelemetrySources.hpp"
#include "custom_ui/PolyStepSequencerUI.hpp"
#include "custom_ui/PolySynthUI.hpp"
#include "custom_ui/SamplerUI.hpp"
#include "custom_ui/SidechainUI.hpp"
#include "custom_ui/SpectrumAnalyzerUI.hpp"
#include "custom_ui/StepSequencerUI.hpp"
#include "custom_ui/StruckInstrumentUI.hpp"
#include "custom_ui/StrumUI.hpp"
#include "custom_ui/ToneGeneratorUI.hpp"
#include "drum_grid/DrumGridUI.hpp"
#include "engine/AudioEngine.hpp"
#include "media_db/ClapAudioEncoder.hpp"
#include "media_db/ClapTextEncoder.hpp"
#include "media_db/MediaDbContext.hpp"
#include "media_db/RobertaTokenizer.hpp"
#include "processors/internal/NativeDeviceProcessors.hpp"
#include "project/ProjectManager.hpp"
#include "slot/ExternalInsertUI.hpp"
#include "slot/StepSequencerClipExport.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"
#include "ui/components/mixer/MidiNoteStrip.hpp"
#include "ui/panels/content/ChordPanelContent.hpp"
#include "ui/panels/content/PluginBrowserContent.hpp"

namespace magda::daw::ui {

namespace {

const juce::Identifier kSamplerSetLoopEnabled{"samplerSetLoopEnabled"};
const juce::Identifier kSamplerSetRootNote{"samplerSetRootNote"};
const juce::Identifier kSamplerGetPlaybackPosition{"samplerGetPlaybackPosition"};
const juce::Identifier kSamplerLoadSample{"samplerLoadSample"};
const juce::Identifier kImpulseResponseLoadFile{"impulseResponseLoadFile"};
const juce::Identifier kStepSequencerRandomizePattern{"stepSequencerRandomizePattern"};
const juce::Identifier kPolyStepSequencerRandomizePattern{"polyStepSequencerRandomizePattern"};
const juce::Identifier kStepSequencerGetStepRecording{"stepSequencerGetStepRecording"};
const juce::Identifier kPolyStepSequencerGetStepRecording{"polyStepSequencerGetStepRecording"};
const juce::Identifier kStepSequencerSetStepRecording{"stepSequencerSetStepRecording"};
const juce::Identifier kPolyStepSequencerSetStepRecording{"polyStepSequencerSetStepRecording"};

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

bool isDrumGridPluginId(const juce::String& pluginId) {
    return pluginId.equalsIgnoreCase(daw::audio::DrumGridPlugin::xmlTypeName);
}

bool isInstrumentDrop(const juce::DynamicObject& obj) {
    if (obj.hasProperty("browserIsInstrument"))
        return static_cast<bool>(obj.getProperty("browserIsInstrument"));
    return static_cast<bool>(obj.getProperty("isInstrument"));
}

bool isMidiFxDrop(const juce::DynamicObject& obj) {
    return obj.getProperty("categoryOverride").toString().equalsIgnoreCase("MIDI FX") ||
           obj.getProperty("subcategory").toString().equalsIgnoreCase("MIDI") ||
           obj.getProperty("rawSubcategory").toString().equalsIgnoreCase("MIDI");
}

bool isMidiFxPlugin(const PluginBrowserInfo& plugin) {
    return plugin.categoryOverride.equalsIgnoreCase("MIDI FX") ||
           plugin.subcategory.equalsIgnoreCase("MIDI");
}

bool isMidiFxPlugin(const juce::PluginDescription& desc) {
    return desc.category.equalsIgnoreCase("MIDI");
}

template <typename Plugin>
void refreshSingleNoteStripFromPlugin(Plugin* plugin, magda::MidiNoteStrip& strip, int& lastNote) {
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

bool pluginDescriptionMatchesDrop(const juce::PluginDescription& desc, const juce::String& fileOrId,
                                  const juce::String& uniqueId) {
    return desc.fileOrIdentifier == fileOrId ||
           (uniqueId.isNotEmpty() &&
            (desc.createIdentifierString() == uniqueId || juce::String(desc.uniqueId) == uniqueId));
}

class CallbackDeviceParameterController final : public magda::DeviceParameterController {
  public:
    CallbackDeviceParameterController(magda::DeviceInfo device,
                                      std::function<void(int, float)> onParameterChanged)
        : device_(std::move(device)), onParameterChanged_(std::move(onParameterChanged)) {}

    std::vector<magda::ParameterInfo> parameters() const override {
        return device_.parameters;
    }

    const magda::ParameterInfo* findParameterByIndex(int paramIndex) const override {
        return device_.findParameterByIndex(paramIndex);
    }

    void setParameterNormalised(int paramIndex, float value) override {
        if (onParameterChanged_)
            onParameterChanged_(paramIndex, value);
    }

    void setDevice(magda::DeviceInfo device) {
        device_ = std::move(device);
    }

  private:
    magda::DeviceInfo device_;
    std::function<void(int, float)> onParameterChanged_;
};

class CallbackDeviceCommandController final : public magda::DeviceCommandController {
  public:
    explicit CallbackDeviceCommandController(
        std::function<juce::var(const juce::Identifier&, const juce::var&)> execute)
        : execute_(std::move(execute)) {}

    juce::var executeCommand(const juce::Identifier& command,
                             const juce::var& arguments = {}) override {
        return execute_ ? execute_(command, arguments) : juce::var{};
    }

  private:
    std::function<juce::var(const juce::Identifier&, const juce::var&)> execute_;
};

void writeParameterChange(const DeviceCustomUIManager::Callbacks& callbacks, int paramIndex,
                          float value) {
    if (callbacks.deviceUiContext != nullptr) {
        if (!callbacks.deviceUiContext->isValid())
            return;
        if (auto* parameters = callbacks.deviceUiContext->parameters()) {
            parameters->setParameterNormalised(paramIndex, value);
            return;
        }
    }

    // A custom context may be valid but intentionally omit a parameter
    // controller; keep the slot callback as the fallback write path.
    if (callbacks.onParameterChanged)
        callbacks.onParameterChanged(paramIndex, value);
}

juce::var executeDeviceCommand(const DeviceCustomUIManager::Callbacks& callbacks,
                               const juce::Identifier& command, const juce::var& arguments = {}) {
    if (callbacks.deviceUiContext != nullptr) {
        if (!callbacks.deviceUiContext->isValid())
            return {};
        if (auto* commands = callbacks.deviceUiContext->commands())
            return commands->executeCommand(command, arguments);
    }

    return {};
}

template <typename Ui>
void forwardParameterChanges(Ui& ui, const DeviceCustomUIManager::Callbacks& callbacks) {
    ui.onParameterChanged = [cb = callbacks](int paramIndex, float value) {
        writeParameterChange(cb, paramIndex, value);
    };
}

magda::PluginFormat pluginFormatFromDescription(const juce::PluginDescription& desc) {
    return magda::pluginFormatFromName(desc.pluginFormatName);
}

magda::DeviceInfo projectPadPluginDevice(magda::DeviceId deviceId,
                                         tracktion::engine::Plugin::Ptr plugin) {
    magda::DeviceInfo device;
    device.id = deviceId;
    device.name = plugin ? plugin->getName() : juce::String();
    device.pluginId = plugin ? plugin->getPluginType() : juce::String();
    device.format = magda::PluginFormat::Internal;
    device.isInstrument = plugin != nullptr && plugin->isSynth();
    device.deviceType =
        device.isInstrument ? magda::DeviceType::Instrument : magda::DeviceType::Effect;
    device.bypassed = plugin != nullptr && !plugin->isEnabled();

    if (auto* ext = dynamic_cast<tracktion::engine::ExternalPlugin*>(plugin.get())) {
        device.format = pluginFormatFromDescription(ext->desc);
        device.name = ext->desc.name.isNotEmpty() ? ext->desc.name : device.name;
        device.pluginId = ext->desc.createIdentifierString();
        device.manufacturer = ext->desc.manufacturerName;
        device.uniqueId = ext->desc.createIdentifierString();
        device.fileOrIdentifier = ext->desc.fileOrIdentifier;
        device.isInstrument = ext->desc.isInstrument;
        device.deviceType =
            device.isInstrument ? magda::DeviceType::Instrument : magda::DeviceType::Effect;
    } else if (auto* internalSpec =
                   daw::audio::findInternalPluginSpecForLoadType(device.pluginId)) {
        device.pluginId = internalSpec->pluginId;
        device.name =
            internalSpec->displayName != nullptr ? internalSpec->displayName : device.name;
        device.isInstrument = internalSpec->isInstrument || device.isInstrument;
        device.deviceType =
            device.isInstrument ? magda::DeviceType::Instrument : magda::DeviceType::Effect;
    }

    if (auto processor = magda::createDeviceProcessorForPlugin(device.id, plugin, device.pluginId))
        processor->populateParameters(device, magda::DeviceProcessor::ValueSource::Engine);

    return device;
}

/// The device an internal plugin id names.
///
/// Both registries, because a compiled device (the drum voices, the Faust FX)
/// is not in the internal one: asking only there would have every compiled pad
/// voice arrive as an effect, and an effect does not replace the pad.
magda::DeviceInfo internalPadDevice(const juce::String& pluginId, const juce::String& displayName) {
    magda::DeviceInfo device;
    device.pluginId = pluginId;
    device.name = displayName.isNotEmpty() ? displayName : pluginId;
    device.format = magda::PluginFormat::Internal;

    if (const auto* compiledSpec = daw::audio::compiled::findCompiledPluginSpec(pluginId)) {
        if (compiledSpec->pluginId != nullptr)
            device.pluginId = compiledSpec->pluginId;
        if (displayName.isEmpty() && compiledSpec->displayName != nullptr)
            device.name = compiledSpec->displayName;
        device.isInstrument = compiledSpec->isInstrument;
    } else if (const auto* spec = daw::audio::findInternalPluginSpec(pluginId)) {
        if (spec->pluginId != nullptr)
            device.pluginId = spec->pluginId;
        if (displayName.isEmpty() && spec->displayName != nullptr)
            device.name = spec->displayName;
        device.isInstrument = spec->isInstrument;
    }

    device.deviceType =
        device.isInstrument ? magda::DeviceType::Instrument : magda::DeviceType::Effect;
    return device;
}

/// The device a scanned external plugin names.
magda::DeviceInfo externalPadDevice(const juce::PluginDescription& desc) {
    magda::DeviceInfo device;
    device.name = desc.name;
    device.manufacturer = desc.manufacturerName;
    device.uniqueId = desc.createIdentifierString();
    device.pluginId = device.uniqueId;
    device.fileOrIdentifier = desc.fileOrIdentifier;
    device.format = pluginFormatFromDescription(desc);
    device.isInstrument = desc.isInstrument;
    device.deviceType =
        device.isInstrument ? magda::DeviceType::Instrument : magda::DeviceType::Effect;
    return device;
}

}  // namespace

DeviceCustomUIManager::DeviceCustomUIManager() = default;
DeviceCustomUIManager::~DeviceCustomUIManager() {
    detachFromLivePlugin();
}

// =============================================================================
// Queries
// =============================================================================

juce::Component* DeviceCustomUIManager::getActiveUI() const {
    if (toneGeneratorUI_)
        return toneGeneratorUI_.get();
    if (externalInsertUI_)
        return externalInsertUI_.get();
    if (samplerUI_)
        return samplerUI_.get();
    if (drumGridUI_)
        return drumGridUI_.get();
    if (fourOscUI_)
        return fourOscUI_.get();
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
    if (struckUI_)
        return struckUI_.get();
    if (sidechainUI_)
        return sidechainUI_.get();
    if (impulseResponseUI_)
        return impulseResponseUI_.get();
    if (chordEngineUI_)
        return chordEngineUI_.get();
    if (arpeggiatorUI_)
        return arpeggiatorUI_.get();
    if (strumUI_)
        return strumUI_.get();
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
    if (fourOscUI_)
        return fourOscUI_->getLinkableSliders();
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
    if (struckUI_)
        return struckUI_->getLinkableSliders();
    if (toneGeneratorUI_)
        return toneGeneratorUI_->getLinkableSliders();
    if (sidechainUI_)
        return sidechainUI_->getLinkableSliders();
    if (impulseResponseUI_)
        return impulseResponseUI_->getLinkableSliders();
    if (samplerUI_)
        return samplerUI_->getLinkableSliders();
    if (arpeggiatorUI_)
        return arpeggiatorUI_->getLinkableSliders();
    if (strumUI_)
        return strumUI_->getLinkableSliders();
    if (stepSequencerUI_)
        return stepSequencerUI_->getLinkableSliders();
    if (polyStepSequencerUI_)
        return polyStepSequencerUI_->getLinkableSliders();
    return {};
}

bool DeviceCustomUIManager::hasAnyUI() const {
    return externalInsertUI_ || toneGeneratorUI_ || samplerUI_ || drumGridUI_ || fourOscUI_ ||
           polySynthUI_ || fmUI_ || materiaUI_ || haloUI_ || nimbusUI_ || struckUI_ ||
           drumVoiceUI_ || impulseResponseUI_ || sidechainUI_ || chordEngineUI_ || arpeggiatorUI_ ||
           strumUI_ || stepSequencerUI_ || polyStepSequencerUI_ || oscilloscopeUI_ ||
           spectrumAnalyzerUI_ || levelsUI_;
}

int DeviceCustomUIManager::getPreferredContentWidth(int drumGridFallback) const {
    if (fourOscUI_)
        return 500;
    if (polySynthUI_)
        return 860;  // four oscillator columns + filter + stacked envelope column
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
    if (struckUI_)
        return struckUI_->preferredContentWidth();  // body panel + EXCITER | RESONATOR
    if (sidechainUI_)
        return 500;  // curve editor + depth/sync/mode/atk/rel control row
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
    if (externalInsertUI_)
        return 360;  // two device pickers + manual-latency field
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

tracktion::engine::Plugin::Ptr DeviceCustomUIManager::getLivePlugin() const {
    if (livePluginProvider_) {
        if (auto plugin = livePluginProvider_())
            return plugin;
    }

    if (auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine()) {
        if (auto* bridge = audioEngine->getAudioBridge())
            return bridge->getPlugin(devicePath_);
    }

    return {};
}

bool DeviceCustomUIManager::randomizeSequencerPattern(bool polyphonic) {
    if (deviceUiContext_ == nullptr || !deviceUiContext_->isValid())
        return false;
    auto* commands = deviceUiContext_->commands();
    if (commands == nullptr)
        return false;
    return static_cast<bool>(commands->executeCommand(
        polyphonic ? kPolyStepSequencerRandomizePattern : kStepSequencerRandomizePattern));
}

std::optional<bool> DeviceCustomUIManager::toggleSequencerStepRecording(bool polyphonic) {
    if (deviceUiContext_ == nullptr || !deviceUiContext_->isValid())
        return std::nullopt;
    auto* commands = deviceUiContext_->commands();
    if (commands == nullptr)
        return std::nullopt;

    const auto getCommand =
        polyphonic ? kPolyStepSequencerGetStepRecording : kStepSequencerGetStepRecording;
    const auto setCommand =
        polyphonic ? kPolyStepSequencerSetStepRecording : kStepSequencerSetStepRecording;
    const bool enabled = !static_cast<bool>(commands->executeCommand(getCommand));
    return static_cast<bool>(commands->executeCommand(setCommand, enabled))
               ? std::optional<bool>{enabled}
               : std::nullopt;
}

void DeviceCustomUIManager::copySequencerPatternToClipboard(bool polyphonic) {
    // Clipboard/export gestures are UI-specific operations that need the concrete
    // plugin helpers rather than the generic command surface used for state
    // mutations.
    auto plugin = getLivePlugin();
    if (polyphonic) {
        if (auto* sequencer = dynamic_cast<daw::audio::PolyStepSequencerPlugin*>(plugin.get()))
            copyPolyStepSequencerPatternToClipboard(*sequencer);
        return;
    }

    if (auto* sequencer = dynamic_cast<daw::audio::StepSequencerPlugin*>(plugin.get()))
        copyStepSequencerPatternToClipboard(*sequencer);
}

bool DeviceCustomUIManager::handleSequencerPatternExternalDrag(bool polyphonic,
                                                               juce::Component* exportButton,
                                                               juce::Component* dragOwner,
                                                               const juce::MouseEvent& event) {
    auto plugin = getLivePlugin();
    if (polyphonic) {
        return handlePolyStepSequencerPatternExternalDrag(
            dynamic_cast<daw::audio::PolyStepSequencerPlugin*>(plugin.get()), exportButton,
            dragOwner, event);
    }

    return handleStepSequencerPatternExternalDrag(
        dynamic_cast<daw::audio::StepSequencerPlugin*>(plugin.get()), exportButton, dragOwner,
        event);
}

bool DeviceCustomUIManager::getSequencerStepRecordingState(bool polyphonic, int& position,
                                                           int& maxSteps) const {
    const auto getCommand =
        polyphonic ? kPolyStepSequencerGetStepRecording : kStepSequencerGetStepRecording;
    bool commandHandled = false;
    if (deviceUiContext_ != nullptr && deviceUiContext_->isValid()) {
        if (auto* commands = deviceUiContext_->commands()) {
            commandHandled = true;
            if (!static_cast<bool>(commands->executeCommand(getCommand)))
                return false;
        }
    }

    // Position and range are display-only details that are not part of the
    // command mutation surface.
    auto plugin = getLivePlugin();
    if (polyphonic) {
        auto* sequencer = dynamic_cast<daw::audio::PolyStepSequencerPlugin*>(plugin.get());
        if (sequencer == nullptr || (!commandHandled && !sequencer->isStepRecording()))
            return false;
        position = sequencer->stepRecordPosition_.load(std::memory_order_relaxed);
        maxSteps = juce::jlimit(1, 32, static_cast<int>(sequencer->numSteps.get()));
        return true;
    }

    auto* sequencer = dynamic_cast<daw::audio::StepSequencerPlugin*>(plugin.get());
    if (sequencer == nullptr || (!commandHandled && !sequencer->isStepRecording()))
        return false;
    position = sequencer->stepRecordPosition_.load(std::memory_order_relaxed);
    maxSteps = juce::jlimit(1, 32, static_cast<int>(sequencer->numSteps.get()));
    return true;
}

void DeviceCustomUIManager::refreshArpeggiatorMidiActivity(magda::MidiNoteStrip& strip,
                                                           int& lastNote) const {
    refreshSingleNoteStripFromPlugin(arpPlugin_, strip, lastNote);
}

void DeviceCustomUIManager::refreshStrumMidiActivity(magda::MidiNoteStrip& strip,
                                                     int& lastNote) const {
    refreshSingleNoteStripFromPlugin(strumPlugin_, strip, lastNote);
}

void DeviceCustomUIManager::refreshStepSequencerMidiActivity(magda::MidiNoteStrip& strip,
                                                             int& lastNote) const {
    refreshSingleNoteStripFromPlugin(stepSeqPlugin_, strip, lastNote);
}

void DeviceCustomUIManager::refreshPolyStepSequencerMidiActivity(magda::MidiNoteStrip& strip,
                                                                 int& lastNote) const {
    refreshSingleNoteStripFromPlugin(polyStepSeqPlugin_, strip, lastNote);
}

void DeviceCustomUIManager::refreshChordEngineMidiActivity(magda::MidiNoteStrip& strip,
                                                           std::array<int, 32>& lastChordNotes,
                                                           int& lastChordCount) const {
    if (chordPlugin_ == nullptr)
        return;

    const int count = chordPlugin_->getHeldNoteCount();
    for (int i = 0; i < lastChordCount; ++i)
        strip.clearNote(lastChordNotes[static_cast<size_t>(i)]);

    for (int i = 0; i < count && i < static_cast<int>(lastChordNotes.size()); ++i) {
        const int note = chordPlugin_->getHeldNote(i);
        lastChordNotes[static_cast<size_t>(i)] = note;
        strip.setNote(note, 100);
    }
    lastChordCount = count;
}

// =============================================================================
// readAndPushModMatrix
// =============================================================================

void DeviceCustomUIManager::readAndPushModMatrix(magda::DeviceId /*deviceId*/) {
    if (!fourOscUI_)
        return;
    auto plugin = getLivePlugin();
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
    if (polySynthUI_ && device.pluginId.equalsIgnoreCase("magda_polysynth"))
        polySynthUI_->updateFromParameters(device.parameters);
    if (drumVoiceUI_ && DrumVoiceUI::handles(device.pluginId))
        drumVoiceUI_->updateFromParameters(device.parameters);
    if (struckUI_ && StruckInstrumentUI::handles(device.pluginId))
        struckUI_->updateFromParameters(device.parameters);
    if (fmUI_ && device.pluginId.equalsIgnoreCase("magda_fm"))
        fmUI_->updateFromParameters(device.parameters);
    if (materiaUI_ && device.pluginId.equalsIgnoreCase("magda_elements"))
        materiaUI_->updateFromParameters(device.parameters);
    if (haloUI_ && device.pluginId.equalsIgnoreCase("magda_rings"))
        haloUI_->updateFromParameters(device.parameters);
    if (nimbusUI_ && device.pluginId.equalsIgnoreCase("magda_clouds"))
        nimbusUI_->updateFromParameters(device.parameters);
    if (sidechainUI_ && device.pluginId.equalsIgnoreCase(daw::audio::SidechainPlugin::xmlTypeName))
        sidechainUI_->updateFromParameters(device.parameters);
    if (strumUI_ && device.pluginId.equalsIgnoreCase(daw::audio::MidiStrumPlugin::xmlTypeName))
        strumUI_->updateFromParameters(device.parameters);
    if (arpeggiatorUI_ &&
        device.pluginId.equalsIgnoreCase(daw::audio::ArpeggiatorPlugin::xmlTypeName))
        arpeggiatorUI_->updateFromParameters(device.parameters);
    if (impulseResponseUI_ && device.pluginId == daw::audio::MagdaConvolutionPlugin::xmlTypeName)
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
        writeParameterChange(cb, paramIndex, value);
    };

    samplerUI_->onLoopEnabledChanged = [cb = callbacks](bool enabled) {
        executeDeviceCommand(cb, kSamplerSetLoopEnabled, enabled);
    };

    samplerUI_->onRootNoteChanged = [cb = callbacks](int note) {
        executeDeviceCommand(cb, kSamplerSetRootNote, note);
    };

    samplerUI_->getPlaybackPosition = [cb = callbacks]() -> double {
        return static_cast<double>(executeDeviceCommand(cb, kSamplerGetPlaybackPosition));
    };

    // Shared logic for loading a sample file and refreshing the UI
    auto loadFile = [cb = callbacks](const juce::File& file) {
        executeDeviceCommand(cb, kSamplerLoadSample, file.getFullPathName());
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
                                                juce::Component& parent,
                                                const Callbacks& callbacks) {
    if (device.pluginId.containsIgnoreCase(daw::audio::MidiChordEnginePlugin::xmlTypeName)) {
        chordEngineUI_ = std::make_unique<ChordPanelContent>();
        parent.addAndMakeVisible(*chordEngineUI_);
        // Connect to the plugin instance
        if (auto plugin = getLivePlugin()) {
            if (auto* cp = dynamic_cast<daw::audio::MidiChordEnginePlugin*>(plugin.get())) {
                chordEngineUI_->setChordEngine(cp, magda::INVALID_TRACK_ID);
                chordPlugin_ = cp;
            }
        }
        return true;
    }

    if (device.pluginId.containsIgnoreCase(daw::audio::ArpeggiatorPlugin::xmlTypeName)) {
        arpeggiatorUI_ = std::make_unique<ArpeggiatorUI>();
        forwardParameterChanges(*arpeggiatorUI_, callbacks);
        // Non-slot settings are authored state: the edit patches the MODEL's
        // state document, and the projection updates the live device (#2317).
        // The model is what autosave writes and what both engines build from,
        // so the edit also dirties the project.
        arpeggiatorUI_->onSettingsEdited = [this](const juce::NamedValueSet& settings) {
            auto& tm = magda::TrackManager::getInstance();
            const bool changed = tm.updateDeviceAuthoredState(
                devicePath_, [&settings](magda::device_state::Doc& doc) {
                    for (int i = 0; i < settings.size(); ++i)
                        doc.root.props.set(settings.getName(i), settings.getValueAt(i));
                });
            if (changed)
                ProjectManager::getInstance().markDirty();
        };
        parent.addAndMakeVisible(*arpeggiatorUI_);
        if (auto plugin = getLivePlugin()) {
            if (auto* arp =
                    daw::audio::tracktion_adapter::deviceFromPlugin<daw::audio::ArpeggiatorPlugin>(
                        plugin.get())) {
                arpeggiatorUI_->setArpeggiator(arp);
                arpPlugin_ = arp;
            }
        }
        update(device);
        return true;
    }

    if (device.pluginId.containsIgnoreCase(daw::audio::MidiStrumPlugin::xmlTypeName)) {
        strumUI_ = std::make_unique<StrumUI>();
        forwardParameterChanges(*strumUI_, callbacks);
        parent.addAndMakeVisible(*strumUI_);
        if (auto plugin = getLivePlugin())
            strumPlugin_ =
                daw::audio::tracktion_adapter::deviceFromPlugin<daw::audio::MidiStrumPlugin>(
                    plugin.get());
        update(device);
        return true;
    }

    if (device.pluginId.containsIgnoreCase(daw::audio::PolyStepSequencerPlugin::xmlTypeName)) {
        // NB: checked before the mono sequencer — "polystepsequencer" also
        // contains "stepsequencer", so the order of these branches matters.
        polyStepSequencerUI_ = std::make_unique<PolyStepSequencerUI>();
        parent.addAndMakeVisible(*polyStepSequencerUI_);
        if (auto plugin = getLivePlugin()) {
            if (auto* seq = dynamic_cast<daw::audio::PolyStepSequencerPlugin*>(plugin.get())) {
                polyStepSequencerUI_->setPlugin(seq);
                polyStepSeqPlugin_ = seq;
            }
        }
        return true;
    }

    if (device.pluginId.containsIgnoreCase(daw::audio::StepSequencerPlugin::xmlTypeName)) {
        stepSequencerUI_ = std::make_unique<StepSequencerUI>();
        parent.addAndMakeVisible(*stepSequencerUI_);
        if (auto plugin = getLivePlugin()) {
            if (auto* seq = dynamic_cast<daw::audio::StepSequencerPlugin*>(plugin.get())) {
                stepSequencerUI_->setPlugin(seq);
                stepSeqPlugin_ = seq;
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
        writeParameterChange(cb, paramIndex, value);
    };
    fourOscUI_->onPluginStateChanged = [this](const juce::String& propertyId, juce::var value) {
        auto plugin = getLivePlugin();
        if (auto* fourOsc = dynamic_cast<te::FourOscPlugin*>(plugin.get()))
            fourOsc->state.setProperty(juce::Identifier(propertyId), value, nullptr);
    };
    fourOscUI_->onModDepthChanged = [this](int paramIndex, int modSourceId, float depth) {
        auto plugin = getLivePlugin();
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
        auto plugin = getLivePlugin();
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
    if (device.pluginId.equalsIgnoreCase("magda_polysynth")) {
        polySynthUI_ = std::make_unique<PolySynthUI>();
        forwardParameterChanges(*polySynthUI_, callbacks);
        parent.addAndMakeVisible(*polySynthUI_);
        refreshLivePluginBindings();
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

    if (StruckInstrumentUI::handles(device.pluginId)) {
        struckUI_ = std::make_unique<StruckInstrumentUI>(device.pluginId);
        forwardParameterChanges(*struckUI_, callbacks);
        parent.addAndMakeVisible(*struckUI_);
        refreshLivePluginBindings();  // bind for the note-on strike flash
        update(device);
        return true;
    }

    return false;
}

bool DeviceCustomUIManager::createSimpleEffectUI(const magda::DeviceInfo& device,
                                                 juce::Component& parent,
                                                 const Callbacks& callbacks) {
    if (device.pluginId.equalsIgnoreCase(daw::audio::SidechainPlugin::xmlTypeName)) {
        sidechainUI_ = std::make_unique<SidechainUI>();
        forwardParameterChanges(*sidechainUI_, callbacks);
        // The curve editor / depth / source picker address the live model by
        // path, so hand through the slot's live path getter.
        sidechainUI_->getNodePath = callbacks.getNodePath;
        parent.addAndMakeVisible(*sidechainUI_);
        update(device);
        return true;
    }

    return false;
}

bool DeviceCustomUIManager::createImpulseResponseUI(const magda::DeviceInfo& device,
                                                    juce::Component& parent,
                                                    const Callbacks& callbacks) {
    if (device.pluginId != daw::audio::MagdaConvolutionPlugin::xmlTypeName)
        return false;

    impulseResponseUI_ = std::make_unique<ImpulseResponseUI>();
    impulseResponseUI_->onParameterChanged = [cb = callbacks](int paramIndex, float value) {
        writeParameterChange(cb, paramIndex, value);
    };

    // Helper to load an IR file through the device command surface.
    auto loadIR = [cb = callbacks](const juce::File& file) {
        executeDeviceCommand(cb, kImpulseResponseLoadFile, file.getFullPathName());
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

    // Helper to get DrumGridPlugin pointer. Reads only: what a pad holds is
    // edited through TrackManager and reaches the plugin by sync (#2207).
    auto getDrumGrid = [this]() -> daw::audio::DrumGridPlugin* {
        auto plugin = getLivePlugin();
        return dynamic_cast<daw::audio::DrumGridPlugin*>(plugin.get());
    };

    // The grid's own path, resolved at callback time: the slot may not know it
    // yet when this UI is built.
    auto gridPath = [this, cb = callbacks]() -> magda::ChainNodePath {
        if (cb.getNodePath) {
            if (auto path = cb.getNodePath(); path.isValid())
                return path;
        }
        return devicePath_;
    };

    // A pad's fader, run now: the sound has to follow the mouse, and a fader
    // notifies trackPropertyChanged, which by design does not rebuild the chain
    // components. `SetPadFaderCommand` stores the one value it changed rather
    // than snapshotting the pad rack, which is what makes it cheap enough to
    // run per mouse move, and coalesces within a drag (#2211).
    auto padFader = [this, gridPath](int padIndex, magda::SetPadFaderCommand::Target target,
                                     float value) {
        const auto grid = gridPath();
        if (!grid.isValid() || drumGridUI_ == nullptr)
            return;
        magda::setPadFader(grid, padIndex, target, value, drumGridUI_->getFaderGesture());
    };

    // One undoable pad edit, posted rather than run now, with a follow-up that
    // runs only if there is still a UI to run it on.
    //
    // `EditPadsCommand` snapshots the grid's pad rack, so any edit -- one pad's
    // switch, two pads trading places, a whole chain replaced -- comes back in
    // one step. Everything reaching it is one command per click or per gesture,
    // so the snapshot is never taken per mouse move (#2211).
    //
    // Every pad edit but a fader notifies trackDevicesChanged, and the track's
    // chain rebuild that follows destroys this slot, this UI, and the row whose
    // button is still on the stack. Posting lets the gesture finish first. The
    // grid's path is resolved here rather than inside the post, because the
    // resolver reads this manager and the rebuild may have taken it by then.
    auto postPadEdit = [this, gridPath](const juce::String& description,
                                        std::function<void(const magda::ChainNodePath&)> edit,
                                        std::function<void()> then = {}) {
        const auto grid = gridPath();
        if (!grid.isValid())
            return;

        juce::MessageManager::callAsync(
            [safeUi = juce::Component::SafePointer<DrumGridUI>(drumGridUI_.get()), grid,
             description, edit = std::move(edit), then = std::move(then)]() {
                magda::editPads(grid, description, [grid, edit]() { edit(grid); });
                if (then && safeUi != nullptr)
                    then();
            });
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
                                       chain->index, chain->bypassed.get(), chain->busOutput.get());
        } else {
            drumGridUI_->updatePadInfo(padIndex, "", false, false, 0.0f, 0.0f, -1);
        }
    };

    // Loading a sample onto a pad is a model edit: it puts a sampler device on
    // the pad's chain, rooted on the pad's note. The plugin follows by sync.
    auto loadSampleToPad = [postPadEdit, getDrumGrid, updatePadFromChain](int padIndex,
                                                                          const juce::File& file) {
        postPadEdit(
            "Load Pad Sample",
            [padIndex, file](const magda::ChainNodePath& grid) {
                magda::TrackManager::getInstance().setPadDevice(
                    grid, padIndex,
                    magda::padSamplerDevice(file.getFullPathName(), magda::padNoteFor(padIndex)));
            },
            [padIndex, getDrumGrid, updatePadFromChain]() {
                if (auto* dg = getDrumGrid())
                    updatePadFromChain(dg, padIndex);
            });
    };

    // Sample drop callback
    drumGridUI_->onSampleDropped = [loadSampleToPad](int padIndex, const juce::File& file) {
        loadSampleToPad(padIndex, file);
    };

    // Load button callback (file chooser)
    drumGridUI_->onLoadRequested = [this, loadSampleToPad](int padIndex) {
        auto chooser = std::make_shared<juce::FileChooser>("Load Sample", juce::File(),
                                                           "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles,
                             [this, padIndex, chooser, loadSampleToPad](const juce::FileChooser&) {
                                 if (!drumGridUI_)
                                     return;
                                 auto result = chooser->getResult();
                                 if (result.existsAsFile())
                                     loadSampleToPad(padIndex, result);
                             });
    };

    // Clear callback
    drumGridUI_->onClearRequested = [this, postPadEdit](int padIndex) {
        postPadEdit(
            "Clear Pad",
            [padIndex](const magda::ChainNodePath& grid) {
                auto& tm = magda::TrackManager::getInstance();
                if (const auto* pad = tm.getPad(grid, padIndex))
                    tm.removePadChain(grid, pad->id);
            },
            [this, padIndex]() {
                drumGridUI_->updatePadInfo(padIndex, "", false, false, 0.0f, 0.0f, -1);
            });
    };

    // A pad's fader, pan and switches are its chain's, so they are set the way
    // any other chain's are.
    //
    // The faders are run now, not posted: they notify trackPropertyChanged,
    // which by design does not rebuild the chain, and a fader wants the sound
    // to move under the mouse. They coalesce into one undo step per drag.
    drumGridUI_->onPadLevelChanged = [padFader](int padIndex, float levelDb) {
        padFader(padIndex, magda::SetPadFaderCommand::Target::Volume, levelDb);
    };

    drumGridUI_->onPadPanChanged = [padFader](int padIndex, float pan) {
        padFader(padIndex, magda::SetPadFaderCommand::Target::Pan, pan);
    };

    drumGridUI_->onPadMuteChanged = [postPadEdit](int padIndex, bool muted) {
        postPadEdit(muted ? "Mute Pad" : "Unmute Pad",
                    [padIndex, muted](const magda::ChainNodePath& grid) {
                        magda::TrackManager::getInstance().setPadMuted(grid, padIndex, muted);
                    });
    };

    drumGridUI_->onPadSoloChanged = [postPadEdit](int padIndex, bool soloed) {
        postPadEdit(soloed ? "Solo Pad" : "Unsolo Pad",
                    [padIndex, soloed](const magda::ChainNodePath& grid) {
                        magda::TrackManager::getInstance().setPadSolo(grid, padIndex, soloed);
                    });
    };

    drumGridUI_->onPadBypassChanged = [postPadEdit](int padIndex, bool bypassed) {
        postPadEdit(bypassed ? "Disable Pad" : "Enable Pad",
                    [padIndex, bypassed](const magda::ChainNodePath& grid) {
                        magda::TrackManager::getInstance().setPadBypassed(grid, padIndex, bypassed);
                    });
    };

    // The pad's output bus. `ChainInfo::outputIndex` is model state, and the
    // device sync turns a pad on a bus into a multi-out child track, so the row
    // selector only ever had to write the model (#2211).
    drumGridUI_->onPadOutputChanged = [this, postPadEdit, gridPath](int padIndex, int busIndex) {
        // Refused for a grid inside a rack: nothing carries a bus off one, so
        // the pads on it would go silent. Asked before the edit, the same way a
        // range is, so a refusal snaps the row back to Main rather than leaving
        // it showing a bus the model never took, and does not become an undo
        // step that changed nothing.
        if (busIndex != 0 && !magda::TrackManager::getInstance().padBusesAvailable(gridPath())) {
            if (drumGridUI_ != nullptr)
                drumGridUI_->rebuildChainRows();
            return;
        }

        postPadEdit("Set Pad Output", [padIndex, busIndex](const magda::ChainNodePath& grid) {
            magda::TrackManager::getInstance().setPadOutput(grid, padIndex, busIndex);
        });
    };

    // The notes a pad answers to. A pad that answers to everything is a chain
    // built before pads were keyed by pitch; it reads as the pad's own note so
    // the row shows the range the grid actually plays it at.
    drumGridUI_->getNoteRange = [gridPath](int padIndex) -> std::tuple<int, int, int> {
        const int note = magda::padNoteFor(padIndex);
        const auto* pad = magda::TrackManager::getInstance().getPad(gridPath(), padIndex);
        if (pad == nullptr || pad->answersToEveryNote())
            return {note, note, note};
        return {pad->lowNote, pad->highNote, pad->rootNote};
    };

    drumGridUI_->onPadRangeChanged = [this, postPadEdit, gridPath](int padIndex, int lowNote,
                                                                   int highNote, int rootNote) {
        // A range has to stay inside the grid's own notes and clear of every
        // other pad's. Asked before the edit, so a refused range snaps the row
        // back rather than leaving it showing something the model never took,
        // and does not become an undo step that changed nothing.
        if (!magda::TrackManager::getInstance().padNoteRangeIsFree(gridPath(), padIndex, lowNote,
                                                                   highNote)) {
            if (drumGridUI_ != nullptr)
                drumGridUI_->refreshRangeRows();
            return;
        }

        postPadEdit("Set Pad Key Range",
                    [padIndex, lowNote, highNote, rootNote](const magda::ChainNodePath& grid) {
                        magda::TrackManager::getInstance().setPadNoteRange(grid, padIndex, lowNote,
                                                                           highNote, rootNote);
                    });
    };

    // Plugin drag and drop onto pads: an instrument replaces the pad
    drumGridUI_->onPluginDropped = [postPadEdit, getDrumGrid, updatePadFromChain,
                                    loadSampleToPad](int padIndex, const juce::DynamicObject& obj) {
        auto& tm = magda::TrackManager::getInstance();

        bool isExternal = obj.getProperty("isExternal");
        juce::String uniqueId = obj.getProperty("uniqueId").toString();

        const auto refreshPad = [padIndex, getDrumGrid, updatePadFromChain]() {
            if (auto* dg = getDrumGrid())
                updatePadFromChain(dg, padIndex);
        };

        // Handle internal plugins (MagdaSampler, etc.)
        if (!isExternal) {
            if (isInstrumentDrop(obj) && !isDrumGridPluginId(uniqueId)) {
                if (uniqueId.equalsIgnoreCase(daw::audio::MagdaSamplerPlugin::xmlTypeName)) {
                    loadSampleToPad(padIndex, juce::File());
                } else {
                    postPadEdit(
                        "Set Pad Instrument",
                        [padIndex,
                         device = internalPadDevice(uniqueId, obj.getProperty("name").toString())](
                            const magda::ChainNodePath& grid) {
                            magda::TrackManager::getInstance().setPadDevice(grid, padIndex, device);
                        },
                        refreshPad);
                }
            }
            return;
        }

        // External plugin: look it up in KnownPluginList
        juce::String fileOrId = obj.getProperty("fileOrIdentifier").toString();

        auto* audioEngine = tm.getAudioEngine();
        if (!audioEngine)
            return;

        for (const auto& desc : audioEngine->getKnownPluginTypes()) {
            if (pluginDescriptionMatchesDrop(desc, fileOrId, uniqueId)) {
                if (desc.isInstrument)
                    postPadEdit(
                        "Set Pad Instrument",
                        [padIndex,
                         device = externalPadDevice(desc)](const magda::ChainNodePath& grid) {
                            magda::TrackManager::getInstance().setPadDevice(grid, padIndex, device);
                        },
                        refreshPad);
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

    // Delete from a chain row removes the chain the row stands for, whatever
    // range it answers to. `clearPad()` is the pad's own delete and refuses a
    // chain shared with its neighbours, which after a range edit would leave a
    // widened chain with no way off the grid at all (#2211).
    drumGridUI_->onPadDeleteRequested = [this, postPadEdit](int padIndex) {
        postPadEdit(
            "Delete Pad Chain",
            [padIndex](const magda::ChainNodePath& grid) {
                auto& tm = magda::TrackManager::getInstance();
                if (const auto* pad = tm.getPad(grid, padIndex))
                    tm.removePadChain(grid, pad->id);
            },
            [this, padIndex]() {
                drumGridUI_->updatePadInfo(padIndex, "", false, false, 0.0f, 0.0f, -1);
            });
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
    drumGridUI_->onPadsSwapped = [this, postPadEdit, getDrumGrid, updatePadFromChain](int srcPad,
                                                                                      int dstPad) {
        postPadEdit(
            "Swap Pads",
            [srcPad, dstPad](const magda::ChainNodePath& grid) {
                magda::TrackManager::getInstance().swapPads(grid, srcPad, dstPad);
            },
            [this, srcPad, dstPad, getDrumGrid, updatePadFromChain]() {
                if (auto* dg = getDrumGrid()) {
                    updatePadFromChain(dg, srcPad);
                    updatePadFromChain(dg, dstPad);
                }
                drumGridUI_->rebuildChainRows();
            });
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
    padChain.getPluginSlots = [getDrumGrid, gridPath, postPadEdit](
                                  int padIndex) -> std::vector<PadChainPanel::PluginSlotInfo> {
        std::vector<PadChainPanel::PluginSlotInfo> result;
        auto* dg = getDrumGrid();
        if (!dg)
            return result;

        int midiNote = daw::audio::DrumGridPlugin::baseNote + padIndex;
        auto* chain = dg->getChainForNote(midiNote);
        if (!chain)
            return result;

        auto& tm = magda::TrackManager::getInstance();
        const auto grid = gridPath();
        const auto* pad = tm.getPad(grid, padIndex);
        const auto padChainId = pad != nullptr ? pad->id : magda::INVALID_CHAIN_ID;

        // What the model says a pad device is, matched by id. Only what the
        // model cannot hold is read off the plugin (#2207).
        const auto modelDevice = [pad](magda::DeviceId deviceId) -> const magda::DeviceInfo* {
            if (pad == nullptr)
                return nullptr;
            for (const auto& element : pad->elements)
                if (magda::isDevice(element) && magda::getDevice(element).id == deviceId)
                    return &magda::getDevice(element);
            return nullptr;
        };

        for (int pluginIndex = 0; pluginIndex < static_cast<int>(chain->plugins.size());
             ++pluginIndex) {
            auto& plugin = chain->plugins[static_cast<size_t>(pluginIndex)];
            if (!plugin)
                continue;
            PadChainPanel::PluginSlotInfo info;
            info.plugin = plugin.get();
            info.livePlugin = [plugin]() { return plugin; };
            info.deviceId = dg->getPluginDeviceId(chain->index, pluginIndex);
            info.device = projectPadPluginDevice(info.deviceId, plugin);
            info.isSampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get()) != nullptr;
            info.name = info.device.name.isNotEmpty() ? info.device.name : plugin->getName();

            // A pad plugin runs inside the grid, not on the track's graph, so
            // it has no DeviceMeteringManager tap of its own. The grid meters
            // each one as it processes it and the slot drains that (#2211).
            info.getMeterLevels = [getDrumGrid, chainIndex = chain->index,
                                   pluginIndex]() -> std::pair<float, float> {
                auto* grid = getDrumGrid();
                return grid != nullptr ? grid->consumeChainPluginPeak(chainIndex, pluginIndex)
                                       : std::pair<float, float>{0.0f, 0.0f};
            };

            if (const auto* device = modelDevice(info.deviceId)) {
                info.bypassed = device->bypassed;
                info.gainDb = device->gainDb;
                info.device.bypassed = device->bypassed;
            }

            if (padChainId != magda::INVALID_CHAIN_ID) {
                info.onPowerChanged = [postPadEdit, padChainId,
                                       deviceId = info.deviceId](bool powered) {
                    postPadEdit(powered ? "Enable Pad Device" : "Disable Pad Device",
                                [padChainId, deviceId, powered](const magda::ChainNodePath& grid) {
                                    magda::TrackManager::getInstance().setPadDeviceBypassed(
                                        grid, padChainId, deviceId, !powered);
                                });
                };
                info.onGainDbChanged = [postPadEdit, padChainId,
                                        deviceId = info.deviceId](float gainDb) {
                    postPadEdit("Set Pad Device Gain",
                                [padChainId, deviceId, gainDb](const magda::ChainNodePath& grid) {
                                    magda::TrackManager::getInstance().setPadDeviceGainDb(
                                        grid, padChainId, deviceId, gainDb);
                                });
                };
            }

            result.push_back(info);
        }
        return result;
    };

    // Adding a device to a pad's chain. An instrument replaces the pad; an
    // effect joins the chain behind it. Both are chain edits on the model: the
    // pad is a chain, so the ordinary add is the one that runs (#2207).
    auto addToPad = [this, postPadEdit, getDrumGrid, updatePadFromChain](
                        int padIndex, const magda::DeviceInfo& device, int insertIdx) {
        postPadEdit(
            device.isInstrument ? "Set Pad Instrument" : "Add Pad Device",
            [padIndex, device, insertIdx](const magda::ChainNodePath& grid) {
                auto& tm = magda::TrackManager::getInstance();
                if (device.isInstrument) {
                    tm.setPadDevice(grid, padIndex, device);
                    return;
                }

                const auto chainId = tm.ensurePad(grid, padIndex);
                if (chainId == INVALID_CHAIN_ID)
                    return;
                tm.addDeviceToPad(grid, chainId, device, insertIdx);
            },
            [this, padIndex, isInstrument = device.isInstrument, getDrumGrid,
             updatePadFromChain]() {
                if (isInstrument)
                    if (auto* dg = getDrumGrid())
                        updatePadFromChain(dg, padIndex);
                drumGridUI_->getPadChainPanel().refresh();
            });
    };

    // FX plugin drop onto chain area
    padChain.onPluginDropped =
        [addToPad, loadSampleToPad](int padIndex, const juce::DynamicObject& obj, int insertIdx) {
            bool isExternal = obj.getProperty("isExternal");
            juce::String uniqueId = obj.getProperty("uniqueId").toString();

            if (!isExternal) {
                if (!isDrumGridPluginId(uniqueId) && !isMidiFxDrop(obj)) {
                    if (uniqueId.equalsIgnoreCase(daw::audio::MagdaSamplerPlugin::xmlTypeName)) {
                        loadSampleToPad(padIndex, juce::File());
                    } else {
                        addToPad(padIndex,
                                 internalPadDevice(uniqueId, obj.getProperty("name").toString()),
                                 insertIdx);
                    }
                }
                return;
            }

            // External plugin: look it up in KnownPluginList
            juce::String fileOrId = obj.getProperty("fileOrIdentifier").toString();

            auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
            if (!audioEngine)
                return;
            for (const auto& desc : audioEngine->getKnownPluginTypes()) {
                if (pluginDescriptionMatchesDrop(desc, fileOrId, uniqueId)) {
                    if (isMidiFxPlugin(desc))
                        return;
                    addToPad(padIndex, externalPadDevice(desc), insertIdx);
                    return;
                }
            }
        };

    // Remove plugin from chain
    padChain.onPluginRemoved = [postPadEdit, getDrumGrid, updatePadFromChain](int padIndex,
                                                                              int pluginIndex) {
        postPadEdit(
            "Remove Pad Device",
            [padIndex, pluginIndex](const magda::ChainNodePath& grid) {
                auto& tm = magda::TrackManager::getInstance();
                const auto* pad = tm.getPad(grid, padIndex);
                if (pad == nullptr)
                    return;

                const auto devices = pad->getDevices();
                if (pluginIndex < 0 || pluginIndex >= static_cast<int>(devices.size()))
                    return;

                tm.removeDeviceFromPad(grid, pad->id,
                                       devices[static_cast<size_t>(pluginIndex)]->id);
            },
            [padIndex, getDrumGrid, updatePadFromChain]() {
                if (auto* dg = getDrumGrid())
                    updatePadFromChain(dg, padIndex);
            });
    };

    // Reorder plugins in chain
    padChain.onPluginMoved = [postPadEdit](int padIndex, int fromIdx, int toIdx) {
        postPadEdit("Reorder Pad Devices",
                    [padIndex, fromIdx, toIdx](const magda::ChainNodePath& grid) {
                        auto& tm = magda::TrackManager::getInstance();
                        const auto* pad = tm.getPad(grid, padIndex);
                        if (pad == nullptr)
                            return;
                        tm.moveDeviceInPad(grid, pad->id, fromIdx, toIdx);
                    });
    };

    // Forward sample operations from PadDeviceSlot -> the model
    padChain.onSampleDropped = [loadSampleToPad](int padIndex, const juce::File& file) {
        loadSampleToPad(padIndex, file);
    };

    padChain.onLoadSampleRequested = [this, loadSampleToPad](int padIndex) {
        auto chooser = std::make_shared<juce::FileChooser>("Load Sample", juce::File(),
                                                           "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles,
                             [this, padIndex, chooser, loadSampleToPad](const juce::FileChooser&) {
                                 if (!drumGridUI_)
                                     return;
                                 auto result = chooser->getResult();
                                 if (result.existsAsFile())
                                     loadSampleToPad(padIndex, result);
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
    padChain.onAddDeviceClicked = [this, addToPad, loadSampleToPad, getDrumGrid,
                                   updatePadFromChain](int padIndex) {
        juce::PopupMenu menu;

        std::vector<PluginBrowserInfo> menuInternals;
        juce::PopupMenu internalInstrumentMenu;
        juce::PopupMenu internalFxMenu;
        for (const auto& entry : PluginBrowserContent::getInternalPlugins()) {
            if (isDrumGridPluginId(entry.uniqueId) || isMidiFxPlugin(entry))
                continue;

            menuInternals.push_back(entry);
            const int itemId = static_cast<int>(menuInternals.size());
            auto& targetMenu =
                entry.category == "Instrument" ? internalInstrumentMenu : internalFxMenu;
            targetMenu.addItem(itemId, entry.name);
        }
        menu.addSubMenu("Pad Instruments", internalInstrumentMenu);
        menu.addSubMenu("Internal FX", internalFxMenu);

        // External plugins from KnownPluginList
        juce::Array<juce::PluginDescription> externalPlugins;
        if (auto* engine = magda::TrackManager::getInstance().getAudioEngine()) {
            externalPlugins = engine->getPreferredPluginTypes();
        }

        if (!externalPlugins.isEmpty()) {
            juce::PopupMenu externalInstrumentMenu;
            std::map<juce::String, juce::PopupMenu> fxByManufacturer;
            for (int i = 0; i < externalPlugins.size(); ++i) {
                const auto& desc = externalPlugins[i];
                if (isMidiFxPlugin(desc))
                    continue;

                auto manufacturer =
                    desc.manufacturerName.isEmpty() ? "Unknown" : desc.manufacturerName;
                if (desc.isInstrument)
                    externalInstrumentMenu.addItem(1000 + i, desc.name);
                else
                    fxByManufacturer[manufacturer].addItem(1000 + i, desc.name);
            }

            if (externalInstrumentMenu.getNumItems() > 0)
                menu.addSubMenu("External Instruments", externalInstrumentMenu);
            for (auto& [manufacturer, subMenu] : fxByManufacturer)
                menu.addSubMenu(manufacturer, subMenu);
        }

        auto capturedPlugins =
            std::make_shared<juce::Array<juce::PluginDescription>>(std::move(externalPlugins));
        auto capturedInternals =
            std::make_shared<std::vector<PluginBrowserInfo>>(std::move(menuInternals));

        menu.showMenuAsync(juce::PopupMenu::Options(), [this, padIndex, addToPad, loadSampleToPad,
                                                        getDrumGrid, updatePadFromChain,
                                                        capturedPlugins,
                                                        capturedInternals](int result) {
            if (result == 0 || !drumGridUI_)
                return;

            if (result >= 1 && result <= static_cast<int>(capturedInternals->size())) {
                auto& entry = (*capturedInternals)[static_cast<size_t>(result - 1)];
                if (entry.uniqueId.equalsIgnoreCase(daw::audio::MagdaSamplerPlugin::xmlTypeName)) {
                    loadSampleToPad(padIndex, juce::File());
                    if (auto* dg = getDrumGrid())
                        updatePadFromChain(dg, padIndex);
                    drumGridUI_->getPadChainPanel().refresh();
                } else {
                    addToPad(padIndex, internalPadDevice(entry.uniqueId, entry.name), -1);
                }
            } else if (result >= 1000) {
                int pluginIdx = result - 1000;
                if (pluginIdx < capturedPlugins->size())
                    addToPad(padIndex, externalPadDevice((*capturedPlugins)[pluginIdx]), -1);
            }
        });
    };

    parent.addAndMakeVisible(*drumGridUI_);
    update(device);
    return true;
}

juce::var DeviceCustomUIManager::executeSamplerCommand(const juce::Identifier& command,
                                                       const juce::var& arguments) {
    auto plugin = getLivePlugin();
    auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get());

    if (command == kSamplerSetLoopEnabled) {
        if (sampler == nullptr)
            return false;
        const bool enabled = static_cast<bool>(arguments);
        sampler->loopEnabledAtomic.store(enabled, std::memory_order_relaxed);
        sampler->loopEnabledValue = enabled;
        return true;
    }

    if (command == kSamplerSetRootNote) {
        if (sampler == nullptr)
            return false;
        sampler->setRootNote(static_cast<int>(arguments));
        return true;
    }

    if (command == kSamplerGetPlaybackPosition)
        return sampler != nullptr ? juce::var{sampler->getPlaybackPosition()} : juce::var{0.0};

    if (command != kSamplerLoadSample)
        return {};

    auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
    if (audioEngine == nullptr)
        return false;
    auto* bridge = audioEngine->getAudioBridge();
    if (bridge == nullptr)
        return false;

    const juce::File file(arguments.toString());
    if (!file.existsAsFile() || !bridge->loadSamplerSample(devicePath_, file))
        return false;

    plugin = getLivePlugin();
    sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get());
    if (sampler != nullptr && samplerUI_ != nullptr) {
        samplerUI_->updateParameters(
            sampler->attackValue.get(), sampler->decayValue.get(), sampler->sustainValue.get(),
            sampler->releaseValue.get(), sampler->pitchValue.get(), sampler->fineValue.get(),
            sampler->levelValue.get(), sampler->sampleStartValue.get(),
            sampler->sampleEndValue.get(), sampler->loopEnabledValue.get(),
            sampler->loopStartValue.get(), sampler->loopEndValue.get(),
            sampler->velAmountValue.get(), file.getFileNameWithoutExtension(),
            sampler->getRootNote(), sampler->voiceModeValue.get(), sampler->glideValue.get());
        samplerUI_->setWaveformData(sampler->getWaveform(), sampler->getSampleRate(),
                                    sampler->getSampleLengthSeconds());
    }
    return true;
}

bool DeviceCustomUIManager::executeImpulseResponseLoadCommand(const juce::var& arguments) {
    const juce::File file(arguments.toString());
    if (!file.existsAsFile()) {
        DBG("IR load: file does not exist: " << file.getFullPathName());
        return false;
    }

    juce::MemoryBlock raw;
    if (!file.loadFileAsData(raw)) {
        DBG("IR load: could not read: " << file.getFullPathName());
        return false;
    }

    // Encode up front so an unreadable file is refused before anything is
    // committed. The command then writes the blob into the MODEL's state
    // document and the projection reloads the live convolution from it -
    // the same route a project load takes (#2317). Undoable: the previous
    // document, previous IR included, comes back as one snapshot.
    juce::MemoryBlock encoded;
    if (!daw::audio::MagdaConvolutionPlugin::encodeImpulseResponse(raw.getData(), raw.getSize(),
                                                                   encoded)) {
        DBG("IR load: not readable as audio: " << file.getFullPathName());
        return false;
    }

    auto command = std::make_unique<magda::LoadImpulseResponseCommand>(
        devicePath_, file.getFileNameWithoutExtension(), std::move(encoded));
    // Refused BEFORE it reaches the UndoManager: executeCommand records even a
    // command whose execute() declines, which would leave a no-op undo step
    // and a false success here.
    if (!command->canExecute()) {
        DBG("IR load: refused (future-schema state or not a convolution device)");
        return false;
    }
    magda::UndoManager::getInstance().executeCommand(std::move(command));

    if (impulseResponseUI_ != nullptr)
        impulseResponseUI_->setIRName(file.getFileNameWithoutExtension());
    return true;
}

juce::var DeviceCustomUIManager::executeSequencerCommand(const juce::Identifier& command,
                                                         const juce::var& arguments,
                                                         bool polyphonic) {
    auto plugin = getLivePlugin();
    if (polyphonic) {
        auto* sequencer = dynamic_cast<daw::audio::PolyStepSequencerPlugin*>(plugin.get());
        if (sequencer == nullptr)
            return false;

        if (command == kPolyStepSequencerRandomizePattern) {
            sequencer->randomizePattern();
            return true;
        }
        if (command == kPolyStepSequencerGetStepRecording)
            return sequencer->isStepRecording();

        sequencer->setStepRecording(static_cast<bool>(arguments));
        return true;
    }

    auto* sequencer = dynamic_cast<daw::audio::StepSequencerPlugin*>(plugin.get());
    if (sequencer == nullptr)
        return false;

    if (command == kStepSequencerRandomizePattern) {
        sequencer->randomizePattern();
        return true;
    }
    if (command == kStepSequencerGetStepRecording)
        return sequencer->isStepRecording();

    sequencer->setStepRecording(static_cast<bool>(arguments));
    return true;
}

juce::var DeviceCustomUIManager::executeCustomUiCommand(const juce::Identifier& command,
                                                        const juce::var& arguments) {
    if (command == kSamplerSetLoopEnabled || command == kSamplerSetRootNote ||
        command == kSamplerGetPlaybackPosition || command == kSamplerLoadSample)
        return executeSamplerCommand(command, arguments);

    if (command == kImpulseResponseLoadFile)
        return executeImpulseResponseLoadCommand(arguments);

    if (command == kStepSequencerRandomizePattern || command == kStepSequencerGetStepRecording ||
        command == kStepSequencerSetStepRecording)
        return executeSequencerCommand(command, arguments, false);

    if (command == kPolyStepSequencerRandomizePattern ||
        command == kPolyStepSequencerGetStepRecording ||
        command == kPolyStepSequencerSetStepRecording)
        return executeSequencerCommand(command, arguments, true);

    return {};
}

void DeviceCustomUIManager::create(const magda::DeviceInfo& device, juce::Component* parent,
                                   const Callbacks& callbacks) {
    livePluginProvider_ = callbacks.getLivePlugin;
    deviceUiContext_ = callbacks.deviceUiContext;
    if (deviceUiContext_ == nullptr) {
        magda::ChainNodePath initialPath;
        if (callbacks.getNodePath)
            initialPath = callbacks.getNodePath();
        deviceUiContext_ = std::make_shared<magda::BasicDeviceUiContext>(device, initialPath);
    } else if (auto* basicContext =
                   dynamic_cast<magda::BasicDeviceUiContext*>(deviceUiContext_.get())) {
        basicContext->setDevice(device);
    }
    if (auto* basicContext = dynamic_cast<magda::BasicDeviceUiContext*>(deviceUiContext_.get())) {
        basicContext->setParameterController(std::make_shared<CallbackDeviceParameterController>(
            device, callbacks.onParameterChanged));
        basicContext->setCommandController(std::make_shared<CallbackDeviceCommandController>(
            [this](const juce::Identifier& command, const juce::var& arguments) -> juce::var {
                return executeCustomUiCommand(command, arguments);
            }));
    }

    auto uiCallbacks = callbacks;
    uiCallbacks.deviceUiContext = deviceUiContext_;

    if (device.pluginId.containsIgnoreCase("tone")) {
        createToneGeneratorUI(device, *parent, uiCallbacks);
    } else if (createSamplerUI(device, *parent, uiCallbacks)) {
        // handled by helper
    } else if (createDrumGridUI(device, *parent, uiCallbacks)) {
        // handled by helper
    } else if (createFourOscUI(device, *parent, uiCallbacks)) {
        // handled by helper
    } else if (createCustomInstrumentUI(device, *parent, uiCallbacks)) {
        // handled by helper
    } else if (createSimpleEffectUI(device, *parent, uiCallbacks)) {
        // handled by helper
    } else if (createImpulseResponseUI(device, *parent, uiCallbacks)) {
        // handled by helper
    } else if (daw::audio::internalPluginHasTag(device.pluginId, "external-insert")) {
        createExternalInsertUI(device, *parent);
    } else if (!createMidiUtilityUI(device, *parent, uiCallbacks)) {
        createAnalyzerUI(device, *parent);
    }
}

void DeviceCustomUIManager::createExternalInsertUI(const magda::DeviceInfo& device,
                                                   juce::Component& parent) {
    externalInsertUI_ = std::make_unique<ExternalInsertUI>(device.isInstrument);
    parent.addAndMakeVisible(*externalInsertUI_);
    // create() may run before the slot's path is valid; setDevicePath() rebinds
    // the pickers from the live plugin once refreshLivePluginBindings() fires.
    if (devicePath_.isValid())
        externalInsertUI_->setDevicePath(devicePath_);
}

void DeviceCustomUIManager::setDevicePath(const magda::ChainNodePath& path) {
    devicePath_ = path;
    if (auto* basicContext = dynamic_cast<magda::BasicDeviceUiContext*>(deviceUiContext_.get())) {
        basicContext->setPath(path);
    }
    // create() bound the analyzer UIs while the path was still invalid; now that
    // it is set, resolve their plugin for real.
    refreshLivePluginBindings();

    // 4OSC's modulation destination dropdown is built from the live TE plugin,
    // and create() can run before the slot has a valid path. Repopulate it once
    // the path is bound so LFO/Mod Env destination lists are not left empty.
    if (fourOscUI_ && devicePath_.isValid())
        readAndPushModMatrix(devicePath_.getDeviceId());

    // Same create-before-path situation for the Sidechain faceplate: its curve
    // editor binds to the live model through the path, so rebind now.
    if (sidechainUI_ && devicePath_.isValid())
        sidechainUI_->refreshFromModel();
}

void DeviceCustomUIManager::refreshLivePluginBindings() {
    bindAnalyzerPlugins();

    if (externalInsertUI_ != nullptr)
        externalInsertUI_->setDevicePath(devicePath_);

    if (polySynthUI_ != nullptr) {
        daw::audio::compiled::MagdaPolySynthCompiledPlugin* synth = nullptr;
        if (auto plugin = getLivePlugin())
            synth = daw::audio::tracktion_adapter::deviceFromPlugin<
                daw::audio::compiled::MagdaPolySynthCompiledPlugin>(plugin.get());
        polySynthUI_->setLivePlugin(synth);
    }

    if (struckUI_ != nullptr) {
        daw::audio::compiled::MagdaCompiledPolyInstrument* inst = nullptr;
        if (auto plugin = getLivePlugin())
            inst = daw::audio::tracktion_adapter::deviceFromPlugin<
                daw::audio::compiled::MagdaCompiledPolyInstrument>(plugin.get());
        struckUI_->setLivePlugin(inst);
    }
}

void DeviceCustomUIManager::detachFromLivePlugin() {
    livePluginProvider_ = {};
    devicePath_ = {};
    telemetryPlugin_ = nullptr;
    oscilloscopeTelemetry_.reset();
    spectrumTelemetry_.reset();
    levelsTelemetry_.reset();
    nimbusTelemetry_.reset();

    if (oscilloscopeUI_ != nullptr)
        oscilloscopeUI_->setTelemetrySource(nullptr);
    if (spectrumAnalyzerUI_ != nullptr) {
        spectrumAnalyzerUI_->setTelemetrySource(nullptr);
        spectrumAnalyzerUI_->setTrackId(magda::INVALID_TRACK_ID);
    }
    if (levelsUI_ != nullptr)
        levelsUI_->setTelemetrySource(nullptr);
    if (nimbusUI_ != nullptr)
        nimbusUI_->setTelemetrySource(nullptr);
    if (polySynthUI_ != nullptr)
        polySynthUI_->setLivePlugin(nullptr);
    if (struckUI_ != nullptr)
        struckUI_->setLivePlugin(nullptr);

    arpPlugin_ = nullptr;
    strumPlugin_ = nullptr;
    stepSeqPlugin_ = nullptr;
    polyStepSeqPlugin_ = nullptr;
    chordPlugin_ = nullptr;

    if (auto* basicContext = dynamic_cast<magda::BasicDeviceUiContext*>(deviceUiContext_.get()))
        basicContext->invalidate();
}

void DeviceCustomUIManager::bindAnalyzerPlugins() {
    if (oscilloscopeUI_ == nullptr && spectrumAnalyzerUI_ == nullptr && levelsUI_ == nullptr &&
        nimbusUI_ == nullptr)
        return;

    auto publishTelemetrySource = [this](std::shared_ptr<magda::DeviceTelemetrySource> source,
                                         const juce::String& key) {
        auto* basicContext = dynamic_cast<magda::BasicDeviceUiContext*>(deviceUiContext_.get());
        if (basicContext == nullptr)
            return;
        if (source != nullptr)
            basicContext->setTelemetrySource(std::move(source));
        else
            basicContext->clearTelemetrySource(key);
    };

    auto plugin = getLivePlugin();
    if (plugin.get() != telemetryPlugin_) {
        telemetryPlugin_ = plugin.get();
        oscilloscopeTelemetry_.reset();
        spectrumTelemetry_.reset();
        levelsTelemetry_.reset();
        nimbusTelemetry_.reset();
    }

    if (oscilloscopeUI_ != nullptr) {
        std::shared_ptr<OscilloscopeTelemetrySource> source;
        if (daw::audio::tracktion_adapter::deviceFromPlugin<daw::audio::OscilloscopePlugin>(
                plugin.get()) != nullptr) {
            if (oscilloscopeTelemetry_ == nullptr)
                oscilloscopeTelemetry_ =
                    std::make_shared<OscilloscopePluginTelemetrySource>(plugin);
            source = oscilloscopeTelemetry_;
            publishTelemetrySource(source, OscilloscopeTelemetrySource::kKey);
        } else {
            publishTelemetrySource(nullptr, OscilloscopeTelemetrySource::kKey);
        }
        oscilloscopeUI_->setTelemetrySource(std::move(source));
    }
    if (spectrumAnalyzerUI_ != nullptr) {
        std::shared_ptr<SpectrumTelemetrySource> source;
        if (daw::audio::tracktion_adapter::deviceFromPlugin<daw::audio::SpectrumAnalyzerPlugin>(
                plugin.get()) != nullptr) {
            if (spectrumTelemetry_ == nullptr)
                spectrumTelemetry_ = std::make_shared<SpectrumPluginTelemetrySource>(plugin);
            source = spectrumTelemetry_;
            publishTelemetrySource(source, SpectrumTelemetrySource::kKey);
        } else {
            publishTelemetrySource(nullptr, SpectrumTelemetrySource::kKey);
        }
        spectrumAnalyzerUI_->setTelemetrySource(std::move(source));
        spectrumAnalyzerUI_->setTrackId(devicePath_.trackId);  // enables masking overlay
    }
    if (levelsUI_ != nullptr) {
        std::shared_ptr<LevelsTelemetrySource> source;
        if (dynamic_cast<daw::audio::LevelsPlugin*>(plugin.get()) != nullptr) {
            if (levelsTelemetry_ == nullptr)
                levelsTelemetry_ = std::make_shared<LevelsPluginTelemetrySource>(plugin);
            source = levelsTelemetry_;
            publishTelemetrySource(source, LevelsTelemetrySource::kKey);
        } else {
            publishTelemetrySource(nullptr, LevelsTelemetrySource::kKey);
        }
        levelsUI_->setTelemetrySource(std::move(source));
    }
    if (nimbusUI_ != nullptr) {
        std::shared_ptr<NimbusTelemetrySource> source;
        if (daw::audio::tracktion_adapter::deviceFromPlugin<daw::audio::MutableCloudsPlugin>(
                plugin.get()) != nullptr) {
            if (nimbusTelemetry_ == nullptr)
                nimbusTelemetry_ = std::make_shared<NimbusPluginTelemetrySource>(plugin);
            source = nimbusTelemetry_;
            publishTelemetrySource(source, NimbusTelemetrySource::kKey);
        } else {
            publishTelemetrySource(nullptr, NimbusTelemetrySource::kKey);
        }
        nimbusUI_->setTelemetrySource(std::move(source));
    }
}

// =============================================================================
// update
// =============================================================================

void DeviceCustomUIManager::update(const magda::DeviceInfo& device) {
    if (deviceUiContext_ != nullptr) {
        if (auto* controller =
                dynamic_cast<CallbackDeviceParameterController*>(deviceUiContext_->parameters())) {
            controller->setDevice(device);
        }
    }

    if (sidechainUI_ && device.pluginId.equalsIgnoreCase(daw::audio::SidechainPlugin::xmlTypeName))
        sidechainUI_->updateFromDevice(device);

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
        float voiceMode = 0.0f, glide = 0.0f;
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
        if (device.parameters.size() >= 14) {
            voiceMode = device.parameters[12].currentValue;
            glide = device.parameters[13].currentValue;
        }

        auto plugin = getLivePlugin();
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
                samplerUI_->setWaveformData(sampler->getWaveform(), sampler->getSampleRate(),
                                            sampler->getSampleLengthSeconds());
        }

        samplerUI_->updateParameters(attack, decay, sustain, release, pitch, fine, level,
                                     sampleStart, sampleEnd, loopEnabled, loopStart, loopEnd,
                                     velAmount, sampleName, rootNote, voiceMode, glide);
    }

    if (drumGridUI_ &&
        device.pluginId.containsIgnoreCase(daw::audio::DrumGridPlugin::xmlTypeName)) {
        auto plugin = getLivePlugin();
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
                                                   chain->bypassed.get(), chain->busOutput.get());
                    }
                }
            }

            int selectedPad = drumGridUI_->getSelectedPad();
            drumGridUI_->getPadChainPanel().showPadChain(selectedPad);
        }
    }

    if (fourOscUI_ && device.pluginId.containsIgnoreCase("4osc")) {
        fourOscUI_->updateFromParameters(device.parameters);

        auto plugin = getLivePlugin();
        if (auto state = magda::FourOscProcessor::capturePluginState(plugin.get()))
            fourOscUI_->updatePluginState(*state);
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

    if (struckUI_ && StruckInstrumentUI::handles(device.pluginId)) {
        struckUI_->updateFromParameters(device.parameters);
    }

    if (impulseResponseUI_ && device.pluginId == daw::audio::MagdaConvolutionPlugin::xmlTypeName) {
        impulseResponseUI_->updateFromParameters(device.parameters);

        auto plugin = getLivePlugin();
        if (const auto* ir =
                daw::audio::tracktion_adapter::deviceFromPlugin<daw::audio::MagdaConvolutionPlugin>(
                    plugin.get()))
            impulseResponseUI_->setIRName(ir->irName());
    }
}

}  // namespace magda::daw::ui
