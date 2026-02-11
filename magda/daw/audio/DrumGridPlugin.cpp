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
        for (auto& plugin : pad.plugins) {
            if (plugin != nullptr)
                initChildPlugin(*plugin);
        }
    }
}

void DrumGridPlugin::deinitialise() {
    for (auto& pad : pads_) {
        for (auto& plugin : pad.plugins) {
            if (plugin != nullptr)
                deinitChildPlugin(*plugin);
        }
    }
}

void DrumGridPlugin::reset() {
    for (auto& pad : pads_) {
        for (auto& plugin : pad.plugins) {
            if (plugin != nullptr)
                plugin->reset();
        }
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
        if (!pad.plugins.empty() && pad.solo.get()) {
            anySoloed = true;
            break;
        }
    }

    for (int i = 0; i < maxPads; ++i) {
        auto& pad = pads_[static_cast<size_t>(i)];
        if (pad.plugins.empty())
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

        // Skip if no MIDI and first plugin doesn't produce audio when idle
        if (padMidi_.isEmpty() && pad.plugins[0] != nullptr &&
            !pad.plugins[0]->producesAudioWhenNoAudioInput())
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

        // Process plugins in sequence (instrument → FX chain)
        for (auto& plugin : pad.plugins) {
            if (plugin != nullptr)
                plugin->applyToBuffer(padContext);
        }

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
    sampler->setRootNote(pad.triggerNote);

    // If we're already initialised, init the child plugin
    if (sampleRate_ > 0 && blockSize_ > 0)
        initChildPlugin(*plugin);

    // Clear existing plugins
    for (auto& p : pad.plugins) {
        if (p != nullptr)
            deinitChildPlugin(*p);
    }
    pad.plugins.clear();

    // Store as first plugin (instrument slot)
    pad.plugins.push_back(plugin);

    // Store child plugin state in the pad's ValueTree
    auto padTree = state.getChild(padIndex);
    if (padTree.isValid()) {
        // Remove all old plugin states
        while (padTree.getChildWithName(te::IDs::PLUGIN).isValid())
            padTree.removeChild(padTree.getChildWithName(te::IDs::PLUGIN), nullptr);
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

    // Clear existing plugins
    for (auto& p : pad.plugins) {
        if (p != nullptr)
            deinitChildPlugin(*p);
    }
    pad.plugins.clear();

    // Store as first plugin (instrument slot)
    pad.plugins.push_back(plugin);

    auto padTree = state.getChild(padIndex);
    if (padTree.isValid()) {
        // Remove all old plugin states
        while (padTree.getChildWithName(te::IDs::PLUGIN).isValid())
            padTree.removeChild(padTree.getChildWithName(te::IDs::PLUGIN), nullptr);
        padTree.addChild(plugin->state, -1, nullptr);
    }
}

void DrumGridPlugin::clearPad(int padIndex) {
    if (padIndex < 0 || padIndex >= maxPads)
        return;

    auto& pad = pads_[static_cast<size_t>(padIndex)];

    for (auto& plugin : pad.plugins) {
        if (plugin != nullptr)
            deinitChildPlugin(*plugin);
    }
    pad.plugins.clear();

    auto padTree = state.getChild(padIndex);
    if (padTree.isValid()) {
        while (padTree.getChildWithName(te::IDs::PLUGIN).isValid())
            padTree.removeChild(padTree.getChildWithName(te::IDs::PLUGIN), nullptr);
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

        // Recreate child plugins from saved state (supports multiple per pad)
        for (int p = 0; p < childTree.getNumChildren(); ++p) {
            auto pluginState = childTree.getChild(p);
            if (!pluginState.hasType(te::IDs::PLUGIN))
                continue;
            auto plugin = edit.getPluginCache().getOrCreatePluginFor(pluginState);
            if (plugin) {
                pad.plugins.push_back(plugin);
                if (sampleRate_ > 0 && blockSize_ > 0)
                    initChildPlugin(*plugin);
            }
        }
    }
}

//==============================================================================
// FX chain management
//==============================================================================

void DrumGridPlugin::addPluginToPad(int padIndex, const juce::PluginDescription& desc,
                                    int insertIndex) {
    if (padIndex < 0 || padIndex >= maxPads)
        return;

    auto& pad = pads_[static_cast<size_t>(padIndex)];

    auto plugin = edit.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, desc);
    if (!plugin)
        return;

    if (sampleRate_ > 0 && blockSize_ > 0)
        initChildPlugin(*plugin);

    // Insert at position or append
    if (insertIndex < 0 || insertIndex >= static_cast<int>(pad.plugins.size()))
        pad.plugins.push_back(plugin);
    else
        pad.plugins.insert(pad.plugins.begin() + insertIndex, plugin);

    // Add plugin state to pad ValueTree at corresponding position
    auto padTree = state.getChild(padIndex);
    if (padTree.isValid()) {
        // Find the correct child index for insertion
        // Count existing PLUGIN children to find the right position
        if (insertIndex < 0 || insertIndex >= static_cast<int>(pad.plugins.size()) - 1)
            padTree.addChild(plugin->state, -1, nullptr);
        else {
            // Find the nth PLUGIN child to insert before
            int pluginChildIdx = 0;
            int count = 0;
            for (int c = 0; c < padTree.getNumChildren(); ++c) {
                if (padTree.getChild(c).hasType(te::IDs::PLUGIN)) {
                    if (count == insertIndex) {
                        pluginChildIdx = c;
                        break;
                    }
                    ++count;
                }
            }
            padTree.addChild(plugin->state, pluginChildIdx, nullptr);
        }
    }
}

void DrumGridPlugin::removePluginFromPad(int padIndex, int pluginIndex) {
    if (padIndex < 0 || padIndex >= maxPads)
        return;

    auto& pad = pads_[static_cast<size_t>(padIndex)];
    if (pluginIndex < 0 || pluginIndex >= static_cast<int>(pad.plugins.size()))
        return;

    auto& plugin = pad.plugins[static_cast<size_t>(pluginIndex)];
    if (plugin != nullptr)
        deinitChildPlugin(*plugin);

    // Remove from ValueTree
    auto padTree = state.getChild(padIndex);
    if (padTree.isValid()) {
        // Find the nth PLUGIN child
        int count = 0;
        for (int c = 0; c < padTree.getNumChildren(); ++c) {
            if (padTree.getChild(c).hasType(te::IDs::PLUGIN)) {
                if (count == pluginIndex) {
                    padTree.removeChild(c, nullptr);
                    break;
                }
                ++count;
            }
        }
    }

    pad.plugins.erase(pad.plugins.begin() + pluginIndex);
}

void DrumGridPlugin::movePluginInPad(int padIndex, int fromIndex, int toIndex) {
    if (padIndex < 0 || padIndex >= maxPads)
        return;

    auto& pad = pads_[static_cast<size_t>(padIndex)];
    int count = static_cast<int>(pad.plugins.size());
    if (fromIndex < 0 || fromIndex >= count || toIndex < 0 || toIndex >= count ||
        fromIndex == toIndex)
        return;

    // Move in vector
    auto plugin = pad.plugins[static_cast<size_t>(fromIndex)];
    pad.plugins.erase(pad.plugins.begin() + fromIndex);
    pad.plugins.insert(pad.plugins.begin() + toIndex, plugin);

    // Move in ValueTree — find the actual child indices for PLUGIN children
    auto padTree = state.getChild(padIndex);
    if (padTree.isValid()) {
        // Collect PLUGIN child indices
        std::vector<int> pluginChildIndices;
        for (int c = 0; c < padTree.getNumChildren(); ++c) {
            if (padTree.getChild(c).hasType(te::IDs::PLUGIN))
                pluginChildIndices.push_back(c);
        }

        if (fromIndex < static_cast<int>(pluginChildIndices.size()) &&
            toIndex < static_cast<int>(pluginChildIndices.size())) {
            padTree.moveChild(pluginChildIndices[static_cast<size_t>(fromIndex)],
                              pluginChildIndices[static_cast<size_t>(toIndex)], nullptr);
        }
    }
}

int DrumGridPlugin::getPadPluginCount(int padIndex) const {
    if (padIndex < 0 || padIndex >= maxPads)
        return 0;
    return static_cast<int>(pads_[static_cast<size_t>(padIndex)].plugins.size());
}

te::Plugin* DrumGridPlugin::getPadPlugin(int padIndex, int pluginIndex) const {
    if (padIndex < 0 || padIndex >= maxPads)
        return nullptr;
    auto& plugins = pads_[static_cast<size_t>(padIndex)].plugins;
    if (pluginIndex < 0 || pluginIndex >= static_cast<int>(plugins.size()))
        return nullptr;
    return plugins[static_cast<size_t>(pluginIndex)].get();
}

}  // namespace magda::daw::audio
