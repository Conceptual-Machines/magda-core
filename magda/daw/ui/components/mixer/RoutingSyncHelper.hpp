#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <algorithm>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "../../../audio/MidiBridge.hpp"
#include "../../../audio/io/HardwareChannels.hpp"
#include "../../utils/ChannelLabels.hpp"
#include "RoutingSelector.hpp"
#include "core/TechnicalText.hpp"
#include "core/TrackInfo.hpp"
#include "core/TrackManager.hpp"

/**
 * @brief Free functions for populating and syncing routing selectors.
 *
 * Shared by TrackHeadersPanel and TrackInspector to avoid duplicating
 * ~200 lines of routing UI logic.
 */
namespace magda::RoutingSyncHelper {

/// The entry for a saved hardware route that no open channel carries, past every other id.
constexpr int kMissingRouteId = 100000;

/**
 * @brief Offer @p route as missing and return its id, so the menu shows where the track
 * still points rather than a channel it does not use (#2748).
 */
inline int addMissingRoute(RoutingSelector& selector, const juce::String& route,
                           std::map<int, juce::String>& channelMapping) {
    const auto name = route.startsWith("stereo:") ? route.substring(7) : route;
    selector.addOption({0, "", true});
    selector.addOption({kMissingRouteId, name + " (missing)"});
    channelMapping[kMissingRouteId] = route;
    return kMissingRouteId;
}

/** @brief One direction of @p hardware, or nothing when no interface is open. */
inline std::optional<HardwareChannels::Direction> openDirection(const HardwareChannels* hardware,
                                                                bool inputs) {
    if (hardware == nullptr || !hardware->isOpen())
        return std::nullopt;
    return inputs ? hardware->inputs() : hardware->outputs();
}

inline void populateAudioInputOptions(RoutingSelector* selector,
                                      const std::optional<HardwareChannels::Direction>& inputs,
                                      TrackId currentTrackId = INVALID_TRACK_ID,
                                      std::map<int, TrackId>* outInputTrackMapping = nullptr,
                                      std::map<int, juce::String>* outChannelMapping = nullptr) {
    if (!selector)
        return;

    std::vector<RoutingSelector::RoutingOption> options;

    if (inputs) {
        const auto& activeInputChannels = inputs->open;
        options.push_back({1, "None"});

        int numActiveChannels = activeInputChannels.countNumberOfSetBits();

        if (numActiveChannels > 0) {
            options.push_back({0, "", true});  // separator

            juce::Array<int> activeIndices;
            for (int i = 0; i < activeInputChannels.getHighestBit() + 1; ++i) {
                if (activeInputChannels[i]) {
                    activeIndices.add(i);
                }
            }

            if (outChannelMapping)
                outChannelMapping->clear();

            // The name a saved route stores for a channel, falling back to "In N".
            auto getDeviceName = [&](int channelIndex) -> juce::String {
                auto it = inputs->routeNames.find(channelIndex);
                if (it != inputs->routeNames.end())
                    return it->second;
                return "In " + juce::String(channelIndex + 1);
            };

            const auto& channelNames = inputs->channelNames;

            // Stereo pairs (ID 10+)
            int id = 10;
            for (int i = 0; i < activeIndices.size(); i += 2) {
                if (i + 1 < activeIndices.size()) {
                    options.push_back(
                        {id,
                         ChannelLabels::pair(channelNames, activeIndices[i], activeIndices[i + 1]),
                         false,
                         {activeIndices[i], activeIndices[i + 1]}});
                    // The name a saved route stores
                    if (outChannelMapping)
                        (*outChannelMapping)[id] = "stereo:" + getDeviceName(activeIndices[i]);
                    ++id;
                }
            }

            if (activeIndices.size() > 1) {
                options.push_back({0, "", true});  // separator
            }

            // Mono channels (ID 100+)
            id = 100;
            for (int activeIndice : activeIndices) {
                options.push_back(
                    {id, ChannelLabels::mono(channelNames, activeIndice), false, {activeIndice}});
                if (outChannelMapping)
                    (*outChannelMapping)[id] = getDeviceName(activeIndice);
                ++id;
            }
        }
    } else {
        options.push_back({1, "None"});
        options.push_back({2, "(No Device Active)"});
    }

    // Add tracks as audio input sources (resampling) — ID 200+
    if (currentTrackId != INVALID_TRACK_ID) {
        auto& trackManager = TrackManager::getInstance();
        const auto& allTracks = trackManager.getTracks();

        // Collect descendants to prevent routing cycles
        std::vector<TrackId> descendants = trackManager.getAllDescendants(currentTrackId);

        if (outInputTrackMapping)
            outInputTrackMapping->clear();

        std::vector<RoutingSelector::RoutingOption> trackOptions;
        int id = 200;
        for (const auto& t : allTracks) {
            if (t.id == currentTrackId)
                continue;
            if (std::ranges::contains(descendants, t.id))
                continue;
            if (trackManager.wouldCreateInputRoutingCycle(currentTrackId, t.id))
                continue;
            if (t.type == TrackType::Media || t.type == TrackType::Group ||
                t.type == TrackType::Aux) {
                trackOptions.push_back({id, t.name});
                if (outInputTrackMapping)
                    (*outInputTrackMapping)[id] = t.id;
                ++id;
            }
        }
        if (!trackOptions.empty()) {
            options.push_back({0, "", true});  // separator
            for (auto& opt : trackOptions)
                options.push_back(std::move(opt));
        }
    }

    selector->setOptions(options);
}

inline void populateAudioOutputOptions(RoutingSelector* selector, TrackId currentTrackId,
                                       const std::optional<HardwareChannels::Direction>& outputs,
                                       std::map<int, TrackId>& outTrackMapping,
                                       std::map<int, juce::String>* outChannelMapping = nullptr) {
    if (!selector)
        return;

    if (outChannelMapping)
        outChannelMapping->clear();

    std::vector<RoutingSelector::RoutingOption> options;
    options.push_back({1, technicalText(TechnicalTextToken::Master)});
    options.push_back({2, "None"});

    auto& trackManager = TrackManager::getInstance();
    const auto& allTracks = trackManager.getTracks();

    // Collect descendants to prevent routing cycles
    std::vector<TrackId> descendants;
    if (currentTrackId != INVALID_TRACK_ID) {
        descendants = trackManager.getAllDescendants(currentTrackId);
    }

    // Group tracks (ID 200+)
    {
        std::vector<RoutingSelector::RoutingOption> groupOptions;
        int id = 200;
        for (const auto& t : allTracks) {
            if (t.type == TrackType::Group && t.id != currentTrackId) {
                if (std::ranges::contains(descendants, t.id))
                    continue;
                groupOptions.push_back({id++, t.name});
            }
        }
        if (!groupOptions.empty()) {
            options.push_back({0, "", true});
            for (auto& opt : groupOptions)
                options.push_back(std::move(opt));
        }
    }

    // Aux tracks (ID 300+)
    {
        std::vector<RoutingSelector::RoutingOption> auxOptions;
        int id = 300;
        for (const auto& t : allTracks) {
            if (t.type == TrackType::Aux && t.id != currentTrackId) {
                auxOptions.push_back({id++, t.name});
            }
        }
        if (!auxOptions.empty()) {
            options.push_back({0, "", true});
            for (auto& opt : auxOptions)
                options.push_back(std::move(opt));
        }
    }

    // Audio/Instrument tracks (ID 400+)
    {
        std::vector<RoutingSelector::RoutingOption> trackOptions;
        int id = 400;

        // If the current track is a multi-out, hide its source track from the list
        TrackId multiOutSourceId = INVALID_TRACK_ID;
        if (currentTrackId != INVALID_TRACK_ID) {
            auto* currentTrack = trackManager.getTrack(currentTrackId);
            if (currentTrack && currentTrack->multiOutLink)
                multiOutSourceId = currentTrack->multiOutLink->sourceTrackId;
        }

        for (const auto& t : allTracks) {
            if (t.type != TrackType::Media || t.id == currentTrackId)
                continue;
            // Hide source track from its own multi-out tracks
            if (t.id == multiOutSourceId)
                continue;
            if (std::ranges::contains(descendants, t.id))
                continue;
            trackOptions.push_back({id++, t.name});
        }
        if (!trackOptions.empty()) {
            options.push_back({0, "", true});
            for (auto& opt : trackOptions)
                options.push_back(std::move(opt));
        }
    }

    // Hardware output channels
    if (outputs) {
        const auto& activeOutputChannels = outputs->open;
        const auto& routeNames = outputs->routeNames;
        int numActiveChannels = activeOutputChannels.countNumberOfSetBits();

        if (numActiveChannels > 0) {
            juce::Array<int> activeIndices;
            for (int i = 0; i < activeOutputChannels.getHighestBit() + 1; ++i) {
                if (activeOutputChannels[i]) {
                    activeIndices.add(i);
                }
            }

            // Group channels by route name: consecutive open channels sharing
            // one form a stereo pair or a mono channel. Only whole groups are
            // offered, so a "mono" option only exists where a mono group does.
            struct DeviceGroup {
                juce::String name;
                juce::Array<int> channels;
            };
            std::vector<DeviceGroup> groups;
            if (!routeNames.empty()) {
                for (int idx : activeIndices) {
                    auto it = routeNames.find(idx);
                    juce::String name =
                        it != routeNames.end() ? it->second : "Out " + juce::String(idx + 1);
                    if (!groups.empty() && groups.back().name == name &&
                        groups.back().channels.size() < 2)
                        groups.back().channels.add(idx);
                    else
                        groups.push_back({name, {idx}});
                }
            } else {
                // No route names: assume the default stereo pairing
                for (int i = 0; i < activeIndices.size(); ++i) {
                    juce::String name = "Out " + juce::String(activeIndices[i] + 1);
                    if (i + 1 < activeIndices.size()) {
                        groups.push_back({name, {activeIndices[i], activeIndices[i + 1]}});
                        ++i;
                    } else {
                        groups.push_back({name, {activeIndices[i]}});
                    }
                }
            }

            options.push_back({0, "", true});

            int stereoCount = 0, monoCount = 0;
            for (const auto& g : groups)
                (g.channels.size() == 2 ? stereoCount : monoCount)++;

            const auto& channelNames = outputs->channelNames;

            int id = 10;
            for (const auto& g : groups) {
                if (g.channels.size() != 2)
                    continue;
                options.push_back(
                    {id, ChannelLabels::pair(channelNames, g.channels[0], g.channels[1])});
                if (outChannelMapping)
                    (*outChannelMapping)[id] = "stereo:" + g.name;
                ++id;
            }

            if (stereoCount > 0 && monoCount > 0) {
                options.push_back({0, "", true});
            }

            id = 100;
            for (const auto& g : groups) {
                if (g.channels.size() != 1)
                    continue;
                options.push_back({id, ChannelLabels::mono(channelNames, g.channels[0])});
                if (outChannelMapping)
                    (*outChannelMapping)[id] = g.name;
                ++id;
            }
        }
    }

    // Build the track-to-option-id mapping
    outTrackMapping.clear();
    {
        int id = 200;
        for (const auto& t : allTracks) {
            if (t.type == TrackType::Group && t.id != currentTrackId) {
                if (std::ranges::contains(descendants, t.id))
                    continue;
                outTrackMapping[id++] = t.id;
            }
        }
        id = 300;
        for (const auto& t : allTracks) {
            if (t.type == TrackType::Aux && t.id != currentTrackId) {
                outTrackMapping[id++] = t.id;
            }
        }
        id = 400;
        for (const auto& t : allTracks) {
            if (t.type == TrackType::Media && t.id != currentTrackId) {
                if (std::ranges::contains(descendants, t.id))
                    continue;
                outTrackMapping[id++] = t.id;
            }
        }
    }

    selector->setOptions(options);
}

inline void populateMidiInputOptions(RoutingSelector* selector,
                                     TrackId currentTrackId = INVALID_TRACK_ID,
                                     std::map<int, TrackId>* outMidiInputTrackMapping = nullptr) {
    if (!selector)
        return;

    auto midiInputs = MidiBridge::getInstance().getAvailableMidiInputs();

    std::vector<RoutingSelector::RoutingOption> options;
    options.push_back({1, "All Inputs"});
    options.push_back({2, "None"});

    if (!midiInputs.empty()) {
        options.push_back({0, "", true});

        int id = 10;
        for (const auto& device : midiInputs) {
            options.push_back({id++, device.name});
        }
    }

    // Add tracks as MIDI input sources — ID 200+
    if (currentTrackId != INVALID_TRACK_ID && outMidiInputTrackMapping) {
        auto& trackManager = TrackManager::getInstance();
        const auto& allTracks = trackManager.getTracks();

        outMidiInputTrackMapping->clear();

        std::vector<RoutingSelector::RoutingOption> trackOptions;
        int id = 200;
        for (const auto& t : allTracks) {
            if (t.id == currentTrackId)
                continue;
            // Chord tracks are valid MIDI sources: their progression clip
            // plays live and drives instrument tracks (issue #1507)
            if (t.type != TrackType::Media && t.type != TrackType::Chord)
                continue;
            if (trackManager.wouldCreateInputRoutingCycle(currentTrackId, t.id))
                continue;
            trackOptions.push_back({id, t.name});
            (*outMidiInputTrackMapping)[id] = t.id;
            ++id;
        }
        if (!trackOptions.empty()) {
            options.push_back({0, "", true});  // separator
            for (auto& opt : trackOptions)
                options.push_back(std::move(opt));
        }
    }

    selector->setOptions(options);
}

inline void populateMidiOutputOptions(RoutingSelector* selector,
                                      std::map<int, TrackId>& outTrackMapping,
                                      TrackId currentTrackId = INVALID_TRACK_ID) {
    if (!selector)
        return;

    auto midiOutputs = MidiBridge::getAvailableMidiOutputs();

    std::vector<RoutingSelector::RoutingOption> options;
    options.push_back({1, "None"});

    if (!midiOutputs.empty()) {
        options.push_back({0, "", true});

        int id = 10;
        for (const auto& device : midiOutputs) {
            options.push_back({id++, device.name});
        }
    }

    outTrackMapping.clear();

    // Add tracks as MIDI destinations ("MIDI To track") — ID 200+
    if (currentTrackId != INVALID_TRACK_ID) {
        auto& trackManager = TrackManager::getInstance();
        const auto& allTracks = trackManager.getTracks();

        std::vector<RoutingSelector::RoutingOption> trackOptions;
        int id = 200;
        for (const auto& t : allTracks) {
            if (t.id == currentTrackId)
                continue;
            if (t.type != TrackType::Media)
                continue;
            // The edge created is candidate ← current (candidate listens to
            // the current track), so the candidate is the destination here.
            if (trackManager.wouldCreateInputRoutingCycle(t.id, currentTrackId))
                continue;
            trackOptions.push_back({id, t.name});
            outTrackMapping[id] = t.id;
            ++id;
        }
        if (!trackOptions.empty()) {
            options.push_back({0, "", true});  // separator
            for (auto& opt : trackOptions)
                options.push_back(std::move(opt));
        }
    }

    selector->setOptions(options);
}

inline void syncSelectorsFromTrack(const TrackInfo& track, RoutingSelector* audioInSelector,
                                   RoutingSelector* midiInSelector,
                                   RoutingSelector* audioOutSelector,
                                   RoutingSelector* midiOutSelector,
                                   const HardwareChannels* hardware, TrackId currentTrackId,
                                   std::map<int, TrackId>& outputTrackMapping,
                                   std::map<int, TrackId>& midiOutputTrackMapping,
                                   std::map<int, TrackId>* inputTrackMapping = nullptr,
                                   std::map<int, juce::String>* inputChannelMapping = nullptr,
                                   std::map<int, TrackId>* midiInputTrackMapping = nullptr,
                                   std::map<int, juce::String>* outputChannelMapping = nullptr) {
    const auto inputs = openDirection(hardware, true);
    const auto outputs = openDirection(hardware, false);
    bool hasAudioInput = !track.audioInputDevice.isEmpty();
    bool hasMidiInput = !track.midiInputDevice.isEmpty();

    // Update Audio Input selector
    if (audioInSelector) {
        // Always re-populate with the current track context so internal-track
        // options are available before any track input is selected — otherwise
        // the first "track:" selection could never be made.
        populateAudioInputOptions(audioInSelector, inputs, currentTrackId, inputTrackMapping,
                                  inputChannelMapping);
        if (hasAudioInput) {
            if (track.audioInputDevice.startsWith("track:") && inputTrackMapping) {
                // Track-as-input: find the matching option ID
                TrackId sourceId =
                    track.audioInputDevice.fromFirstOccurrenceOf("track:", false, false)
                        .getIntValue();
                int optionId = 1;
                for (const auto& [oid, tid] : *inputTrackMapping) {
                    if (tid == sourceId) {
                        optionId = oid;
                        break;
                    }
                }
                audioInSelector->setSelectedId(optionId);
            } else if (inputChannelMapping) {
                // Find the option ID matching the stored device name
                int optionId = -1;
                for (const auto& [oid, name] : *inputChannelMapping) {
                    if (name == track.audioInputDevice) {
                        optionId = oid;
                        break;
                    }
                }
                if (optionId > 0) {
                    audioInSelector->setSelectedId(optionId);
                } else if (track.audioInputDevice != "default") {
                    audioInSelector->setSelectedId(addMissingRoute(
                        *audioInSelector, track.audioInputDevice, *inputChannelMapping));
                } else {
                    // "default" reads the first channel, which is what the host plays
                    int firstChannel = audioInSelector->getFirstChannelOptionId();
                    audioInSelector->setSelectedId(firstChannel > 0 ? firstChannel : 1);
                }
            } else {
                int firstChannel = audioInSelector->getFirstChannelOptionId();
                audioInSelector->setSelectedId(firstChannel > 0 ? firstChannel : 1);
            }
            audioInSelector->setEnabled(true);
        } else {
            audioInSelector->setSelectedId(1);  // "None"
            audioInSelector->setEnabled(false);
        }
    }

    // Update MIDI Input selector
    if (midiInSelector) {
        // Always re-populate with the current track context (see audio note above).
        populateMidiInputOptions(midiInSelector, currentTrackId, midiInputTrackMapping);
        if (!hasMidiInput) {
            midiInSelector->setSelectedId(2);  // "None"
            midiInSelector->setEnabled(false);
        } else if (track.midiInputDevice == "all") {
            midiInSelector->setSelectedId(1);  // "All Inputs"
            midiInSelector->setEnabled(true);
        } else if (track.midiInputDevice.startsWith("track:") && midiInputTrackMapping) {
            TrackId sourceId =
                track.midiInputDevice.fromFirstOccurrenceOf("track:", false, false).getIntValue();
            int selectedId = 2;
            for (const auto& [oid, tid] : *midiInputTrackMapping) {
                if (tid == sourceId) {
                    selectedId = oid;
                    break;
                }
            }
            midiInSelector->setSelectedId(selectedId);
            midiInSelector->setEnabled(selectedId != 2);
        } else {
            auto midiInputs = MidiBridge::getInstance().getAvailableMidiInputs();
            int selectedId = 2;
            for (size_t i = 0; i < midiInputs.size(); ++i) {
                if (midiInputs[i].id == track.midiInputDevice) {
                    selectedId = 10 + static_cast<int>(i);
                    break;
                }
            }
            midiInSelector->setSelectedId(selectedId);
            midiInSelector->setEnabled(selectedId != 2);
        }
    }

    // Update Audio Output selector
    if (audioOutSelector) {
        populateAudioOutputOptions(audioOutSelector, currentTrackId, outputs, outputTrackMapping,
                                   outputChannelMapping);
        juce::String currentAudioOutput = track.audioOutputDevice;
        if (currentAudioOutput.isEmpty()) {
            audioOutSelector->setSelectedId(2);  // "None"
            audioOutSelector->setEnabled(false);
        } else if (currentAudioOutput == "master") {
            audioOutSelector->setSelectedId(1);  // Master
            audioOutSelector->setEnabled(true);
        } else if (currentAudioOutput.startsWith("track:")) {
            TrackId destId =
                currentAudioOutput.fromFirstOccurrenceOf("track:", false, false).getIntValue();
            int optionId = -1;
            for (const auto& [oid, tid] : outputTrackMapping) {
                if (tid == destId) {
                    optionId = oid;
                    break;
                }
            }
            if (optionId > 0) {
                audioOutSelector->setSelectedId(optionId);
            }
            audioOutSelector->setEnabled(true);
        } else {
            // Hardware output device — find the option whose mapped device
            // string matches so the dropdown doesn't snap back to Master
            if (outputChannelMapping) {
                int optionId = -1;
                for (const auto& [oid, name] : *outputChannelMapping) {
                    if (name == currentAudioOutput) {
                        optionId = oid;
                        break;
                    }
                }

                juce::String canonicalAlias;
                if (optionId < 0) {
                    const auto pairAlias = "stereo:" + currentAudioOutput;
                    if (std::ranges::any_of(*outputChannelMapping, [&](const auto& entry) {
                            return entry.second == pairAlias;
                        })) {
                        canonicalAlias = pairAlias;
                    }
                }

                if (optionId < 0 && canonicalAlias.isEmpty() && outputs &&
                    !outputs->routeNames.empty()) {
                    const auto& active = outputs->open;
                    const auto& routeNames = outputs->routeNames;
                    juce::Array<int> channels;
                    for (auto channel = 0; channel <= active.getHighestBit(); ++channel)
                        if (active[channel])
                            channels.add(channel);

                    for (auto index = 0; index + 1 < channels.size(); index += 2) {
                        const auto first = channels[index];
                        if (currentAudioOutput != "stereo:Out " + juce::String(first + 1))
                            continue;

                        const auto left = routeNames.find(first);
                        const auto right = routeNames.find(channels[index + 1]);
                        if (left != routeNames.end() && right != routeNames.end() &&
                            left->second == right->second)
                            canonicalAlias = "stereo:" + left->second;
                        break;
                    }
                }

                if (optionId < 0 && canonicalAlias.isNotEmpty()) {
                    auto aliasOption = -1;
                    for (const auto& [oid, name] : *outputChannelMapping) {
                        if (name == canonicalAlias) {
                            if (aliasOption > 0) {
                                aliasOption = -1;
                                break;
                            }
                            aliasOption = oid;
                        }
                    }
                    optionId = aliasOption;
                }
                if (optionId < 0)
                    optionId = addMissingRoute(*audioOutSelector, currentAudioOutput,
                                               *outputChannelMapping);
                audioOutSelector->setSelectedId(optionId);
            }
            audioOutSelector->setEnabled(true);
        }
    }

    // Update MIDI Output selector
    if (midiOutSelector) {
        // Always re-populate with the current track context (see audio note above).
        populateMidiOutputOptions(midiOutSelector, midiOutputTrackMapping, currentTrackId);

        // Mirror view of internal MIDI routing: if another track listens to
        // this track ("track:<id>" MIDI input), show that destination on the
        // out selector. With destination-side fan-out only the first listener
        // is shown — acceptable.
        TrackId listenerId = INVALID_TRACK_ID;
        if (currentTrackId != INVALID_TRACK_ID) {
            const juce::String trackInputId = "track:" + juce::String(currentTrackId);
            for (const auto& t : TrackManager::getInstance().getTracks()) {
                if (t.id != currentTrackId && t.midiInputDevice == trackInputId) {
                    listenerId = t.id;
                    break;
                }
            }
        }

        juce::String currentMidiOutput = track.midiOutputDevice;
        if (listenerId != INVALID_TRACK_ID) {
            int selectedId = 1;  // "None" fallback
            for (const auto& [oid, tid] : midiOutputTrackMapping) {
                if (tid == listenerId) {
                    selectedId = oid;
                    break;
                }
            }
            midiOutSelector->setSelectedId(selectedId);
            midiOutSelector->setEnabled(selectedId != 1);
        } else if (currentMidiOutput.isEmpty()) {
            midiOutSelector->setSelectedId(1);  // "None"
        } else {
            auto midiOutputs = MidiBridge::getAvailableMidiOutputs();
            int selectedId = 1;
            for (size_t i = 0; i < midiOutputs.size(); ++i) {
                if (midiOutputs[i].id == currentMidiOutput) {
                    selectedId = 10 + static_cast<int>(i);
                    break;
                }
            }
            midiOutSelector->setSelectedId(selectedId);
            midiOutSelector->setEnabled(true);
        }
    }
}

}  // namespace magda::RoutingSyncHelper
