#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

#include "../../../audio/MidiBridge.hpp"
#include "RoutingSelector.hpp"
#include "core/TrackInfo.hpp"
#include "core/TrackManager.hpp"

namespace magda {

/**
 * @brief Free functions for populating and syncing routing selectors.
 *
 * Shared by TrackHeadersPanel and TrackInspector to avoid duplicating
 * ~200 lines of routing UI logic.
 */
namespace RoutingSyncHelper {

inline void populateAudioInputOptions(RoutingSelector* selector, juce::AudioIODevice* device,
                                      TrackId currentTrackId = INVALID_TRACK_ID,
                                      std::map<int, TrackId>* outInputTrackMapping = nullptr,
                                      juce::BigInteger enabledInputChannels = {},
                                      std::map<int, juce::String>* outChannelMapping = nullptr) {
    if (!selector)
        return;

    std::vector<RoutingSelector::RoutingOption> options;

    if (device) {
        // Use enabled channels if provided (from TE WaveDevices), otherwise fall back
        // to JUCE active channels (which may show all channels)
        auto activeInputChannels =
            enabledInputChannels.isZero() ? device->getActiveInputChannels() : enabledInputChannels;
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

            // Stereo pairs (ID 10+)
            int id = 10;
            for (int i = 0; i < activeIndices.size(); i += 2) {
                if (i + 1 < activeIndices.size()) {
                    int ch1 = activeIndices[i] + 1;
                    int ch2 = activeIndices[i + 1] + 1;
                    juce::String pairName = juce::String(ch1) + "-" + juce::String(ch2);
                    options.push_back({id, pairName});
                    // Store as "stereo:In 1" to distinguish from mono "In 1"
                    if (outChannelMapping)
                        (*outChannelMapping)[id] = "stereo:In " + juce::String(ch1);
                    ++id;
                }
            }

            if (activeIndices.size() > 1) {
                options.push_back({0, "", true});  // separator
            }

            // Mono channels (ID 100+)
            id = 100;
            for (int i = 0; i < activeIndices.size(); ++i) {
                int channelNum = activeIndices[i] + 1;
                options.push_back({id, juce::String(channelNum) + " (mono)"});
                if (outChannelMapping)
                    (*outChannelMapping)[id] = "In " + juce::String(channelNum);
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
            if (std::find(descendants.begin(), descendants.end(), t.id) != descendants.end())
                continue;
            if (t.type == TrackType::Audio || t.type == TrackType::Instrument ||
                t.type == TrackType::Group || t.type == TrackType::Aux) {
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
                                       juce::AudioIODevice* device,
                                       std::map<int, TrackId>& outTrackMapping,
                                       juce::BigInteger enabledOutputChannels = {}) {
    if (!selector)
        return;

    std::vector<RoutingSelector::RoutingOption> options;
    options.push_back({1, "Master"});
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
                if (std::find(descendants.begin(), descendants.end(), t.id) != descendants.end())
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
        for (const auto& t : allTracks) {
            if ((t.type == TrackType::Audio || t.type == TrackType::Instrument) &&
                t.id != currentTrackId) {
                if (std::find(descendants.begin(), descendants.end(), t.id) != descendants.end())
                    continue;
                trackOptions.push_back({id++, t.name});
            }
        }
        if (!trackOptions.empty()) {
            options.push_back({0, "", true});
            for (auto& opt : trackOptions)
                options.push_back(std::move(opt));
        }
    }

    // Hardware output channels
    if (device) {
        auto activeOutputChannels = enabledOutputChannels.isZero()
                                        ? device->getActiveOutputChannels()
                                        : enabledOutputChannels;
        int numActiveChannels = activeOutputChannels.countNumberOfSetBits();

        if (numActiveChannels > 0) {
            options.push_back({0, "", true});

            juce::Array<int> activeIndices;
            for (int i = 0; i < activeOutputChannels.getHighestBit() + 1; ++i) {
                if (activeOutputChannels[i]) {
                    activeIndices.add(i);
                }
            }

            int id = 10;
            for (int i = 0; i < activeIndices.size(); i += 2) {
                if (i + 1 < activeIndices.size()) {
                    int ch1 = activeIndices[i] + 1;
                    int ch2 = activeIndices[i + 1] + 1;
                    juce::String pairName = juce::String(ch1) + "-" + juce::String(ch2);
                    options.push_back({id++, pairName});
                }
            }

            if (activeIndices.size() > 1) {
                options.push_back({0, "", true});
            }

            id = 100;
            for (int i = 0; i < activeIndices.size(); ++i) {
                int channelNum = activeIndices[i] + 1;
                options.push_back({id++, juce::String(channelNum) + " (mono)"});
            }
        }
    }

    // Build the track-to-option-id mapping
    outTrackMapping.clear();
    {
        int id = 200;
        for (const auto& t : allTracks) {
            if (t.type == TrackType::Group && t.id != currentTrackId) {
                if (std::find(descendants.begin(), descendants.end(), t.id) != descendants.end())
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
            if ((t.type == TrackType::Audio || t.type == TrackType::Instrument) &&
                t.id != currentTrackId) {
                if (std::find(descendants.begin(), descendants.end(), t.id) != descendants.end())
                    continue;
                outTrackMapping[id++] = t.id;
            }
        }
    }

    selector->setOptions(options);
}

inline void populateMidiInputOptions(RoutingSelector* selector, MidiBridge* midiBridge) {
    if (!selector || !midiBridge)
        return;

    auto midiInputs = midiBridge->getAvailableMidiInputs();

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

    selector->setOptions(options);
}

inline void populateMidiOutputOptions(RoutingSelector* selector, MidiBridge* midiBridge,
                                      std::map<int, TrackId>& outTrackMapping) {
    if (!selector || !midiBridge)
        return;

    auto midiOutputs = midiBridge->getAvailableMidiOutputs();

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
    selector->setOptions(options);
}

inline void syncSelectorsFromTrack(
    const TrackInfo& track, RoutingSelector* audioInSelector, RoutingSelector* midiInSelector,
    RoutingSelector* audioOutSelector, RoutingSelector* midiOutSelector, MidiBridge* midiBridge,
    juce::AudioIODevice* device, TrackId currentTrackId, std::map<int, TrackId>& outputTrackMapping,
    std::map<int, TrackId>& midiOutputTrackMapping,
    std::map<int, TrackId>* inputTrackMapping = nullptr, juce::BigInteger enabledInputChannels = {},
    juce::BigInteger enabledOutputChannels = {},
    std::map<int, juce::String>* inputChannelMapping = nullptr) {
    bool hasAudioInput = !track.audioInputDevice.isEmpty();
    bool hasMidiInput = !track.midiInputDevice.isEmpty();

    // Update Audio Input selector
    if (audioInSelector) {
        if (hasAudioInput) {
            populateAudioInputOptions(audioInSelector, device, currentTrackId, inputTrackMapping,
                                      enabledInputChannels, inputChannelMapping);

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
                } else {
                    // Fallback to first channel option
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
        if (!hasMidiInput) {
            midiInSelector->setSelectedId(2);  // "None"
            midiInSelector->setEnabled(false);
        } else if (track.midiInputDevice == "all") {
            midiInSelector->setSelectedId(1);  // "All Inputs"
            midiInSelector->setEnabled(true);
        } else if (midiBridge) {
            auto midiInputs = midiBridge->getAvailableMidiInputs();
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
        populateAudioOutputOptions(audioOutSelector, currentTrackId, device, outputTrackMapping,
                                   enabledOutputChannels);
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
            audioOutSelector->setEnabled(true);
        }
    }

    // Update MIDI Output selector
    if (midiOutSelector) {
        populateMidiOutputOptions(midiOutSelector, midiBridge, midiOutputTrackMapping);
        juce::String currentMidiOutput = track.midiOutputDevice;
        if (currentMidiOutput.isEmpty()) {
            midiOutSelector->setSelectedId(1);  // "None"
        } else if (midiBridge) {
            auto midiOutputs = midiBridge->getAvailableMidiOutputs();
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

}  // namespace RoutingSyncHelper
}  // namespace magda
