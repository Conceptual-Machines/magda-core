#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <map>
#include <vector>

#include "../../audio/MidiBridge.hpp"
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

inline void populateAudioInputOptions(RoutingSelector* selector, juce::AudioIODevice* device) {
    if (!selector)
        return;

    std::vector<RoutingSelector::RoutingOption> options;

    if (device) {
        auto activeInputChannels = device->getActiveInputChannels();
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

            // Stereo pairs (ID 10+)
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
                options.push_back({0, "", true});  // separator
            }

            // Mono channels (ID 100+)
            id = 100;
            for (int i = 0; i < activeIndices.size(); ++i) {
                int channelNum = activeIndices[i] + 1;
                options.push_back({id++, juce::String(channelNum) + " (mono)"});
            }
        }
    } else {
        options.push_back({1, "None"});
        options.push_back({2, "(No Device Active)"});
    }

    selector->setOptions(options);
}

inline void populateAudioOutputOptions(RoutingSelector* selector, TrackId currentTrackId,
                                       juce::AudioIODevice* device,
                                       std::map<int, TrackId>& outTrackMapping) {
    if (!selector)
        return;

    std::vector<RoutingSelector::RoutingOption> options;
    options.push_back({1, "Master"});

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

    // Hardware output channels
    if (device) {
        auto activeOutputChannels = device->getActiveOutputChannels();
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

inline void populateMidiOutputOptions(RoutingSelector* selector, MidiBridge* midiBridge) {
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

    selector->setOptions(options);
}

inline void syncSelectorsFromTrack(const TrackInfo& track, RoutingSelector* audioInSelector,
                                   RoutingSelector* midiInSelector,
                                   RoutingSelector* audioOutSelector,
                                   RoutingSelector* midiOutSelector, MidiBridge* midiBridge,
                                   juce::AudioIODevice* device, TrackId currentTrackId,
                                   std::map<int, TrackId>& outputTrackMapping) {
    bool hasAudioInput = !track.audioInputDevice.isEmpty();
    bool hasMidiInput = !track.midiInputDevice.isEmpty();

    // Update Audio Input selector
    if (audioInSelector) {
        if (hasAudioInput) {
            int currentId = audioInSelector->getSelectedId();
            populateAudioInputOptions(audioInSelector, device);
            if (currentId < 10) {
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
        populateAudioOutputOptions(audioOutSelector, currentTrackId, device, outputTrackMapping);
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
