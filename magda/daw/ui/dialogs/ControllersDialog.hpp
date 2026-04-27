#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "core/controllers/Controller.hpp"
#include "core/controllers/ControllerProfile.hpp"
#include "core/controllers/ControllerRegistry.hpp"

namespace magda {

/**
 * Dialog that lets the user manage controller instances. Users click
 * [+ Add profile] to pick a bundled profile and assign a MIDI port;
 * click a row to toggle enabled; right-click a row to remove.
 *
 * Layout:
 *   Section header + [+ Add profile] button
 *   Controllers list (click to toggle enabled, right-click to remove)
 *   Close button
 */
class ControllersDialog : public juce::Component,
                          private ControllerRegistryListener,
                          private juce::Timer {
  public:
    ControllersDialog();
    ~ControllersDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    static void showDialog(juce::Component* parent);

    // ControllerRegistryListener
    void controllerRegistryChanged() override;

    // juce::Timer
    void timerCallback() override;

  private:
    // -------------------------------------------------------------------------
    // Inner ListBoxModel
    // -------------------------------------------------------------------------
    struct ControllerListModel : public juce::ListBoxModel {
        std::vector<Controller>* controllers = nullptr;
        // Returns true when the controller's inputPort matches a live MIDI input.
        std::function<bool(const Controller&)> isConnected;
        // Returns true when the controller has any binding registered against
        // its id — i.e. is currently active. False = user has toggled it off.
        std::function<bool(const Controller&)> isEnabled;
        std::function<void(int, const juce::MouseEvent&)> onRowClicked;

        int getNumRows() override {
            return controllers ? static_cast<int>(controllers->size()) : 0;
        }
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height,
                              bool rowIsSelected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& e) override {
            if (onRowClicked)
                onRowClicked(row, e);
        }
    };

    // -------------------------------------------------------------------------
    // Data
    // -------------------------------------------------------------------------
    std::vector<Controller> controllers_;
    juce::Array<juce::MidiDeviceInfo> liveInputs_;

    // -------------------------------------------------------------------------
    // UI components
    // -------------------------------------------------------------------------
    juce::Label sectionLabel_;
    juce::TextButton addButton_;
    juce::TextButton uploadButton_;
    ControllerListModel listModel_;
    std::unique_ptr<juce::ListBox> list_;
    std::unique_ptr<juce::FileChooser> uploadChooser_;

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------
    void refreshLiveInputs();
    void rebuildList();
    void persist();

    void onAddClicked();
    void onUploadClicked();
    void importProfileFile(const juce::File& file, const juce::String& title);
    void onProfilePicked(const ControllerProfile& profile);
    void onPortPicked(const ControllerProfile& profile, const juce::MidiDeviceInfo& dev);

    void onRowClicked(int row, const juce::MouseEvent& e);
    void onRowToggled(int row);
    void onRowRemoveRequested(int row);

    bool isControllerConnected(const Controller& c) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControllersDialog)
};

}  // namespace magda
