#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "core/controllers/Controller.hpp"
#include "core/controllers/ControllerProfile.hpp"
#include "core/controllers/ControllerRegistry.hpp"

namespace magda {

/**
 * Dialog that lets the user enable bundled/user controller profiles and
 * manage currently-active controllers.
 *
 * Layout:
 *   Top list   - available profiles
 *   ComboBox   - MIDI input port
 *   Enable btn - materialise profile into ControllerRegistry + BindingRegistry
 *   Bottom list - enabled controllers with per-row Remove button
 *   Close btn
 */
class ControllersDialog : public juce::Component, private ControllerRegistryListener {
  public:
    ControllersDialog();
    ~ControllersDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    static void showDialog(juce::Component* parent);

    // ControllerRegistryListener
    void controllerRegistryChanged() override;

  private:
    // -------------------------------------------------------------------------
    // Inner ListBoxModel for available profiles
    // -------------------------------------------------------------------------
    struct ProfileListModel : public juce::ListBoxModel {
        std::vector<ControllerProfile>* profiles = nullptr;
        // Predicate: returns true if the profile has a matching live MIDI input.
        // For profiles with an empty portMatchPattern, should return true (generic).
        std::function<bool(const ControllerProfile&)> isConnected;
        std::function<void(int)> onRowSelected;

        int getNumRows() override {
            return profiles ? static_cast<int>(profiles->size()) : 0;
        }
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height,
                              bool rowIsSelected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent&) override {
            if (onRowSelected)
                onRowSelected(row);
        }
    };

    // -------------------------------------------------------------------------
    // Inner ListBoxModel for enabled controllers
    // -------------------------------------------------------------------------
    struct ControllerListModel : public juce::ListBoxModel {
        std::vector<Controller>* controllers = nullptr;
        std::function<void(int)> onRemoveClicked;

        int getNumRows() override {
            return controllers ? static_cast<int>(controllers->size()) : 0;
        }
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height,
                              bool rowIsSelected) override;
        juce::Component* refreshComponentForRow(int rowNumber, bool isRowSelected,
                                                juce::Component* existingComponent) override;
    };

    // -------------------------------------------------------------------------
    // Data
    // -------------------------------------------------------------------------
    std::vector<ControllerProfile> availableProfiles_;
    std::vector<Controller> enabledControllers_;

    // Stable MIDI device list: identifier -> display name
    juce::Array<juce::MidiDeviceInfo> liveInputs_;

    int selectedProfileIndex_ = -1;

    // -------------------------------------------------------------------------
    // UI components
    // -------------------------------------------------------------------------
    juce::Label availableLabel_;
    ProfileListModel profileListModel_;
    std::unique_ptr<juce::ListBox> profileList_;

    juce::Label portLabel_;
    juce::ComboBox portComboBox_;
    juce::TextButton enableButton_;

    juce::Label enabledLabel_;
    ControllerListModel controllerListModel_;
    std::unique_ptr<juce::ListBox> controllerList_;

    juce::TextButton closeButton_;

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------
    void rebuildProfileList();
    void rebuildControllerList();
    void refreshPortComboBox();
    void onEnableClicked();
    void onRemoveClicked(int rowIndex);

    /** Returns the MIDI device identifier for the selected ComboBox item, or empty. */
    juce::String selectedPortIdentifier() const;

    /** Returns display name for a port identifier, or the identifier itself if not found. */
    juce::String portDisplayName(const juce::String& identifier) const;

    void setupSectionLabel(juce::Label& label, const juce::String& text);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControllersDialog)
};

}  // namespace magda
