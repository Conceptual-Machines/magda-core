// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/ui/components/mixer/RoutingSyncHelper.hpp"

/** The master's output selector lists the device's open pairs and nothing else (#3424). */

using namespace magda;

namespace {

/// Four outputs open, as two stereo pairs.
class FourOutputs final : public HardwareChannels {
  public:
    bool isOpen() const override {
        return true;
    }
    Direction inputs() const override {
        return {};
    }
    Direction outputs() const override {
        Direction outputs;
        outputs.open.setRange(0, 4, true);
        return outputs;
    }
};

class MasterOutputSelectorTest final : public juce::UnitTest {
  public:
    MasterOutputSelectorTest() : juce::UnitTest("Master Output Selector Tests", "magda") {}

    void runTest() override {
        test::runWithCleanJuceState([this] { testListsDevicePairsOnly(); });
    }

  private:
    struct Shown {
        juce::String route;
        int options = 0;
    };

    static Shown shown(const HardwareChannels& hardware) {
        RoutingSelector audioOut(RoutingSelector::Type::AudioOut);
        std::map<int, TrackId> outputs;
        std::map<int, TrackId> midiOutputs;
        std::map<int, juce::String> channels;
        RoutingSyncHelper::syncSelectorsFromTrack(
            *TrackManager::getInstance().getTrack(MASTER_TRACK_ID), nullptr, nullptr, &audioOut,
            nullptr, &hardware, MASTER_TRACK_ID, outputs, midiOutputs, nullptr, nullptr, nullptr,
            &channels);
        const auto route = channels.find(audioOut.getSelectedId());
        return {route != channels.end() ? route->second : juce::String(),
                static_cast<int>(channels.size() + outputs.size())};
    }

    void testListsDevicePairsOnly() {
        beginTest("The master lists the open pairs and shows the first until one is chosen");

        auto& tracks = TrackManager::getInstance();
        tracks.createTrack("Group", TrackType::Group);
        const FourOutputs hardware;

        const auto unset = shown(hardware);
        expectEquals(unset.route, juce::String("stereo:Out 1"), "unset shows the first pair");
        expectEquals(unset.options, 2, "two pairs and no tracks");

        tracks.setTrackAudioOutput(MASTER_TRACK_ID, "stereo:Out 3");
        expectEquals(shown(hardware).route, juce::String("stereo:Out 3"), "the chosen pair");
    }
};

MasterOutputSelectorTest masterOutputSelectorTest;

}  // namespace
