#include "ControllersDialog.hpp"

#include <map>

#include "../themes/DarkTheme.hpp"
#include "../themes/DialogLookAndFeel.hpp"
#include "../themes/FontManager.hpp"
#include "core/Config.hpp"
#include "core/StringTable.hpp"
#include "core/controllers/BindingRegistry.hpp"
#include "core/controllers/ControllerProfileRegistry.hpp"
#include "core/controllers/ControllerRegistry.hpp"

namespace magda {

namespace {

constexpr int kPollIntervalMs = 2000;

}  // namespace

// =============================================================================
// ControllerListModel
// =============================================================================

void ControllersDialog::ControllerListModel::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                                              int width, int height,
                                                              bool rowIsSelected) {
    if (!controllers || rowNumber < 0 || rowNumber >= static_cast<int>(controllers->size()))
        return;

    const auto& c = (*controllers)[static_cast<size_t>(rowNumber)];
    const bool connected = isConnected ? isConnected(c) : false;
    const bool enabled = isEnabled ? isEnabled(c) : true;
    const bool active = enabled && connected;

    if (rowIsSelected) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.20f));
        g.fillRect(0, 0, width, height);
    }

    const int pad = 6;
    const int dotSize = 8;
    const int dotX = pad;
    const int textX = dotX + dotSize + 8;
    const int lineH = (height - 2 * pad) / 2;

    // Status dot: green when enabled + connected, dim otherwise
    const int dotY = (height - dotSize) / 2;
    g.setColour(active ? DarkTheme::getColour(DarkTheme::ACCENT_GREEN)
                       : DarkTheme::getColour(DarkTheme::TEXT_DIM));
    g.fillEllipse(static_cast<float>(dotX), static_cast<float>(dotY), static_cast<float>(dotSize),
                  static_cast<float>(dotSize));

    // Line 1: "Vendor  .  Name" — full opacity when active, dimmed otherwise
    juce::String line1 = c.vendor.isEmpty() ? c.name : c.vendor + "  \xc2\xb7  " + c.name;
    g.setColour(active ? DarkTheme::getTextColour()
                       : DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    g.setFont(FontManager::getInstance().getUIFontBold(12.0f));
    g.drawText(line1, textX, pad, width - textX - pad, lineH, juce::Justification::centredLeft,
               true);

    // Line 2: port name · status
    juce::String status;
    if (!enabled)
        status = tr("controllers.disabled");
    else if (connected)
        status = tr("controllers.connected");
    else
        status = tr("controllers.not_connected");

    juce::String portText = c.inputPortName.isNotEmpty() ? c.inputPortName : c.inputPort;
    juce::String line2 = portText + juce::String::fromUTF8("  \xc2\xb7  ") + status;

    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
    g.setFont(FontManager::getInstance().getUIFont(10.0f));
    g.drawText(line2, textX, pad + lineH, width - textX - pad, lineH,
               juce::Justification::centredLeft, true);
}

// =============================================================================
// ControllersDialog
// =============================================================================

ControllersDialog::ControllersDialog() {
    setLookAndFeel(&daw::ui::DialogLookAndFeel::getInstance());

    // Section header
    sectionLabel_.setText(tr("controllers.my_controllers"), juce::dontSendNotification);
    sectionLabel_.setColour(juce::Label::textColourId,
                            DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    sectionLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    sectionLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(sectionLabel_);

    addButton_.setButtonText(tr("controllers.add_profile"));
    addButton_.onClick = [this]() { onAddClicked(); };
    addAndMakeVisible(addButton_);

    uploadButton_.setButtonText(tr("controllers.upload_profile"));
    uploadButton_.onClick = [this]() { onUploadClicked(); };
    addAndMakeVisible(uploadButton_);

    // Controllers list
    listModel_.controllers = &controllers_;
    listModel_.isConnected = [this](const Controller& c) { return isControllerConnected(c); };
    listModel_.isEnabled = [](const Controller& c) {
        return BindingRegistry::getInstance().hasAnyBindingForController(c.id);
    };
    listModel_.onRowClicked = [this](int row, const juce::MouseEvent& e) { onRowClicked(row, e); };

    list_ = std::make_unique<juce::ListBox>("controllers", &listModel_);
    list_->setColour(juce::ListBox::backgroundColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    list_->setColour(juce::ListBox::outlineColourId, DarkTheme::getBorderColour());
    list_->setOutlineThickness(1);
    list_->setRowHeight(46);
    addAndMakeVisible(*list_);

    refreshLiveInputs();

    // Adopt the registry on first show: rematch any stale identifiers against
    // the current live input list, then pick up the data.
    if (ControllerRegistry::getInstance().rematchInputPorts(liveInputs_))
        persist();
    rebuildList();

    ControllerRegistry::getInstance().addListener(this);
    startTimer(kPollIntervalMs);

    setSize(560, 440);
}

ControllersDialog::~ControllersDialog() {
    stopTimer();
    ControllerRegistry::getInstance().removeListener(this);
    setLookAndFeel(nullptr);
    if (list_)
        list_->setModel(nullptr);
}

void ControllersDialog::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
}

void ControllersDialog::resized() {
    auto bounds = getLocalBounds().reduced(16);
    const int labelH = 24;
    const int addBtnW = 120;
    const int uploadBtnW = 140;
    const int btnGap = 6;

    // Section header row: label on the left, upload + add buttons on the right
    auto headerRow = bounds.removeFromTop(labelH);
    addButton_.setBounds(headerRow.removeFromRight(addBtnW));
    headerRow.removeFromRight(btnGap);
    uploadButton_.setBounds(headerRow.removeFromRight(uploadBtnW));
    sectionLabel_.setBounds(headerRow);
    bounds.removeFromTop(6);

    list_->setBounds(bounds);
}

// -----------------------------------------------------------------------------
// Data helpers
// -----------------------------------------------------------------------------

void ControllersDialog::refreshLiveInputs() {
    liveInputs_ = juce::MidiInput::getAvailableDevices();
}

void ControllersDialog::rebuildList() {
    controllers_ = ControllerRegistry::getInstance().all();
    if (list_)
        list_->updateContent();
    repaint();
}

void ControllersDialog::persist() {
    auto& cfg = Config::getInstance();
    cfg.setControllers(ControllerRegistry::getInstance().saveToConfig());
    cfg.setGlobalBindings(BindingRegistry::getInstance().saveGlobal());
    cfg.save();
}

bool ControllersDialog::isControllerConnected(const Controller& c) const {
    for (const auto& dev : liveInputs_)
        if (dev.identifier == c.inputPort)
            return true;
    return false;
}

// -----------------------------------------------------------------------------
// Add flow
// -----------------------------------------------------------------------------

void ControllersDialog::onUploadClicked() {
    auto title = tr("controllers.upload_profile");
    uploadChooser_ = std::make_unique<juce::FileChooser>(
        title, juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.json");
    juce::Component::SafePointer<ControllersDialog> safeThis(this);
    uploadChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                    juce::FileBrowserComponent::canSelectFiles,
                                [safeThis, title](const juce::FileChooser& fc) {
                                    auto file = fc.getResult();
                                    if (file == juce::File{})
                                        return;  // cancelled
                                    if (safeThis == nullptr)
                                        return;  // dialog closed before chooser returned
                                    safeThis->importProfileFile(file, title);
                                });
}

void ControllersDialog::importProfileFile(const juce::File& file, const juce::String& title) {
    auto fail = [&](const juce::String& reason) {
        juce::AlertWindow::showMessageBox(juce::AlertWindow::WarningIcon, title, reason);
    };

    auto parsed = juce::JSON::parse(file.loadFileAsString());
    if (parsed.isVoid())
        return fail(tr("controllers.upload_invalid_json"));

    auto profileOpt = decodeControllerProfile(parsed);
    if (!profileOpt.has_value())
        return fail(tr("controllers.upload_invalid_profile"));

    // Cross-field consistency — duplicate controlIds, defaultBindings pointing
    // at unknown controls. Validator emits localizable keys, UI formats.
    auto issues = validateControllerProfile(*profileOpt);
    if (!issues.empty()) {
        juce::String body = tr("controllers.upload_validation_failed");
        for (const auto& issue : issues)
            body += "\n  • " + tr(issue.key).replace("{0}", issue.arg);
        return fail(body);
    }

    auto& reg = ControllerProfileRegistry::getInstance();
    auto userDir = ControllerProfileRegistry::userControllersDirectory();
    if (!userDir.isDirectory()) {
        auto createResult = userDir.createDirectory();
        if (createResult.failed())
            return fail(tr("controllers.upload_create_dir_failed")
                            .replace("{0}", createResult.getErrorMessage()));
    }

    // Copy with a name derived from the profile id so the file lives next to
    // its siblings; if a profile with the same id exists, ask before clobbering.
    auto destFile =
        userDir.getChildFile(ControllerProfileRegistry::filenameForProfileId(profileOpt->id));

    if (destFile.existsAsFile()) {
        bool ok = juce::AlertWindow::showOkCancelBox(
            juce::AlertWindow::QuestionIcon, title,
            tr("controllers.upload_overwrite").replace("{0}", profileOpt->id), tr("dialogs.ok"),
            tr("dialogs.cancel"));
        if (!ok)
            return;
    }

    if (!file.copyFileTo(destFile))
        return fail(tr("controllers.upload_copy_failed"));

    reg.load();  // pick up the new file
    rebuildList();

    juce::AlertWindow::showMessageBox(
        juce::AlertWindow::InfoIcon, title,
        tr("controllers.upload_success").replace("{0}", profileOpt->id));
}

void ControllersDialog::onAddClicked() {
    // Re-scan the profiles directory so files added or removed on disk since
    // app launch are reflected in the menu — without this, deleting a JSON in
    // Finder still leaves the entry in +Add.
    auto& profileReg = ControllerProfileRegistry::getInstance();
    profileReg.load();
    auto profiles = profileReg.all();
    if (profiles.empty()) {
        juce::AlertWindow::showMessageBox(juce::AlertWindow::InfoIcon,
                                          tr("controllers.add_profile"),
                                          tr("controllers.no_profiles"));
        return;
    }

    juce::PopupMenu menu;
    // Disambiguate entries sharing the same vendor·name by appending the id.
    std::map<juce::String, int> nameCounts;
    for (const auto& p : profiles) {
        juce::String key = p.vendor + "\x1f" + p.name;
        nameCounts[key]++;
    }
    for (size_t i = 0; i < profiles.size(); ++i) {
        const auto& p = profiles[i];
        juce::String label = p.vendor.isEmpty() ? p.name : p.vendor + "  \xc2\xb7  " + p.name;
        juce::String key = p.vendor + "\x1f" + p.name;
        if (nameCounts[key] > 1)
            label += "  (" + p.id + ")";
        menu.addItem(static_cast<int>(i) + 1, label);
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addButton_),
                       [this, profiles](int result) {
                           if (result <= 0)
                               return;
                           size_t idx = static_cast<size_t>(result - 1);
                           if (idx >= profiles.size())
                               return;
                           onProfilePicked(profiles[idx]);
                       });
}

void ControllersDialog::onProfilePicked(const ControllerProfile& profile) {
    refreshLiveInputs();

    if (liveInputs_.isEmpty()) {
        juce::AlertWindow::showMessageBox(juce::AlertWindow::WarningIcon,
                                          tr("controllers.add_profile"),
                                          tr("controllers.no_midi_inputs"));
        return;
    }

    juce::PopupMenu menu;
    for (int i = 0; i < liveInputs_.size(); ++i)
        menu.addItem(i + 1, liveInputs_[i].name);

    auto devicesCopy = liveInputs_;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addButton_),
                       [this, profile, devicesCopy](int result) {
                           if (result <= 0)
                               return;
                           int idx = result - 1;
                           if (idx < 0 || idx >= devicesCopy.size())
                               return;
                           onPortPicked(profile, devicesCopy[idx]);
                       });
}

void ControllersDialog::onPortPicked(const ControllerProfile& profile,
                                     const juce::MidiDeviceInfo& dev) {
    auto& controllerReg = ControllerRegistry::getInstance();
    auto& bindingReg = BindingRegistry::getInstance();

    // One enabled controller per port: any existing row on this port stays
    // registered, but its bindings are dropped so it goes inactive. The new
    // controller becomes the active one.
    for (const auto& existing : controllerReg.all()) {
        if (existing.inputPort == dev.identifier) {
            bindingReg.removeAllForController(BindingScope::Global, existing.id);
            bindingReg.removeAllForController(BindingScope::Project, existing.id);
        }
    }

    auto mat = materialiseControllerFromProfile(profile, dev.identifier, {}, dev.name);

    controllerReg.add(mat.controller);
    for (const auto& b : mat.bindings)
        bindingReg.add(BindingScope::Global, b);

    persist();
    rebuildList();
}

// -----------------------------------------------------------------------------
// Row interaction
// -----------------------------------------------------------------------------

void ControllersDialog::onRowClicked(int row, const juce::MouseEvent& e) {
    if (row < 0 || row >= static_cast<int>(controllers_.size()))
        return;

    if (e.mods.isPopupMenu() || e.mods.isRightButtonDown() || e.mods.isCtrlDown()) {
        onRowRemoveRequested(row);
        return;
    }

    onRowToggled(row);
}

void ControllersDialog::onRowToggled(int row) {
    if (row < 0 || row >= static_cast<int>(controllers_.size()))
        return;

    const auto& c = controllers_[static_cast<size_t>(row)];
    auto& controllerReg = ControllerRegistry::getInstance();
    auto& bindingReg = BindingRegistry::getInstance();

    if (bindingReg.hasAnyBindingForController(c.id)) {
        // Currently enabled — silence by removing all bindings keyed to this id.
        bindingReg.removeAllForController(BindingScope::Global, c.id);
        bindingReg.removeAllForController(BindingScope::Project, c.id);
    } else {
        // Currently disabled — first silence any other controller on the same
        // port (one enabled per port), then re-materialise this profile's
        // bindings and add them back.
        for (const auto& other : controllerReg.all()) {
            if (other.id == c.id || other.inputPort != c.inputPort)
                continue;
            bindingReg.removeAllForController(BindingScope::Global, other.id);
            bindingReg.removeAllForController(BindingScope::Project, other.id);
        }
        auto profileOpt = ControllerProfileRegistry::getInstance().findById(c.profileId);
        if (!profileOpt.has_value())
            return;
        auto mat = materialiseControllerFromProfile(*profileOpt, c.inputPort, c.outputPort,
                                                    c.inputPortName);
        // Reuse the existing controller's id so bindings stay tied to the same
        // registry row across enable/disable cycles.
        for (auto& b : mat.bindings) {
            b.source.controllerId = c.id;
            bindingReg.add(BindingScope::Global, b);
        }
    }

    persist();
    rebuildList();
}

void ControllersDialog::onRowRemoveRequested(int row) {
    if (row < 0 || row >= static_cast<int>(controllers_.size()))
        return;

    const auto& c = controllers_[static_cast<size_t>(row)];

    juce::PopupMenu menu;
    const bool haveProfile = c.profileId.isNotEmpty();
    if (haveProfile)
        menu.addItem(2, tr("controllers.show_in_finder"));
    menu.addItem(1, tr("controllers.remove"));

    const auto id = c.id;
    const auto name = c.name;
    const auto profileId = c.profileId;

    // No target component → JUCE pops the menu at the cursor (where the user
    // right-clicked), instead of pinning it to the list's edge.
    menu.showMenuAsync(juce::PopupMenu::Options(), [this, id, name, profileId](int result) {
        if (result == 2) {
            // Bundled profile filenames don't always match their id
            // (e.g. id "novation.launchkey_mini_mk4" lives in
            // novation_launchkey_mini_mk4.json), so look the file up by
            // parsed id rather than synthesising a path.
            auto file =
                ControllerProfileRegistry::getInstance().findSourceFileForProfileId(profileId);
            if (file.existsAsFile())
                file.revealToUser();
            return;
        }

        if (result != 1)
            return;

        juce::String title = tr("controllers.remove_confirm_title");
        juce::String msg = tr("controllers.remove_confirm_message");
        msg = msg.replace("{0}", name);

        juce::AlertWindow::showOkCancelBox(
            juce::AlertWindow::QuestionIcon, title, msg, tr("dialogs.ok"), tr("dialogs.cancel"),
            nullptr, juce::ModalCallbackFunction::create([this, id](int result2) {
                if (result2 != 1)
                    return;

                BindingRegistry::getInstance().removeAllForController(BindingScope::Global, id);
                ControllerRegistry::getInstance().remove(id);
                persist();
                rebuildList();
            }));
    });
}

// -----------------------------------------------------------------------------
// Listeners
// -----------------------------------------------------------------------------

void ControllersDialog::controllerRegistryChanged() {
    rebuildList();
}

void ControllersDialog::timerCallback() {
    auto previous = liveInputs_;
    refreshLiveInputs();

    // Has the device set changed?
    bool changed = previous.size() != liveInputs_.size();
    if (!changed) {
        for (int i = 0; i < liveInputs_.size(); ++i) {
            if (previous[i].identifier != liveInputs_[i].identifier ||
                previous[i].name != liveInputs_[i].name) {
                changed = true;
                break;
            }
        }
    }

    if (!changed)
        return;

    if (ControllerRegistry::getInstance().rematchInputPorts(liveInputs_))
        persist();
    rebuildList();
}

// =============================================================================
// showDialog
// =============================================================================

namespace {
class SelfClosingDialogWindow : public juce::DialogWindow {
  public:
    SelfClosingDialogWindow(const juce::String& title, juce::Colour bg)
        : juce::DialogWindow(title, bg, false, true) {}

    void closeButtonPressed() override {
        juce::MessageManager::callAsync([self = this]() { delete self; });
    }
};
}  // namespace

void ControllersDialog::showDialog(juce::Component* /*parent*/) {
    auto* dialog = new ControllersDialog();
    auto bg = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);

    auto* window = new SelfClosingDialogWindow(tr("controllers.title"), bg);
    window->setContentOwned(dialog, true);
    window->setUsingNativeTitleBar(true);
    window->setResizable(true, false);
    window->setAlwaysOnTop(true);
    window->centreWithSize(dialog->getWidth(), dialog->getHeight());
    window->setVisible(true);
}

}  // namespace magda
