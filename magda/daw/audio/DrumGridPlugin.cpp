#include "DrumGridPlugin.hpp"

#include "MagdaSamplerPlugin.hpp"
#include "core/TrackManager.hpp"

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
const juce::Identifier DrumGridPlugin::padBypassedId("padBypassed");
const juce::Identifier DrumGridPlugin::busOutputId("busOutput");
const juce::Identifier DrumGridPlugin::mixerExpandedId("mixerExpanded");
const juce::Identifier DrumGridPlugin::multiOutEnabledId("multiOutEnabled");
const juce::Identifier DrumGridPlugin::pluginDeviceIdProp("magdaDeviceId");

//==============================================================================
DrumGridPlugin::DrumGridPlugin(const te::PluginCreationInfo& info) : Plugin(info) {
    mixerExpanded_.referTo(state, mixerExpandedId, getUndoManager(), false);
    multiOutEnabled_.referTo(state, multiOutEnabledId, getUndoManager(), false);

    // Register AutomatableParameters for all 64 pads (fixed slots for stable macro/mod indexing)
    for (int i = 0; i < maxPads; ++i) {
        auto padName = "Pad " + juce::String(i + 1);
        levelParams_[static_cast<size_t>(i)] =
            addParam("padLevel" + juce::String(i), padName + " Level", {-60.0f, 12.0f});
        panParams_[static_cast<size_t>(i)] =
            addParam("padPan" + juce::String(i), padName + " Pan", {-1.0f, 1.0f});
    }

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
        chain->bypassed.referTo(childTree, padBypassedId, um, false);
        chain->busOutput.referTo(childTree, busOutputId, um, 0);

        if (chain->index >= nextChainIndex_)
            nextChainIndex_ = chain->index + 1;

        // Sync CachedValues → AutomatableParams for restored chains
        syncParamFromChain(chain->index);

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

    // Pre-allocate stereo scratch buffer to avoid per-callback heap allocs on the audio thread.
    scratchBuffer_.setSize(2, juce::jmax(1, blockSize_), false, false, true);

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

    outputBuffer.clear(rc.bufferStartSample, numSamples);

    bool anySoloed = false;
    for (const auto& chain : chains_) {
        if (!chain->plugins.empty() && chain->solo.get()) {
            anySoloed = true;
            break;
        }
    }

    for (auto& chain : chains_) {
        if (chain->plugins.empty() || chain->mute.get() || chain->bypassed.get())
            continue;
        if (anySoloed && !chain->solo.get())
            continue;

        processChain(*chain, outputBuffer, inputMidi, numSamples, numChannels, rc);
    }
}

void DrumGridPlugin::processChain(Chain& chain, juce::AudioBuffer<float>& outputBuffer,
                                  const te::MidiMessageArray& inputMidi, int numSamples,
                                  int numChannels, const te::PluginRenderContext& rc) {
    // Filter MIDI to this chain's note range and remap
    chainMidi_.clear();
    chainMidi_.isAllNotesOff = inputMidi.isAllNotesOff;

    for (auto& msg : inputMidi) {
        if (msg.isNoteOnOrOff()) {
            int note = msg.getNoteNumber();
            if (note >= chain.lowNote && note <= chain.highNote) {
                if (msg.isNoteOn()) {
                    int padIdx = note - baseNote;
                    if (padIdx >= 0 && padIdx < maxPads)
                        setPadTriggered(padIdx);
                }
                auto remapped = msg;
                remapped.setNoteNumber(chain.rootNote + (note - chain.lowNote));
                chainMidi_.add(remapped);
            }
        } else {
            chainMidi_.add(msg);
        }
    }

    if (chainMidi_.isEmpty() && chain.plugins[0] != nullptr &&
        !chain.plugins[0]->producesAudioWhenNoAudioInput())
        return;

    // Run plugins on a stereo scratch buffer (pre-allocated in initialise()).
    constexpr int scratchChannels = 2;
    if (scratchBuffer_.getNumChannels() < scratchChannels ||
        scratchBuffer_.getNumSamples() < numSamples) {
        // Defensive — host changed block size without re-initialising. Shouldn't happen,
        // but reallocate rather than write past the end.
        scratchBuffer_.setSize(scratchChannels, numSamples, false, false, true);
    }
    scratchBuffer_.clear(0, numSamples);

    te::PluginRenderContext chainRc(
        &scratchBuffer_, juce::AudioChannelSet::canonicalChannelSet(scratchChannels), 0, numSamples,
        &chainMidi_, 0.0, rc.editTime, rc.isPlaying, rc.isScrubbing, rc.isRendering, false);

    int padIdx = padIndexFor(chain);

    for (int pi = 0; pi < static_cast<int>(chain.plugins.size()); ++pi) {
        auto& p = chain.plugins[static_cast<size_t>(pi)];
        if (p != nullptr)
            p->applyToBufferWithAutomation(chainRc);

        float pluginGain = (pi < static_cast<int>(chain.pluginGains.size()))
                               ? chain.pluginGains[static_cast<size_t>(pi)]
                               : 1.0f;
        if (pluginGain != 1.0f)
            scratchBuffer_.applyGain(0, numSamples, pluginGain);

        // Capture per-plugin output peak (post pluginGain, pre level/pan).
        if (padIdx >= 0 && pi < maxFxPerChain) {
            float pl = scratchBuffer_.getMagnitude(0, 0, numSamples);
            float pr = scratchChannels >= 2 ? scratchBuffer_.getMagnitude(1, 0, numSamples) : pl;
            auto& pm = pluginMeters_[static_cast<size_t>(padIdx)][static_cast<size_t>(pi)];
            if (pl > pm.peakL.load(std::memory_order_relaxed))
                pm.peakL.store(pl, std::memory_order_relaxed);
            if (pr > pm.peakR.load(std::memory_order_relaxed))
                pm.peakR.store(pr, std::memory_order_relaxed);
        }
    }

    // Compute level/pan gains (read from AutomatableParam to include macro/mod modulation)
    float levelDb = (padIdx >= 0 && levelParams_[static_cast<size_t>(padIdx)] != nullptr)
                        ? levelParams_[static_cast<size_t>(padIdx)]->getCurrentValue()
                        : chain.level.get();
    float levelLinear = juce::Decibels::decibelsToGain(levelDb);
    float panValue = (padIdx >= 0 && panParams_[static_cast<size_t>(padIdx)] != nullptr)
                         ? panParams_[static_cast<size_t>(padIdx)]->getCurrentValue()
                         : chain.pan.get();
    float leftGain =
        levelLinear * std::cos((panValue + 1.0f) * juce::MathConstants<float>::halfPi * 0.5f);
    float rightGain =
        levelLinear * std::sin((panValue + 1.0f) * juce::MathConstants<float>::halfPi * 0.5f);

    // Route to assigned bus output (stereo pair)
    int busIdx = juce::jlimit(0, maxBusOutputs - 1, chain.busOutput.get());
    int leftCh = busIdx * 2;
    int rightCh = busIdx * 2 + 1;
    if (rightCh >= numChannels) {
        leftCh = 0;
        rightCh = std::min(1, numChannels - 1);
    }

    outputBuffer.addFrom(leftCh, rc.bufferStartSample, scratchBuffer_, 0, 0, numSamples, leftGain);
    if (rightCh < numChannels)
        outputBuffer.addFrom(rightCh, rc.bufferStartSample, scratchBuffer_,
                             scratchBuffer_.getNumChannels() >= 2 ? 1 : 0, 0, numSamples,
                             rightGain);

    // Store chain-out peaks (post level/pan).
    if (padIdx >= 0) {
        float rawL = scratchBuffer_.getMagnitude(0, 0, numSamples);
        float rawR = scratchChannels >= 2 ? scratchBuffer_.getMagnitude(1, 0, numSamples) : rawL;
        float peakL = rawL * leftGain;
        float peakR = rawR * rightGain;

        auto& chainMeter = chainMeters_[static_cast<size_t>(padIdx)];
        if (peakL > chainMeter.peakL.load(std::memory_order_relaxed))
            chainMeter.peakL.store(peakL, std::memory_order_relaxed);
        if (peakR > chainMeter.peakR.load(std::memory_order_relaxed))
            chainMeter.peakR.store(peakR, std::memory_order_relaxed);
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
    chainTree.setProperty(padBypassedId, false, nullptr);
    chainTree.setProperty(busOutputId, 0, nullptr);
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
    chain->bypassed.referTo(chainTree, padBypassedId, um, false);
    chain->busOutput.referTo(chainTree, busOutputId, um, 0);

    syncParamFromChain(idx);
    chains_.push_back(std::move(chain));

    assignBusOutputs();
    notifyGraphRebuildNeeded();
    notifyChainsChanged();
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
    assignBusOutputs();
    notifyGraphRebuildNeeded();
    notifyChainsChanged();
}

const std::vector<std::unique_ptr<DrumGridPlugin::Chain>>& DrumGridPlugin::getChains() const {
    return chains_;
}

int DrumGridPlugin::getPluginDeviceId(int chainIndex, int pluginIndex) const {
    auto* chain = getChainByIndex(chainIndex);
    if (!chain || pluginIndex < 0 || pluginIndex >= static_cast<int>(chain->plugins.size()))
        return -1;
    return chain->plugins[static_cast<size_t>(pluginIndex)]->state.getProperty(pluginDeviceIdProp,
                                                                               -1);
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

void DrumGridPlugin::setChainNoteRange(int chainIndex, int lowNote, int highNote, int rootNote) {
    auto* chain = getChainByIndexMutable(chainIndex);
    if (!chain)
        return;

    chain->lowNote = juce::jlimit(0, 127, lowNote);
    chain->highNote = juce::jlimit(0, 127, highNote);
    chain->rootNote = juce::jlimit(0, 127, rootNote);

    auto chainTree = findChainTree(chainIndex);
    if (chainTree.isValid()) {
        chainTree.setProperty(lowNoteId, chain->lowNote, nullptr);
        chainTree.setProperty(highNoteId, chain->highNote, nullptr);
        chainTree.setProperty(rootNoteId, chain->rootNote, nullptr);
    }
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

    // Assign a stable DeviceId for macro/mod linking
    plugin->state.setProperty(pluginDeviceIdProp,
                              magda::TrackManager::getInstance().allocateDeviceId(), nullptr);

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

    assignBusOutputs();
    notifyGraphRebuildNeeded();
    notifyChainsChanged();
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

    // Assign a stable DeviceId for macro/mod linking
    plugin->state.setProperty(pluginDeviceIdProp,
                              magda::TrackManager::getInstance().allocateDeviceId(), nullptr);

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

    assignBusOutputs();
    notifyGraphRebuildNeeded();
    notifyChainsChanged();
}

void DrumGridPlugin::swapPadChains(int padIndexA, int padIndexB) {
    if (padIndexA < 0 || padIndexA >= maxPads || padIndexB < 0 || padIndexB >= maxPads)
        return;
    if (padIndexA == padIndexB)
        return;

    int noteA = baseNote + padIndexA;
    int noteB = baseNote + padIndexB;

    auto* chainA = findChainForNote(noteA);
    auto* chainB = findChainForNote(noteB);

    if (!chainA && !chainB)
        return;  // Both empty — nothing to do

    if (chainA && chainB) {
        // Both pads have chains — swap their note assignments and names
        auto treeA = findChainTree(chainA->index);
        auto treeB = findChainTree(chainB->index);

        // Swap note ranges
        std::swap(chainA->lowNote, chainB->lowNote);
        std::swap(chainA->highNote, chainB->highNote);
        std::swap(chainA->rootNote, chainB->rootNote);
        std::swap(chainA->name, chainB->name);

        // Sync to ValueTree
        if (treeA.isValid()) {
            treeA.setProperty(lowNoteId, chainA->lowNote, nullptr);
            treeA.setProperty(highNoteId, chainA->highNote, nullptr);
            treeA.setProperty(rootNoteId, chainA->rootNote, nullptr);
            treeA.setProperty(chainNameId, chainA->name, nullptr);
        }
        if (treeB.isValid()) {
            treeB.setProperty(lowNoteId, chainB->lowNote, nullptr);
            treeB.setProperty(highNoteId, chainB->highNote, nullptr);
            treeB.setProperty(rootNoteId, chainB->rootNote, nullptr);
            treeB.setProperty(chainNameId, chainB->name, nullptr);
        }
    } else {
        // Only one pad has a chain — move it to the other pad's note
        auto* src = chainA ? chainA : chainB;
        int dstNote = chainA ? noteB : noteA;

        src->lowNote = dstNote;
        src->highNote = dstNote;
        src->rootNote = dstNote;

        auto tree = findChainTree(src->index);
        if (tree.isValid()) {
            tree.setProperty(lowNoteId, src->lowNote, nullptr);
            tree.setProperty(highNoteId, src->highNote, nullptr);
            tree.setProperty(rootNoteId, src->rootNote, nullptr);
        }
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

    // Assign a stable DeviceId for macro/mod linking
    plugin->state.setProperty(pluginDeviceIdProp,
                              magda::TrackManager::getInstance().allocateDeviceId(), nullptr);

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

void DrumGridPlugin::addInternalPluginToChain(int chainIndex, const juce::String& pluginId,
                                              int insertIndex) {
    auto* chain = getChainByIndexMutable(chainIndex);
    if (!chain)
        return;

    auto plugin = edit.getPluginCache().createNewPlugin(pluginId, {});
    if (!plugin)
        return;

    if (insertIndex < 0 || insertIndex >= static_cast<int>(chain->plugins.size()))
        chain->plugins.push_back(plugin);
    else
        chain->plugins.insert(chain->plugins.begin() + insertIndex, plugin);

    if (sampleRate_ > 0.0) {
        te::PluginInitialisationInfo initInfo;
        initInfo.startTime = tracktion::TimePosition();
        initInfo.sampleRate = sampleRate_;
        initInfo.blockSizeSamples = blockSize_;
        plugin->baseClassInitialise(initInfo);
    }

    // Assign a stable DeviceId for macro/mod linking
    plugin->state.setProperty(pluginDeviceIdProp,
                              magda::TrackManager::getInstance().allocateDeviceId(), nullptr);

    auto chainTree = findChainTree(chainIndex);
    if (chainTree.isValid())
        chainTree.addChild(plugin->state, -1, nullptr);

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
    auto* chain = getChainByIndex(chainIndex);
    if (!chain)
        return {0.0f, 0.0f};
    int padIdx = padIndexFor(*chain);
    if (padIdx < 0)
        return {0.0f, 0.0f};
    auto& m = chainMeters_[static_cast<size_t>(padIdx)];
    float l = m.peakL.exchange(0.0f, std::memory_order_relaxed);
    float r = m.peakR.exchange(0.0f, std::memory_order_relaxed);
    return {l, r};
}

void DrumGridPlugin::setChainPluginGain(int chainIndex, int pluginIndex, float gainLinear) {
    auto* chain = getChainByIndexMutable(chainIndex);
    if (!chain || pluginIndex < 0)
        return;
    if (pluginIndex >= static_cast<int>(chain->pluginGains.size()))
        chain->pluginGains.resize(static_cast<size_t>(pluginIndex + 1), 1.0f);
    chain->pluginGains[static_cast<size_t>(pluginIndex)] = gainLinear;
}

float DrumGridPlugin::getChainPluginGain(int chainIndex, int pluginIndex) const {
    auto* chain = getChainByIndex(chainIndex);
    if (!chain || pluginIndex < 0 || pluginIndex >= static_cast<int>(chain->pluginGains.size()))
        return 1.0f;
    return chain->pluginGains[static_cast<size_t>(pluginIndex)];
}

std::pair<float, float> DrumGridPlugin::consumeChainPluginPeak(int chainIndex, int pluginIndex) {
    if (pluginIndex < 0 || pluginIndex >= maxFxPerChain)
        return {0.0f, 0.0f};
    auto* chain = getChainByIndex(chainIndex);
    if (!chain)
        return {0.0f, 0.0f};
    int padIdx = padIndexFor(*chain);
    if (padIdx < 0)
        return {0.0f, 0.0f};
    auto& m = pluginMeters_[static_cast<size_t>(padIdx)][static_cast<size_t>(pluginIndex)];
    float l = m.peakL.exchange(0.0f, std::memory_order_relaxed);
    float r = m.peakR.exchange(0.0f, std::memory_order_relaxed);
    return {l, r};
}

void DrumGridPlugin::setMultiOutEnabled(bool enabled) {
    if (multiOutEnabled_.get() == enabled)
        return;
    multiOutEnabled_ = enabled;
    if (enabled)
        fullReassignBusOutputs();
    else
        assignBusOutputs();  // resets all to bus 0
    notifyGraphRebuildNeeded();
    notifyChainsChanged();
}

void DrumGridPlugin::setChainBusOutput(int chainIndex, int busIndex) {
    auto* chain = getChainByIndexMutable(chainIndex);
    if (!chain)
        return;

    busIndex = juce::jlimit(0, maxBusOutputs - 1, busIndex);
    if (chain->busOutput.get() == busIndex)
        return;

    chain->busOutput = busIndex;
    notifyGraphRebuildNeeded();
    notifyChainsChanged();
}

void DrumGridPlugin::assignBusOutputs() {
    if (!multiOutEnabled_.get()) {
        // Multi-out disabled → reset all to Main (bus 0)
        for (auto& chain : chains_) {
            if (chain->busOutput.get() != 0)
                chain->busOutput = 0;
        }
        return;
    }

    // Multi-out enabled → only assign a bus to non-empty chains that are still
    // on the main bus (0). This preserves user-selected bus assignments.
    int nextBus = getNextFreeBus();
    for (auto& chain : chains_) {
        if (!chain->plugins.empty() && chain->busOutput.get() == 0) {
            chain->busOutput = juce::jmin(nextBus, maxBusOutputs - 1);
            if (nextBus < maxBusOutputs - 1)
                ++nextBus;
        }
    }
}

void DrumGridPlugin::fullReassignBusOutputs() {
    // Called only when multi-out is first enabled — assigns sequential buses
    // to all non-empty chains, overwriting any existing assignments.
    int nextBus = 1;
    for (auto& chain : chains_) {
        int newBus = 0;
        if (!chain->plugins.empty()) {
            newBus = juce::jmin(nextBus, maxBusOutputs - 1);
            if (nextBus < maxBusOutputs - 1)
                ++nextBus;
        }
        if (chain->busOutput.get() != newBus)
            chain->busOutput = newBus;
    }
}

int DrumGridPlugin::getNextFreeBus() const {
    int maxBus = 0;
    for (const auto& chain : chains_) {
        int bus = chain->busOutput.get();
        if (bus > maxBus)
            maxBus = bus;
    }
    return maxBus + 1;
}

int DrumGridPlugin::getActiveBusCount() const {
    int count = 0;
    for (const auto& chain : chains_) {
        if (!chain->plugins.empty() && chain->busOutput.get() > 0)
            ++count;
    }
    return count;
}

void DrumGridPlugin::notifyGraphRebuildNeeded() {
    edit.restartPlayback();
}

void DrumGridPlugin::notifyChainsChanged() {
    listeners_.call([this](Listener& l) { l.drumGridChainsChanged(this); });
}

//==============================================================================
void DrumGridPlugin::restorePluginStateFromValueTree(const juce::ValueTree& v) {
    // Copy top-level properties
    for (int i = 0; i < v.getNumProperties(); ++i) {
        auto propName = v.getPropertyName(i);
        state.setProperty(propName, v.getProperty(propName), nullptr);
    }

    mixerExpanded_.forceUpdateOfCachedValue();
    multiOutEnabled_.forceUpdateOfCachedValue();

    // Copy CHAIN children into state ValueTree, then create Chain objects
    for (int i = 0; i < v.getNumChildren(); ++i) {
        auto childTree = v.getChild(i);
        if (!childTree.hasType(chainTreeId))
            continue;

        int chainIdx = childTree.getProperty(chainIndexId, -1);
        if (chainIdx < 0)
            continue;

        // Add the CHAIN child to the plugin's state ValueTree
        // (the constructor reads from state, but we're called after construction)
        auto chainCopy = childTree.createCopy();
        state.addChild(chainCopy, -1, nullptr);

        // Create the Chain object (same as constructor logic)
        auto chain = std::make_unique<Chain>();
        chain->index = chainCopy.getProperty(chainIndexId, 0);
        chain->lowNote = chainCopy.getProperty(lowNoteId, 60);
        chain->highNote = chainCopy.getProperty(highNoteId, 60);
        chain->rootNote = chainCopy.getProperty(rootNoteId, 60);
        chain->name = chainCopy.getProperty(chainNameId, "").toString();

        auto um = getUndoManager();
        chain->level.referTo(chainCopy, padLevelId, um, 0.0f);
        chain->pan.referTo(chainCopy, padPanId, um, 0.0f);
        chain->mute.referTo(chainCopy, padMuteId, um, false);
        chain->solo.referTo(chainCopy, padSoloId, um, false);
        chain->bypassed.referTo(chainCopy, padBypassedId, um, false);
        chain->busOutput.referTo(chainCopy, busOutputId, um, 0);

        if (chain->index >= nextChainIndex_)
            nextChainIndex_ = chain->index + 1;

        // Restore plugins from CHAIN children
        for (int p = 0; p < chainCopy.getNumChildren(); ++p) {
            auto pluginState = chainCopy.getChild(p);
            if (!pluginState.hasType(te::IDs::PLUGIN))
                continue;
            auto plugin = edit.getPluginCache().getOrCreatePluginFor(pluginState);
            if (plugin) {
                // Ensure a stable DeviceId exists for macro/mod linking
                if (!pluginState.hasProperty(pluginDeviceIdProp)) {
                    pluginState.setProperty(pluginDeviceIdProp,
                                            magda::TrackManager::getInstance().allocateDeviceId(),
                                            nullptr);
                } else {
                    int restoredId = pluginState.getProperty(pluginDeviceIdProp);
                    magda::TrackManager::getInstance().ensureDeviceIdAbove(restoredId);
                }

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

        syncParamFromChain(chain->index);
        chains_.push_back(std::move(chain));
    }

    assignBusOutputs();
}

//==============================================================================
// AutomatableParameter sync
//==============================================================================

void DrumGridPlugin::syncParamFromChain(int chainIndex) {
    // Find the chain's CachedValues via its ValueTree
    auto chainTree = findChainTree(chainIndex);
    if (!chainTree.isValid())
        return;

    int lowNote = chainTree.getProperty(lowNoteId, -1);
    int padIdx = lowNote - baseNote;
    if (padIdx < 0 || padIdx >= maxPads)
        return;
    auto idx = static_cast<size_t>(padIdx);

    float level = chainTree.getProperty(padLevelId, 0.0f);
    float pan = chainTree.getProperty(padPanId, 0.0f);

    if (levelParams_[idx] != nullptr)
        levelParams_[idx]->setParameter(level, juce::dontSendNotification);
    if (panParams_[idx] != nullptr)
        panParams_[idx]->setParameter(pan, juce::dontSendNotification);
}

void DrumGridPlugin::valueTreePropertyChanged(juce::ValueTree& tree,
                                              const juce::Identifier& property) {
    // Only respond to chain subtree changes (not the plugin root)
    if (!tree.hasType(chainTreeId))
        return;

    int lowNote = tree.getProperty(lowNoteId, -1);
    int padIdx = lowNote - baseNote;
    if (padIdx < 0 || padIdx >= maxPads)
        return;

    auto idx = static_cast<size_t>(padIdx);

    if (property == padLevelId && levelParams_[idx] != nullptr) {
        float val = tree.getProperty(padLevelId, 0.0f);
        levelParams_[idx]->setParameter(val, juce::dontSendNotification);
    } else if (property == padPanId && panParams_[idx] != nullptr) {
        float val = tree.getProperty(padPanId, 0.0f);
        panParams_[idx]->setParameter(val, juce::dontSendNotification);
    }
}

}  // namespace magda::daw::audio
