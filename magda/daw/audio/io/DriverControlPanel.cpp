#include "DriverControlPanel.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

bool showDriverControlPanel(juce::AudioIODevice& device) {
#if JUCE_WINDOWS
    // Only ASIO has a panel. Elsewhere the blocker has nothing to block, and under Xvfb its
    // window is an X error (BadAtom) that ends the process.
    juce::Component blocker;
    blocker.setOpaque(true);
    blocker.addToDesktop(0);
    blocker.enterModalState();
#endif
    return device.showControlPanel();
}

}  // namespace magda
