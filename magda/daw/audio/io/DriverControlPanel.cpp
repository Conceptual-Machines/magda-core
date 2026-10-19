#include "DriverControlPanel.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

bool showDriverControlPanel(juce::AudioIODevice& device) {
    juce::Component blocker;
    blocker.setOpaque(true);
    blocker.addToDesktop(0);
    blocker.enterModalState();
    return device.showControlPanel();
}

}  // namespace magda
