#include "plugins/DrumGridPlugin.hpp"

#include <algorithm>
#include <memory>
#include <vector>

#include "core/DrumGridPads.hpp"
#include "core/RackInfo.hpp"

namespace magda::daw::audio {

namespace te = tracktion::engine;

namespace {

void initialisePluginIfNeeded(te::Plugin& plugin, double sampleRate, int blockSize) {
    if (sampleRate <= 0.0)
        return;

    te::PluginInitialisationInfo initInfo;
    initInfo.startTime = tracktion::TimePosition();
    initInfo.sampleRate = sampleRate;
    initInfo.blockSizeSamples = blockSize;
    plugin.baseClassInitialise(initInfo);
}

}  // namespace

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

    // No chains are built here, and none are read out of `state`. They are the
    // model's, and arrive through syncFromModel() once the device exists in it
    // (#2207). A tree that still carries CHAIN children is one a project saved
    // before the pads moved; its pads were read into the model at load, and
    // building them here as well would give the grid each pad twice.
    for (int i = state.getNumChildren(); --i >= 0;)
        if (state.getChild(i).hasType(chainTreeId))
            state.removeChild(i, nullptr);
}

DrumGridPlugin::~DrumGridPlugin() {
    // Stop the reaper before any members are torn down so timerCallback() can't
    // run mid-destruction. The audio thread is already stopped (deinitialise()),
    // so everything still retired is safe to deinit/free now.
    stopTimer();
    drainRetired();
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

    // Publish the initial snapshot now that the child plugins are initialised, so
    // the audio thread has a valid graph to read on the first block.
    publishSnapshot();
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
void DrumGridPlugin::publishSnapshot(std::vector<te::Plugin::Ptr> reapPlugins,
                                     std::vector<std::unique_ptr<Chain>> reapChains) {
    // Build a fresh immutable snapshot from the current message-thread model.
    auto snapshot = std::make_shared<AudioSnapshot>();
    snapshot->reserve(chains_.size());
    for (auto& chain : chains_) {
        AudioChainEntry entry;
        entry.chain = chain.get();
        entry.lowNote = chain->lowNote;    // copy note-range so the audio thread never
        entry.highNote = chain->highNote;  // reads these mutable plain-int Chain fields
        entry.rootNote = chain->rootNote;  // directly (data-race free)
        entry.plugins = chain->plugins;    // copy owning Plugin::Ptrs
        entry.gains = chain->pluginGains;
        snapshot->push_back(std::move(entry));
    }

    // Publish atomically and hand the previous snapshot to the retirement queue.
    // We never block here: the publishing thread returns immediately, and the
    // retired snapshot (plus any plugins/chains removed by this edit, which it
    // still references) is freed later by drainRetired() once the audio thread has
    // released it. This keeps removed objects alive until the audio thread can no
    // longer reach them, without a busy-spin that could hard-hang the message
    // thread if the audio callback ever stalls.
    JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wdeprecated-declarations")
    std::shared_ptr<const AudioSnapshot> previous = std::atomic_exchange_explicit(
        &audioSnapshot_, std::shared_ptr<const AudioSnapshot>(std::move(snapshot)),
        std::memory_order_acq_rel);
    JUCE_END_IGNORE_WARNINGS_GCC_LIKE

    retired_.push_back({std::move(previous), std::move(reapPlugins), std::move(reapChains)});

    drainRetired();  // reap anything already safe
    if (!retired_.empty() && !isTimerRunning())
        startTimerHz(30);  // ensure pending items get reaped even without further edits
}

void DrumGridPlugin::drainRetired() {
    auto deinit = [](const te::Plugin::Ptr& p) {
        if (p != nullptr && !p->baseClassNeedsInitialising())
            p->baseClassDeinitialise();
    };

    for (auto it = retired_.begin(); it != retired_.end();) {
        // A null guard means there was no prior published snapshot (first publish):
        // nothing the audio thread could be holding, so it is immediately safe.
        const bool released = (it->guard == nullptr) || (it->guard.use_count() == 1);
        if (!released) {
            ++it;
            continue;
        }

        // Audio thread has let go of the snapshot that referenced these — deinit on
        // the message thread, then drop (freeing them here, never under the audio
        // callback).
        for (auto& p : it->reapPlugins)
            deinit(p);
        for (auto& c : it->reapChains)
            if (c != nullptr)
                for (auto& p : c->plugins)
                    deinit(p);

        it = retired_.erase(it);
    }

    if (retired_.empty() && isTimerRunning())
        stopTimer();
}

void DrumGridPlugin::timerCallback() {
    drainRetired();
}

void DrumGridPlugin::applyToBuffer(const te::PluginRenderContext& rc) {
    if (!rc.destBuffer || !rc.bufferForMidiMessages)
        return;

    updateParameterStreams(rc.editTime.getStart());

    auto& outputBuffer = *rc.destBuffer;
    auto& inputMidi = *rc.bufferForMidiMessages;
    const int numSamples = rc.bufferNumSamples;
    const int numChannels = outputBuffer.getNumChannels();

    outputBuffer.clear(rc.bufferStartSample, numSamples);

    // Read the immutable published snapshot — never chains_ directly. The
    // shared_ptr copy keeps every chain + plugin alive for the whole block even
    // if the message thread swaps in a new snapshot mid-block (see
    // publishSnapshot()).
    JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wdeprecated-declarations")
    const std::shared_ptr<const AudioSnapshot> snapshot =
        std::atomic_load_explicit(&audioSnapshot_, std::memory_order_acquire);
    JUCE_END_IGNORE_WARNINGS_GCC_LIKE
    if (!snapshot)
        return;

    bool anySoloed = false;
    for (const auto& entry : *snapshot) {
        if (!entry.plugins.empty() && entry.chain->solo.get()) {
            anySoloed = true;
            break;
        }
    }

    for (const auto& entry : *snapshot) {
        if (entry.plugins.empty() || entry.chain->mute.get() || entry.chain->bypassed.get())
            continue;
        if (anySoloed && !entry.chain->solo.get())
            continue;

        processChain(entry, outputBuffer, inputMidi, numSamples, numChannels, rc);
    }
}

void DrumGridPlugin::processChain(const AudioChainEntry& entry,
                                  juce::AudioBuffer<float>& outputBuffer,
                                  const te::MidiMessageArray& inputMidi, int numSamples,
                                  int numChannels, const te::PluginRenderContext& rc) {
    const Chain& chain = *entry.chain;  // CachedValue control reads + metering keys only
    const auto& plugins = entry.plugins;
    const auto& pluginGains = entry.gains;
    // Note-range / remap come from the snapshot, never the mutable Chain (see
    // AudioChainEntry): the message thread can edit these concurrently.
    const int lowNote = entry.lowNote;
    const int rootNote = entry.rootNote;

    // Filter MIDI to this chain's note range and remap
    chainMidi_.clear();
    chainMidi_.isAllNotesOff = inputMidi.isAllNotesOff;

    for (auto& msg : inputMidi) {
        if (msg.isNoteOnOrOff()) {
            int note = msg.getNoteNumber();
            if (note >= lowNote && note <= entry.highNote) {
                if (msg.isNoteOn()) {
                    int padIdx = note - baseNote;
                    if (padIdx >= 0 && padIdx < maxPads)
                        setPadTriggered(padIdx);
                }
                auto remapped = msg;
                remapped.setNoteNumber(rootNote + (note - lowNote));
                chainMidi_.add(remapped);
            }
        } else {
            chainMidi_.add(msg);
        }
    }

    if (chainMidi_.isEmpty() && plugins[0] != nullptr &&
        !plugins[0]->producesAudioWhenNoAudioInput())
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

    int padIdx =
        (lowNote - baseNote >= 0 && lowNote - baseNote < maxPads) ? lowNote - baseNote : -1;

    for (int pi = 0; pi < static_cast<int>(plugins.size()); ++pi) {
        const auto& p = plugins[static_cast<size_t>(pi)];
        if (p != nullptr) {
            p->updateParameterStreams(rc.editTime.getStart());
            p->applyToBufferWithAutomation(chainRc);
        }

        float pluginGain = (pi < static_cast<int>(pluginGains.size()))
                               ? pluginGains[static_cast<size_t>(pi)]
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
    // See DrumGridPlugin::computePadGains for the pan law and why it is linear.
    float leftGain = 0.0f;
    float rightGain = 0.0f;
    computePadGains(levelLinear, panValue, leftGain, rightGain);

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
// Filling the mirror from the model (#2207)
//==============================================================================

juce::String DrumGridPlugin::padStructureFingerprint(const magda::RackInfo& pads) {
    juce::String key;

    // Order matters as much as membership: a pad's devices run in the order the
    // model lists them, so a reorder has to rebuild even though the same ids
    // are still there.
    for (const auto& pad : pads.chains) {
        key << pad.id << ':';
        for (const auto& element : pad.elements)
            if (magda::isDevice(element))
                key << magda::getDevice(element).id << ',';
        key << ';';
    }

    return key;
}

std::unique_ptr<DrumGridPlugin::Chain> DrumGridPlugin::takeChain(int index, bool& created) {
    created = false;

    for (auto it = chains_.begin(); it != chains_.end(); ++it) {
        if ((*it)->index == index) {
            auto existing = std::move(*it);
            chains_.erase(it);
            return existing;
        }
    }

    created = true;

    auto chain = std::make_unique<Chain>();
    chain->index = index;

    // Standalone, and never added to `state`. The CachedValues need a tree to
    // refer to; the pads do not need a second home (#2207).
    chain->tree = juce::ValueTree(chainTreeId);
    chain->tree.setProperty(chainIndexId, index, nullptr);

    auto* um = getUndoManager();
    chain->level.referTo(chain->tree, padLevelId, um, 0.0f);
    chain->pan.referTo(chain->tree, padPanId, um, 0.0f);
    chain->mute.referTo(chain->tree, padMuteId, um, false);
    chain->solo.referTo(chain->tree, padSoloId, um, false);
    chain->bypassed.referTo(chain->tree, padBypassedId, um, false);
    chain->busOutput.referTo(chain->tree, busOutputId, um, 0);

    if (index >= nextChainIndex_)
        nextChainIndex_ = index + 1;

    return chain;
}

bool DrumGridPlugin::applyPadProperties(Chain& chain, const magda::ChainInfo& pad, bool created) {
    // A pad always names its notes. ChainInfo's "every note" default belongs to
    // a plain parallel chain, and the audio thread compares against a range, so
    // it is spelled out rather than left inverted.
    const int low = pad.answersToEveryNote() ? 0 : juce::jlimit(0, 127, pad.lowNote);
    const int high = pad.answersToEveryNote() ? 127 : juce::jlimit(0, 127, pad.highNote);
    const int root = juce::jlimit(0, 127, pad.rootNote);

    const bool rangeMoved =
        chain.lowNote != low || chain.highNote != high || chain.rootNote != root;

    chain.lowNote = low;
    chain.highNote = high;
    chain.rootNote = root;
    chain.name = pad.name;

    chain.tree.setProperty(lowNoteId, low, nullptr);
    chain.tree.setProperty(highNoteId, high, nullptr);
    chain.tree.setProperty(rootNoteId, root, nullptr);
    chain.tree.setProperty(chainNameId, chain.name, nullptr);

    const bool faderMoved = chain.level.get() != pad.volume || chain.pan.get() != pad.pan;

    if (chain.level.get() != pad.volume)
        chain.level = pad.volume;
    if (chain.pan.get() != pad.pan)
        chain.pan = pad.pan;
    if (chain.mute.get() != pad.muted)
        chain.mute = pad.muted;
    if (chain.solo.get() != pad.solo)
        chain.solo = pad.solo;
    if (chain.bypassed.get() != pad.bypassed)
        chain.bypassed = pad.bypassed;

    const int bus = juce::jlimit(0, maxBusOutputs - 1, pad.outputIndex);
    if (chain.busOutput.get() != bus)
        chain.busOutput = bus;

    // Only when the model actually moved a fader, or when the pad is new. Every
    // sync pass reaches here, and a pad's level and pan are automatable
    // parameters of the grid: stamping the model's base value on them each time
    // would overwrite what a lane or a modifier had just written. A new pad is
    // the exception: it writes its defaults without changing anything, and the
    // parameter would otherwise keep whatever the slot held for the pad before.
    if (created || faderMoved || rangeMoved)
        syncParamFromChain(chain);

    return rangeMoved;
}

void DrumGridPlugin::syncPadPlugins(Chain& chain, const magda::ChainInfo& pad,
                                    const PadPluginFactory& makePlugin,
                                    std::vector<te::Plugin::Ptr>& reap) {
    std::vector<te::Plugin::Ptr> ordered;
    std::vector<float> gains;

    for (const auto& element : pad.elements) {
        if (!magda::isDevice(element))
            continue;

        const auto& padDevice = magda::getDevice(element);

        // Kept when the model still names it, so a pad that gained an effect
        // does not rebuild the instrument under it and cut the note it is
        // playing. Matched on the DeviceId the plugin carries, which is the one
        // thing the model and the mirror agree on.
        te::Plugin::Ptr plugin;
        for (auto it = chain.plugins.begin(); it != chain.plugins.end(); ++it) {
            if (*it != nullptr && static_cast<int>((*it)->state.getProperty(pluginDeviceIdProp,
                                                                            -1)) == padDevice.id) {
                plugin = *it;
                chain.plugins.erase(it);
                break;
            }
        }

        if (plugin == nullptr) {
            plugin = makePlugin(padDevice);
            if (plugin == nullptr)
                continue;

            plugin->state.setProperty(pluginDeviceIdProp, padDevice.id, nullptr);

            // Initialised before it can become visible to the audio thread.
            initialisePluginIfNeeded(*plugin, sampleRate_, blockSize_);
        }

        ordered.push_back(plugin);
        gains.push_back(padDevice.gainValue);
    }

    // Whatever the model stopped naming. Handed over rather than freed here:
    // the audio thread may still be running the snapshot that references it.
    for (auto& dropped : chain.plugins)
        if (dropped != nullptr)
            reap.push_back(std::move(dropped));

    chain.plugins = std::move(ordered);
    chain.pluginGains = std::move(gains);

    applyPadPluginState(chain, pad);
}

bool DrumGridPlugin::applyPadPluginState(Chain& chain, const magda::ChainInfo& pad) {
    // A pad device's power and gain are model state like everything else on a
    // pad, so both are written on every sync pass and not only when the plugin
    // is made: neither changes the structure the rebuild keys on, and nothing
    // else writes them now (#2207).
    chain.pluginGains.resize(chain.plugins.size(), 1.0f);

    bool gainMoved = false;

    for (const auto& element : pad.elements) {
        if (!magda::isDevice(element))
            continue;

        const auto& padDevice = magda::getDevice(element);

        for (std::size_t i = 0; i < chain.plugins.size(); ++i) {
            const auto& plugin = chain.plugins[i];
            if (plugin == nullptr ||
                static_cast<int>(plugin->state.getProperty(pluginDeviceIdProp, -1)) != padDevice.id)
                continue;

            plugin->setEnabled(!padDevice.bypassed);

            if (chain.pluginGains[i] != padDevice.gainValue) {
                chain.pluginGains[i] = padDevice.gainValue;
                gainMoved = true;
            }
        }
    }

    return gainMoved;
}

void DrumGridPlugin::syncFromModel(const magda::RackInfo& pads,
                                   const PadPluginFactory& makePlugin) {
    const auto fingerprint = padStructureFingerprint(pads);
    const bool structural = fingerprint != padFingerprint_;

    std::vector<te::Plugin::Ptr> reapPlugins;
    std::vector<std::unique_ptr<Chain>> reapChains;
    bool rangeMoved = false;

    if (structural) {
        // Chains the model no longer has. Detached first, then retired with the
        // publish below, so nothing they own is freed under the audio thread.
        for (auto it = chains_.begin(); it != chains_.end();) {
            const bool kept =
                std::ranges::any_of(pads.chains, [index = (*it)->index](const magda::ChainInfo& p) {
                    return p.id == index;
                });
            if (kept) {
                ++it;
                continue;
            }
            reapChains.push_back(std::move(*it));
            it = chains_.erase(it);
        }

        std::vector<std::unique_ptr<Chain>> ordered;
        ordered.reserve(pads.chains.size());
        for (const auto& pad : pads.chains) {
            bool created = false;
            auto chain = takeChain(pad.id, created);
            rangeMoved |= applyPadProperties(*chain, pad, created);
            syncPadPlugins(*chain, pad, makePlugin, reapPlugins);
            ordered.push_back(std::move(chain));
        }
        chains_ = std::move(ordered);
    } else {
        for (const auto& pad : pads.chains) {
            if (auto* chain = getChainByIndexMutable(pad.id)) {
                rangeMoved |= applyPadProperties(*chain, pad, false);
                // The gains ride in the published snapshot, so a change to one
                // has to be republished the same as a note range.
                rangeMoved |= applyPadPluginState(*chain, pad);
            }
        }
    }

    // The snapshot carries the plugin list and the note ranges, so anything
    // that moved either has to be republished before the audio thread can see
    // it. Nothing else does: level, pan, mute, solo and bus are read live off
    // the Chain the snapshot points at.
    if (structural || rangeMoved)
        publishSnapshot(std::move(reapPlugins), std::move(reapChains));

    if (structural) {
        padFingerprint_ = fingerprint;
        notifyGraphRebuildNeeded();
        notifyChainsChanged();
    }
}

//==============================================================================
// Chain reads
//==============================================================================

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

void DrumGridPlugin::notifyGraphRebuildNeeded() {
    edit.restartPlayback();
}

void DrumGridPlugin::notifyChainsChanged() {
    listeners_.call([this](Listener& l) { l.drumGridChainsChanged(this); });
}

//==============================================================================
void DrumGridPlugin::restorePluginStateFromValueTree(const juce::ValueTree& v) {
    // The grid's own properties, and nothing else. Its CHAIN children are the
    // pads, and the pads are the model's: they arrive through syncFromModel()
    // once the device is in it. Reading them here as well would build every pad
    // twice for a project saved before they moved, and would put the plugin
    // back in charge of what it holds (#2207).
    for (int i = 0; i < v.getNumProperties(); ++i) {
        auto propName = v.getPropertyName(i);
        state.setProperty(propName, v.getProperty(propName), nullptr);
    }

    mixerExpanded_.forceUpdateOfCachedValue();
    multiOutEnabled_.forceUpdateOfCachedValue();
}

void DrumGridPlugin::syncParamFromChain(const Chain& chain) {
    // A pad's level and pan are the grid's own parameters, reached by the pad's
    // bottom note, so a chain whose range starts outside the grid drives none.
    const int padIdx = padIndexFor(chain);
    if (padIdx < 0)
        return;

    const auto idx = static_cast<size_t>(padIdx);

    if (levelParams_[idx] != nullptr)
        levelParams_[idx]->setParameterFromHost(chain.level.get(), juce::dontSendNotification);
    if (panParams_[idx] != nullptr)
        panParams_[idx]->setParameterFromHost(chain.pan.get(), juce::dontSendNotification);
}

}  // namespace magda::daw::audio
