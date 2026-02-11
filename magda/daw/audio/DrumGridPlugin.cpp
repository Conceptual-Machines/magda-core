#include "DrumGridPlugin.hpp"

#include "MagdaSamplerPlugin.hpp"

namespace magda::daw::audio {

namespace te = tracktion::engine;

const char* DrumGridPlugin::xmlTypeName = "drumgrid";

const juce::Identifier DrumGridPlugin::padTreeId("PAD");
const juce::Identifier DrumGridPlugin::triggerNoteId("triggerNote");
const juce::Identifier DrumGridPlugin::padLevelId("padLevel");
const juce::Identifier DrumGridPlugin::padPanId("padPan");
const juce::Identifier DrumGridPlugin::padMuteId("padMute");
const juce::Identifier DrumGridPlugin::padSoloId("padSolo");

//==============================================================================
DrumGridPlugin::DrumGridPlugin(const te::PluginCreationInfo& info) : Plugin(info) {
    initPadValueTrees();
}

DrumGridPlugin::~DrumGridPlugin() {
    notifyListenersOfDeletion();
}

void DrumGridPlugin::initPadValueTrees() {
    auto um = getUndoManager();

    // Ensure we have PAD child ValueTrees in state for each pad
    // On first creation they won't exist; on restore they will
    for (int i = 0; i < maxPads; ++i) {
        juce::ValueTree padTree = state.getChildWithProperty(triggerNoteId, 36 + i);

        if (!padTree.isValid()) {
            // Check if there's a PAD at this index position
            if (i < state.getNumChildren() && state.getChild(i).hasType(padTreeId)) {
                padTree = state.getChild(i);
            } else {
                padTree = juce::ValueTree(padTreeId);
                padTree.setProperty(triggerNoteId, 36 + i, nullptr);
                padTree.setProperty(padLevelId, 0.0f, nullptr);
                padTree.setProperty(padPanId, 0.0f, nullptr);
                padTree.setProperty(padMuteId, false, nullptr);
                padTree.setProperty(padSoloId, false, nullptr);
                state.addChild(padTree, i, nullptr);
            }
        }

        pads_[static_cast<size_t>(i)].triggerNote =
            static_cast<int>(padTree.getProperty(triggerNoteId, 36 + i));
        pads_[static_cast<size_t>(i)].level.referTo(padTree, padLevelId, um, 0.0f);
        pads_[static_cast<size_t>(i)].pan.referTo(padTree, padPanId, um, 0.0f);
        pads_[static_cast<size_t>(i)].mute.referTo(padTree, padMuteId, um, false);
        pads_[static_cast<size_t>(i)].solo.referTo(padTree, padSoloId, um, false);
    }
}

//==============================================================================
void DrumGridPlugin::initialise(const te::PluginInitialisationInfo& info) {
    sampleRate_ = info.sampleRate;
    blockSize_ = info.blockSizeSamples;
    scratchBuffer_.setSize(2, blockSize_);

    // Initialise any child plugins that are loaded
    for (auto& pad : pads_) {
        if (pad.plugin != nullptr)
            initChildPlugin(*pad.plugin);
    }
}

void DrumGridPlugin::deinitialise() {
    for (auto& pad : pads_) {
        if (pad.plugin != nullptr)
            deinitChildPlugin(*pad.plugin);
    }
}

void DrumGridPlugin::reset() {
    for (auto& pad : pads_) {
        if (pad.plugin != nullptr)
            pad.plugin->reset();
    }
}

void DrumGridPlugin::initChildPlugin(te::Plugin& childPlugin) {
    te::PluginInitialisationInfo childInfo;
    childInfo.sampleRate = sampleRate_;
    childInfo.blockSizeSamples = blockSize_;
    childPlugin.baseClassInitialise(childInfo);
}

void DrumGridPlugin::deinitChildPlugin(te::Plugin& childPlugin) {
    childPlugin.baseClassDeinitialise();
}

//==============================================================================
void DrumGridPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (fc.destBuffer == nullptr)
        return;

    auto& destBuffer = *fc.destBuffer;
    const int numSamples = fc.bufferNumSamples;
    const int startSample = fc.bufferStartSample;

    // Clear output region
    destBuffer.clear(startSample, numSamples);

    // Check if any pad is soloed
    bool anySoloed = false;
    for (const auto& pad : pads_) {
        if (pad.plugin != nullptr && pad.solo.get()) {
            anySoloed = true;
            break;
        }
    }

    for (int i = 0; i < maxPads; ++i) {
        auto& pad = pads_[static_cast<size_t>(i)];
        if (pad.plugin == nullptr)
            continue;

        // Solo/mute logic
        if (pad.mute.get())
            continue;
        if (anySoloed && !pad.solo.get())
            continue;

        // Filter MIDI: copy only events matching this pad's trigger note
        padMidi_.clear();
        if (fc.bufferForMidiMessages != nullptr) {
            for (auto& msg : *fc.bufferForMidiMessages) {
                if (msg.isNoteOnOrOff() && msg.getNoteNumber() == pad.triggerNote) {
                    padMidi_.add(msg);
                } else if (!msg.isNoteOnOrOff()) {
                    // Pass through non-note events (CC, pitch bend, etc.)
                    padMidi_.add(msg);
                }
            }
        }

        // Skip if no MIDI and plugin doesn't produce audio when idle
        if (padMidi_.isEmpty() && !pad.plugin->producesAudioWhenNoAudioInput())
            continue;

        // Clear scratch buffer
        scratchBuffer_.clear(0, numSamples);

        // Build child render context
        te::PluginRenderContext padContext(&scratchBuffer_,                  // destBuffer
                                           juce::AudioChannelSet::stereo(),  // destBufferChannels
                                           0,                                // bufferStartSample
                                           numSamples,                       // bufferNumSamples
                                           &padMidi_,       // bufferForMidiMessages
                                           0.0,             // midiBufferOffset
                                           fc.editTime,     // editTime
                                           fc.isPlaying,    // playing
                                           fc.isScrubbing,  // scrubbing
                                           fc.isRendering,  // rendering
                                           false            // allowBypassedProcessing
        );

        pad.plugin->applyToBuffer(padContext);

        // Apply per-pad level and pan, then mix into dest
        float levelDb = pad.level.get();
        float levelLinear = juce::Decibels::decibelsToGain(levelDb);
        float panValue = pad.pan.get();  // -1..1

        // Simple equal-power pan law
        float leftGain =
            levelLinear * std::cos((panValue + 1.0f) * juce::MathConstants<float>::halfPi * 0.5f);
        float rightGain =
            levelLinear * std::sin((panValue + 1.0f) * juce::MathConstants<float>::halfPi * 0.5f);

        // Mix scratch into dest
        if (destBuffer.getNumChannels() >= 1)
            destBuffer.addFrom(0, startSample, scratchBuffer_, 0, 0, numSamples, leftGain);
        if (destBuffer.getNumChannels() >= 2)
            destBuffer.addFrom(1, startSample, scratchBuffer_,
                               scratchBuffer_.getNumChannels() >= 2 ? 1 : 0, 0, numSamples,
                               rightGain);
    }
}

//==============================================================================
void DrumGridPlugin::loadSampleToPad(int padIndex, const juce::File& file) {
    if (padIndex < 0 || padIndex >= maxPads)
        return;

    auto& pad = pads_[static_cast<size_t>(padIndex)];

    // Create a MagdaSamplerPlugin via plugin cache
    juce::ValueTree pluginState(te::IDs::PLUGIN);
    pluginState.setProperty(te::IDs::type, MagdaSamplerPlugin::xmlTypeName, nullptr);

    auto plugin = edit.getPluginCache().createNewPlugin(pluginState);
    if (!plugin)
        return;

    auto* sampler = dynamic_cast<MagdaSamplerPlugin*>(plugin.get());
    if (!sampler)
        return;

    sampler->loadSample(file);

    // If we're already initialised, init the child plugin
    if (sampleRate_ > 0 && blockSize_ > 0)
        initChildPlugin(*plugin);

    // Store in pad
    pad.plugin = plugin;

    // Store child plugin state in the pad's ValueTree
    auto padTree = state.getChild(padIndex);
    if (padTree.isValid()) {
        // Remove old plugin state if any
        auto oldPluginState = padTree.getChildWithName(te::IDs::PLUGIN);
        if (oldPluginState.isValid())
            padTree.removeChild(oldPluginState, nullptr);
        padTree.addChild(plugin->state, -1, nullptr);
    }
}

void DrumGridPlugin::loadPluginToPad(int padIndex, const juce::PluginDescription& desc) {
    if (padIndex < 0 || padIndex >= maxPads)
        return;

    auto& pad = pads_[static_cast<size_t>(padIndex)];

    auto plugin = edit.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, desc);
    if (!plugin)
        return;

    if (sampleRate_ > 0 && blockSize_ > 0)
        initChildPlugin(*plugin);

    pad.plugin = plugin;

    auto padTree = state.getChild(padIndex);
    if (padTree.isValid()) {
        auto oldPluginState = padTree.getChildWithName(te::IDs::PLUGIN);
        if (oldPluginState.isValid())
            padTree.removeChild(oldPluginState, nullptr);
        padTree.addChild(plugin->state, -1, nullptr);
    }
}

void DrumGridPlugin::clearPad(int padIndex) {
    if (padIndex < 0 || padIndex >= maxPads)
        return;

    auto& pad = pads_[static_cast<size_t>(padIndex)];

    if (pad.plugin != nullptr) {
        deinitChildPlugin(*pad.plugin);
        pad.plugin = nullptr;
    }

    auto padTree = state.getChild(padIndex);
    if (padTree.isValid()) {
        auto pluginState = padTree.getChildWithName(te::IDs::PLUGIN);
        if (pluginState.isValid())
            padTree.removeChild(pluginState, nullptr);
    }
}

const DrumGridPlugin::Pad& DrumGridPlugin::getPad(int padIndex) const {
    jassert(padIndex >= 0 && padIndex < maxPads);
    return pads_[static_cast<size_t>(padIndex)];
}

//==============================================================================
void DrumGridPlugin::restorePluginStateFromValueTree(const juce::ValueTree& v) {
    // Copy properties to state
    for (int i = 0; i < v.getNumProperties(); ++i) {
        auto propName = v.getPropertyName(i);
        state.setProperty(propName, v.getProperty(propName), nullptr);
    }

    // Restore PAD child trees and recreate child plugins
    for (int i = 0; i < v.getNumChildren(); ++i) {
        auto childTree = v.getChild(i);
        if (!childTree.hasType(padTreeId))
            continue;

        int note = childTree.getProperty(triggerNoteId, -1);
        if (note < 0)
            continue;

        // Find the pad index for this note
        int padIdx = note - 36;
        if (padIdx < 0 || padIdx >= maxPads)
            continue;

        auto& pad = pads_[static_cast<size_t>(padIdx)];

        // Update cached values
        pad.level.forceUpdateOfCachedValue();
        pad.pan.forceUpdateOfCachedValue();
        pad.mute.forceUpdateOfCachedValue();
        pad.solo.forceUpdateOfCachedValue();

        // Recreate child plugin from saved state
        auto pluginState = childTree.getChildWithName(te::IDs::PLUGIN);
        if (pluginState.isValid()) {
            auto plugin = edit.getPluginCache().getOrCreatePluginFor(pluginState);
            if (plugin) {
                pad.plugin = plugin;
                if (sampleRate_ > 0 && blockSize_ > 0)
                    initChildPlugin(*plugin);
            }
        }
    }
}

}  // namespace magda::daw::audio
