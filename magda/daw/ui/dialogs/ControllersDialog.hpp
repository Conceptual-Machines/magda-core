#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "core/controllers/Controller.hpp"
#include "core/controllers/ControllerProfile.hpp"
#include "core/controllers/ControllerRegistry.hpp"

namespace magda {

class ControllerProfilesPage;
class LuaScriptsPage;

/**
 * Two-tab dialog for managing controllers:
 *
 *   [ MIDI Profiles ] [ Lua Scripts ]
 *
 * Profiles tab: list of vendor/uploaded controller profiles bound to MIDI
 *   inputs. Click a row to toggle enabled (one enabled per port), right-
 *   click for Remove / Show profile in Finder.
 *
 * Scripts tab: list of `.lua` files in the per-user scripts folder. Click a
 *   row to make it active (one active script at a time), right-click for
 *   Reveal / Unload. Reload re-runs the active script.
 */
class ControllersDialog : public juce::Component {
  public:
    ControllersDialog();
    ~ControllersDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    static void showDialog(juce::Component* parent);

  private:
    juce::TabbedComponent tabbedComponent_{juce::TabbedButtonBar::TabsAtTop};
    std::unique_ptr<ControllerProfilesPage> profilesPage_;
    std::unique_ptr<LuaScriptsPage> scriptsPage_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControllersDialog)
};

}  // namespace magda
