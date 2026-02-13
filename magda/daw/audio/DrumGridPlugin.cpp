#include "DrumGridPlugin.hpp"

#include "MagdaSamplerPlugin.hpp"

namespace magda::daw::audio {

namespace te = tracktion::engine;

const char* DrumGridPlugin::xmlTypeName = "drumgrid";

const juce::Identifier DrumGridPlugin::chainTreeId("CHAIN");
const juce::Identifier DrumGridPlugin::chainIndexId("index");
const juce::Identifier DrumGridPlugin::lowNoteId("lowNote");
const juce::Identifier DrumGridPlugin::highNoteId("highNote");
const juce::Identifier DrumGridPlugin::rootNoteId("rootNote");
const juce::Identifier DrumGridPlugin::chainNameId("name");
const juce::Identifier DrumGridPlugin::padLevelId("padLevel");
const juce::Identifier DrumGridPlugin::padPanId("padPan");
const juce::Identifier DrumGridPlugin::padMuteId("padMute");
const juce::Identifier DrumGridPlugin::padSoloId("padSolo");
const juce::Identifier DrumGridPlugin::mixerExpandedId("mixerExpanded");

//==============================================================================
DrumGridPlugin::DrumGridPlugin(const te::PluginCreationInfo& info) : Plugin(info) {
    mixerExpanded_.referTo(state, mixerExpandedId, getUndoManager(), false);

    // Restore chains from existing ValueTree state (if any)
    for (int i = 0; i < state.getNumChildren(); ++i) {
        auto childTree = state.getChild(i);
        if (!childTree.hasType(chainTreeId))
            continue;

        auto chain = std::make_unique<Chain>();
        chain->index = childTree.getProperty(chainIndexId, 0);
        chain->lowNote = childTree.getProperty(lowNoteId, 60);
        chain->highNote = childTree.getProperty(highNoteId, 60);
        chain->rootNote = childTree.getProperty(rootNoteId, 60);
        chain->name = childTree.getProperty(chainNameId, "").toString();

        auto um = getUndoManager();
        chain->level.referTo(childTree, padLevelId, um, 0.0f);
        chain->pan.referTo(childTree, padPanId, um, 0.0f);
        chain->mute.referTo(childTree, padMuteId, um, false);
        chain->solo.referTo(childTree, padSoloId, um, false);

        if (chain->index >= nextChainIndex_)
            nextChainIndex_ = chain->index + 1;

        chains_.push_back(std::move(chain));
    }
}

DrumGridPlugin::~DrumGridPlugin() {
    notifyListenersOfDeletion();
}

//==============================================================================
void DrumGridPlugin::initialise(const te::PluginInitialisationInfo& info) {
    sampleRate_ = info.sampleRate;
    blockSize_ = info.blockSizeSamples;

    // Initialise child plugins in all chains
    for (auto& chain : chains_) {
        for (auto& p : chain->plugins) {
            if (p != nullptr)
                p->baseClassInitialise(info);
        }
    }
}

void DrumGridPlugin::deinitialise() {
    for (auto& chain : chains_) {
        for (auto& p : chain->plugins) {
            if (p != nullptr && !p->baseClassNeedsInitialising())
                p->baseClassDeinitialise();
        }
    }
}

void DrumGridPlugin::reset() {
    for (auto& chain : chains_) {
        for (auto& p : chain->plugins) {
            if (p != nullptr)
                p->reset();
        }
    }
}

//==============================================================================
void DrumGridPlugin::applyToBuffer(const te::PluginRenderContext& rc) {
    if (!rc.destBuffer || !rc.bufferForMidiMessages)
        return;

    auto& outputBuffer = *rc.destBuffer;
    auto& inputMidi = *rc.bufferForMidiMessages;
    const int numSamples = rc.bufferNumSamples;
    const int numChannels = outputBuffer.getNumChannels();

    // Clear output (we sum into it)
    outputBuffer.clear(rc.bufferStartSample, numSamples);

    // Detect solo
    bool anySoloed = false;
    for (const auto& chain : chains_) {
        if (!chain->plugins.empty() && chain->solo.get()) {
            anySoloed = true;
            break;
        }
    }

    // Process each chain
    for (auto& chain : chains_) {
        if (chain->plugins.empty())
            continue;

        if (chain->mute.get())
            continue;
        if (anySoloed && !chain->solo.get())
            continue;

        // Filter MIDI by note range, remap notes
        chainMidi_.clear();
        chainMidi_.isAllNotesOff = inputMidi.isAllNotesOff;

        for (auto& msg : inputMidi) {
            if (msg.isNoteOnOrOff()) {
                int note = msg.getNoteNumber();
                if (note >= chain->lowNote && note <= chain->highNote) {
                    if (msg.isNoteOn()) {
                        int padIdx = note - baseNote;
                        if (padIdx >= 0 && padIdx < maxPads)
                            setPadTriggered(padIdx);
                    }
                    auto remapped = msg;
                    int remappedNote = chain->rootNote + (note - chain->lowNote);
                    remapped.setNoteNumber(remappedNote);
                    chainMidi_.add(remapped);
                }
            } else {
                chainMidi_.add(msg);
            }
        }

        // Skip if no MIDI and instrument doesn't produce audio without input
        if (chainMidi_.isEmpty() && !chain->plugins.empty() && chain->plugins[0] != nullptr &&
            !chain->plugins[0]->producesAudioWhenNoAudioInput())
            continue;

        // Create scratch buffer
        juce::AudioBuffer<float> scratchBuffer(numChannels, numSamples);
        scratchBuffer.clear();

        // Process each plugin in the chain
        te::PluginRenderContext chainRc(
            &scratchBuffer, juce::AudioChannelSet::canonicalChannelSet(numChannels), 0, numSamples,
            &chainMidi_, 0.0, rc.editTime, rc.isPlaying, rc.isScrubbing, rc.isRendering, false);

        for (auto& p : chain->plugins) {
            if (p != nullptr)
                p->applyToBufferWithAutomation(chainRc);
        }

        // Apply gain/pan and sum into output
        float levelDb = chain->level.get();
        float levelLinear = juce::Decibels::decibelsToGain(levelDb);
        float panValue = chain->pan.get();

        float leftGain =
            levelLinear * std::cos((panValue + 1.0f) * juce::MathConstants<float>::halfPi * 0.5f);
        float rightGain =
            levelLinear * std::sin((panValue + 1.0f) * juce::MathConstants<float>::halfPi * 0.5f);

        // Measure post-gain peak from scratch buffer
        float peakL = scratchBuffer.getMagnitude(0, 0, numSamples) * leftGain;
        float peakR =
            (numChannels >= 2 ? scratchBuffer.getMagnitude(1, 0, numSamples) : peakL) * rightGain;

        // Store as running max (UI thread resets via consumeChainPeak)
        if (chain->index >= 0 && chain->index < maxPads) {
            auto& meter = chainMeters_[static_cast<size_t>(chain->index)];
            auto prevL = meter.peakL.load(std::memory_order_relaxed);
            if (peakL > prevL)
                meter.peakL.store(peakL, std::memory_order_relaxed);
            auto prevR = meter.peakR.load(std::memory_order_relaxed);
            if (peakR > prevR)
                meter.peakR.store(peakR, std::memory_order_relaxed);
        }

        if (numChannels >= 1)
            outputBuffer.addFrom(0, rc.bufferStartSample, scratchBuffer, 0, 0, numSamples,
                                 leftGain);
        if (numChannels >= 2)
            outputBuffer.addFrom(1, rc.bufferStartSample, scratchBuffer,
                                 scratchBuffer.getNumChannels() >= 2 ? 1 : 0, 0, numSamples,
                                 rightGain);
    }
}

//==============================================================================
// Chain management
//==============================================================================

int DrumGridPlugin::addChain(int lowNote, int highNote, int rootNote, const juce::String& name) {
    int idx = nextChainIndex_++;

    juce::ValueTree chainTree(chainTreeId);
    chainTree.setProperty(chainIndexId, idx, nullptr);
    chainTree.setProperty(lowNoteId, lowNote, nullptr);
    chainTree.setProperty(highNoteId, highNote, nullptr);
    chainTree.setProperty(rootNoteId, rootNote, nullptr);
    chainTree.setProperty(chainNameId, name, nullptr);
    chainTree.setProperty(padLevelId, 0.0f, nullptr);
    chainTree.setProperty(padPanId, 0.0f, nullptr);
    chainTree.setProperty(padMuteId, false, nullptr);
    chainTree.setProperty(padSoloId, false, nullptr);
    state.addChild(chainTree, -1, nullptr);

    auto chain = std::make_unique<Chain>();
    chain->index = idx;
    chain->lowNote = lowNote;
    chain->highNote = highNote;
    chain->rootNote = rootNote;
    chain->name = name;

    auto um = getUndoManager();
    chain->level.referTo(chainTree, padLevelId, um, 0.0f);
    chain->pan.referTo(chainTree, padPanId, um, 0.0f);
    chain->mute.referTo(chainTree, padMuteId, um, false);
    chain->solo.referTo(chainTree, padSoloId, um, false);

    chains_.push_back(std::move(chain));

    notifyGraphRebuildNeeded();
    return idx;
}

void DrumGridPlugin::removeChain(int chainIndex) {
    for (auto it = chains_.begin(); it != chains_.end(); ++it) {
        if ((*it)->index == chainIndex) {
            // Deinit plugins before removing
            for (auto& p : (*it)->plugins) {
                if (p != nullptr && !p->baseClassNeedsInitialising())
                    p->baseClassDeinitialise();
            }
            chains_.erase(it);
            break;
        }
    }
    removeChainFromState(chainIndex);
    notifyGraphRebuildNeeded();
}

const std::vector<std::unique_ptr<DrumGridPlugin::Chain>>& DrumGridPlugin::getChains() const {
    return chains_;
}

const DrumGridPlugin::Chain* DrumGridPlugin::getChainForNote(int midiNote) const {
    for (const auto& chain : chains_) {
        if (midiNote >= chain->lowNote && midiNote <= chain->highNote)
            return chain.get();
    }
    return nullptr;
}

const DrumGridPlugin::Chain* DrumGridPlugin::getChainByIndex(int chainIndex) const {
    for (const auto& chain : chains_) {
        if (chain->index == chainIndex)
            return chain.get();
    }
    return nullptr;
}

DrumGridPlugin::Chain* DrumGridPlugin::getChainByIndexMutable(int chainIndex) {
    for (auto& chain : chains_) {
        if (chain->index == chainIndex)
            return chain.get();
    }
    return nullptr;
}

DrumGridPlugin::Chain* DrumGridPlugin::findChainForNote(int midiNote) {
    for (auto& chain : chains_) {
        if (midiNote >= chain->lowNote && midiNote <= chain->highNote)
            return chain.get();
    }
    return nullptr;
}

DrumGridPlugin::Chain* DrumGridPlugin::findOrCreateChainForPad(int padIndex) {
    if (padIndex < 0 || padIndex >= maxPads)
        return nullptr;

    int midiNote = baseNote + padIndex;

    if (auto* existing = findChainForNote(midiNote))
        return existing;

    int idx = addChain(midiNote, midiNote, midiNote, "");
    return getChainByIndexMutable(idx);
}

void DrumGridPlugin::removeChainFromState(int chainIndex) {
    for (int i = 0; i < state.getNumChildren(); ++i) {
        auto child = state.getChild(i);
        if (child.hasType(chainTreeId) &&
            static_cast<int>(child.getProperty(chainIndexId)) == chainIndex) {
            state.removeChild(i, nullptr);
            return;
        }
    }
}

juce::ValueTree DrumGridPlugin::findChainTree(int chainIndex) const {
    for (int i = 0; i < state.getNumChildren(); ++i) {
        auto child = state.getChild(i);
        if (child.hasType(chainTreeId) &&
            static_cast<int>(child.getProperty(chainIndexId)) == chainIndex)
            return child;
    }
    return {};
}

//==============================================================================
// Convenience pad-level API
//==============================================================================

void DrumGridPlugin::loadSampleToPad(int padIndex, const juce::File& file) {
    if (padIndex < 0 || padIndex >= maxPads)
        return;

    auto* chain = findOrCreateChainForPad(padIndex);
    if (!chain)
        return;

    int midiNote = baseNote + padIndex;

    juce::ValueTree pluginState(te::IDs::PLUGIN);
    pluginState.setProperty(te::IDs::type, MagdaSamplerPlugin::xmlTypeName, nullptr);

    auto plugin = edit.getPluginCache().createNewPlugin(pluginState);
    if (!plugin)
        return;

    auto* sampler = dynamic_cast<MagdaSamplerPlugin*>(plugin.get());
    if (!sampler)
        return;

    sampler->loadSample(file);
    sampler->setRootNote(midiNote);

    // Deinit old plugins
    for (auto& p : chain->plugins) {
        if (p != nullptr && !p->baseClassNeedsInitialising())
            p->baseClassDeinitialise();
    }
    chain->plugins.clear();

    chain->name = file.getFileNameWithoutExtension();
    chain->plugins.push_back(plugin);

    // Init new plugin if we're already initialized
    if (sampleRate_ > 0.0) {
        te::PluginInitialisationInfo initInfo;
        initInfo.startTime = tracktion::TimePosition();
        initInfo.sampleRate = sampleRate_;
        initInfo.blockSizeSamples = blockSize_;
        plugin->baseClassInitialise(initInfo);
    }

    auto chainTree = findChainTree(chain->index);
    if (chainTree.isValid()) {
        chainTree.setProperty(chainNameId, chain->name, nullptr);
        while (chainTree.getChildWithName(te::IDs::PLUGIN).isValid())
            chainTree.removeChild(chainTree.getChildWithName(te::IDs::PLUGIN), nullptr);
        chainTree.addChild(plugin->state, -1, nullptr);
    }

    notifyGraphRebuildNeeded();
}

void DrumGridPlugin::loadPluginToPad(int padIndex, const juce::PluginDescription& desc) {
    if (padIndex < 0 || padIndex >= maxPads)
        return;

    auto* chain = findOrCreateChainForPad(padIndex);
    if (!chain)
        return;

    auto plugin = edit.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, desc);
    if (!plugin)
        return;

    // Deinit old plugins
    for (auto& p : chain->plugins) {
        if (p != nullptr && !p->baseClassNeedsInitialising())
            p->baseClassDeinitialise();
    }
    chain->plugins.clear();

    chain->name = desc.name;
    chain->plugins.push_back(plugin);

    // Init new plugin if we're already initialized
    if (sampleRate_ > 0.0) {
        te::PluginInitialisationInfo initInfo;
        initInfo.startTime = tracktion::TimePosition();
        initInfo.sampleRate = sampleRate_;
        initInfo.blockSizeSamples = blockSize_;
        plugin->baseClassInitialise(initInfo);
    }

    auto chainTree = findChainTree(chain->index);
    if (chainTree.isValid()) {
        chainTree.setProperty(chainNameId, chain->name, nullptr);
        while (chainTree.getChildWithName(te::IDs::PLUGIN).isValid())
            chainTree.removeChild(chainTree.getChildWithName(te::IDs::PLUGIN), nullptr);
        chainTree.addChild(plugin->state, -1, nullptr);
    }

    notifyGraphRebuildNeeded();
}

void DrumGridPlugin::clearPad(int padIndex) {
    if (padIndex < 0 || padIndex >= maxPads)
        return;

    int midiNote = baseNote + padIndex;
    auto* chain = findChainForNote(midiNote);
    if (!chain)
        return;

    if (chain->lowNote == midiNote && chain->highNote == midiNote) {
        removeChain(chain->index);
        // removeChain already calls notifyGraphRebuildNeeded()
    }
}

//==============================================================================
// FX chain management on chains
//==============================================================================

void DrumGridPlugin::addPluginToChain(int chainIndex, const juce::PluginDescription& desc,
                                      int insertIndex) {
    auto* chain = getChainByIndexMutable(chainIndex);
    if (!chain)
        return;

    auto plugin = edit.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, desc);
    if (!plugin)
        return;

    if (insertIndex < 0 || insertIndex >= static_cast<int>(chain->plugins.size()))
        chain->plugins.push_back(plugin);
    else
        chain->plugins.insert(chain->plugins.begin() + insertIndex, plugin);

    // Init new plugin if we're already initialized
    if (sampleRate_ > 0.0) {
        te::PluginInitialisationInfo initInfo;
        initInfo.startTime = tracktion::TimePosition();
        initInfo.sampleRate = sampleRate_;
        initInfo.blockSizeSamples = blockSize_;
        plugin->baseClassInitialise(initInfo);
    }

    auto chainTree = findChainTree(chainIndex);
    if (chainTree.isValid()) {
        if (insertIndex < 0 || insertIndex >= static_cast<int>(chain->plugins.size()) - 1)
            chainTree.addChild(plugin->state, -1, nullptr);
        else {
            int pluginChildIdx = 0;
            int count = 0;
            for (int c = 0; c < chainTree.getNumChildren(); ++c) {
                if (chainTree.getChild(c).hasType(te::IDs::PLUGIN)) {
                    if (count == insertIndex) {
                        pluginChildIdx = c;
                        break;
                    }
                    ++count;
                }
            }
            chainTree.addChild(plugin->state, pluginChildIdx, nullptr);
        }
    }

    notifyGraphRebuildNeeded();
}

void DrumGridPlugin::removePluginFromChain(int chainIndex, int pluginIndex) {
    auto* chain = getChainByIndexMutable(chainIndex);
    if (!chain)
        return;
    if (pluginIndex < 0 || pluginIndex >= static_cast<int>(chain->plugins.size()))
        return;

    auto chainTree = findChainTree(chainIndex);
    if (chainTree.isValid()) {
        int count = 0;
        for (int c = 0; c < chainTree.getNumChildren(); ++c) {
            if (chainTree.getChild(c).hasType(te::IDs::PLUGIN)) {
                if (count == pluginIndex) {
                    chainTree.removeChild(c, nullptr);
                    break;
                }
                ++count;
            }
        }
    }

    auto& pluginToRemove = chain->plugins[static_cast<size_t>(pluginIndex)];
    if (pluginToRemove != nullptr && !pluginToRemove->baseClassNeedsInitialising())
        pluginToRemove->baseClassDeinitialise();
    chain->plugins.erase(chain->plugins.begin() + pluginIndex);
    notifyGraphRebuildNeeded();
}

void DrumGridPlugin::movePluginInChain(int chainIndex, int fromIndex, int toIndex) {
    auto* chain = getChainByIndexMutable(chainIndex);
    if (!chain)
        return;

    int count = static_cast<int>(chain->plugins.size());
    if (fromIndex < 0 || fromIndex >= count || toIndex < 0 || toIndex >= count ||
        fromIndex == toIndex)
        return;

    auto plugin = chain->plugins[static_cast<size_t>(fromIndex)];
    chain->plugins.erase(chain->plugins.begin() + fromIndex);
    chain->plugins.insert(chain->plugins.begin() + toIndex, plugin);

    auto chainTree = findChainTree(chainIndex);
    if (chainTree.isValid()) {
        std::vector<int> pluginChildIndices;
        for (int c = 0; c < chainTree.getNumChildren(); ++c) {
            if (chainTree.getChild(c).hasType(te::IDs::PLUGIN))
                pluginChildIndices.push_back(c);
        }

        if (fromIndex < static_cast<int>(pluginChildIndices.size()) &&
            toIndex < static_cast<int>(pluginChildIndices.size())) {
            chainTree.moveChild(pluginChildIndices[static_cast<size_t>(fromIndex)],
                                pluginChildIndices[static_cast<size_t>(toIndex)], nullptr);
        }
    }

    notifyGraphRebuildNeeded();
}

int DrumGridPlugin::getChainPluginCount(int chainIndex) const {
    if (auto* chain = getChainByIndex(chainIndex))
        return static_cast<int>(chain->plugins.size());
    return 0;
}

te::Plugin* DrumGridPlugin::getChainPlugin(int chainIndex, int pluginIndex) const {
    if (auto* chain = getChainByIndex(chainIndex)) {
        if (pluginIndex >= 0 && pluginIndex < static_cast<int>(chain->plugins.size()))
            return chain->plugins[static_cast<size_t>(pluginIndex)].get();
    }
    return nullptr;
}

//==============================================================================
// Legacy pad-level FX API
//==============================================================================

void DrumGridPlugin::addPluginToPad(int padIndex, const juce::PluginDescription& desc,
                                    int insertIndex) {
    if (padIndex < 0 || padIndex >= maxPads)
        return;
    int midiNote = baseNote + padIndex;
    if (auto* chain = findChainForNote(midiNote))
        addPluginToChain(chain->index, desc, insertIndex);
}

void DrumGridPlugin::removePluginFromPad(int padIndex, int pluginIndex) {
    if (padIndex < 0 || padIndex >= maxPads)
        return;
    int midiNote = baseNote + padIndex;
    if (auto* chain = findChainForNote(midiNote))
        removePluginFromChain(chain->index, pluginIndex);
}

void DrumGridPlugin::movePluginInPad(int padIndex, int fromIndex, int toIndex) {
    if (padIndex < 0 || padIndex >= maxPads)
        return;
    int midiNote = baseNote + padIndex;
    if (auto* chain = findChainForNote(midiNote))
        movePluginInChain(chain->index, fromIndex, toIndex);
}

int DrumGridPlugin::getPadPluginCount(int padIndex) const {
    if (padIndex < 0 || padIndex >= maxPads)
        return 0;
    int midiNote = baseNote + padIndex;
    if (auto* chain = getChainForNote(midiNote))
        return static_cast<int>(chain->plugins.size());
    return 0;
}

te::Plugin* DrumGridPlugin::getPadPlugin(int padIndex, int pluginIndex) const {
    if (padIndex < 0 || padIndex >= maxPads)
        return nullptr;
    int midiNote = baseNote + padIndex;
    if (auto* chain = getChainForNote(midiNote)) {
        if (pluginIndex >= 0 && pluginIndex < static_cast<int>(chain->plugins.size()))
            return chain->plugins[static_cast<size_t>(pluginIndex)].get();
    }
    return nullptr;
}

void DrumGridPlugin::setPadTriggered(int padIndex) {
    if (padIndex >= 0 && padIndex < maxPads)
        padTriggered_[padIndex].store(true, std::memory_order_relaxed);
}

bool DrumGridPlugin::consumePadTrigger(int padIndex) {
    if (padIndex < 0 || padIndex >= maxPads)
        return false;
    return padTriggered_[padIndex].exchange(false, std::memory_order_relaxed);
}

std::pair<float, float> DrumGridPlugin::consumeChainPeak(int chainIndex) {
    if (chainIndex < 0 || chainIndex >= maxPads)
        return {0.0f, 0.0f};
    auto& m = chainMeters_[static_cast<size_t>(chainIndex)];
    float l = m.peakL.exchange(0.0f, std::memory_order_relaxed);
    float r = m.peakR.exchange(0.0f, std::memory_order_relaxed);
    return {l, r};
}

void DrumGridPlugin::notifyGraphRebuildNeeded() {
    edit.restartPlayback();
}

//==============================================================================
void DrumGridPlugin::restorePluginStateFromValueTree(const juce::ValueTree& v) {
    for (int i = 0; i < v.getNumProperties(); ++i) {
        auto propName = v.getPropertyName(i);
        state.setProperty(propName, v.getProperty(propName), nullptr);
    }

    mixerExpanded_.forceUpdateOfCachedValue();

    for (int i = 0; i < v.getNumChildren(); ++i) {
        auto childTree = v.getChild(i);
        if (!childTree.hasType(chainTreeId))
            continue;

        int chainIdx = childTree.getProperty(chainIndexId, -1);
        if (chainIdx < 0)
            continue;

        auto* chain = getChainByIndexMutable(chainIdx);
        if (!chain)
            continue;

        chain->level.forceUpdateOfCachedValue();
        chain->pan.forceUpdateOfCachedValue();
        chain->mute.forceUpdateOfCachedValue();
        chain->solo.forceUpdateOfCachedValue();

        for (int p = 0; p < childTree.getNumChildren(); ++p) {
            auto pluginState = childTree.getChild(p);
            if (!pluginState.hasType(te::IDs::PLUGIN))
                continue;
            auto plugin = edit.getPluginCache().getOrCreatePluginFor(pluginState);
            if (plugin) {
                chain->plugins.push_back(plugin);

                if (sampleRate_ > 0.0) {
                    te::PluginInitialisationInfo initInfo;
                    initInfo.startTime = tracktion::TimePosition();
                    initInfo.sampleRate = sampleRate_;
                    initInfo.blockSizeSamples = blockSize_;
                    plugin->baseClassInitialise(initInfo);
                }
            }
        }
    }
}

}  // namespace magda::daw::audio
