#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <map>

#include "../../themes/ActiveTheme.hpp"
#include "../../themes/DialogLookAndFeel.hpp"
#include "../../themes/FontManager.hpp"
#include "SettingsSections.hpp"
#include "SettingsToolbar.hpp"
#include "core/Config.hpp"
#include "core/StringTable.hpp"
#include "core/TechnicalText.hpp"
#include "core/controllers/BindingRegistry.hpp"
#include "core/controllers/Controller.hpp"
#include "core/controllers/ControllerActivation.hpp"
#include "core/controllers/ControllerProfile.hpp"
#include "core/controllers/ControllerProfileRegistry.hpp"
#include "core/controllers/ControllerRegistry.hpp"
#include "scripting_app.hpp"
#include "ui/components/common/MagdaAlertWindow.hpp"

namespace magda {

namespace {

constexpr int kPollIntervalMs = 2000;
constexpr int kProfilePortOutWidth = 170;
constexpr int kScriptPortOutWidth = 150;
constexpr int kScriptPortInWidth = 150;
constexpr int kListRowHeight = 44;
constexpr int kSwitchWidth = 34;
constexpr int kRowPad = 8;

void styleListBox(juce::ListBox& lb) {
    lb.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    lb.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    lb.setOutlineThickness(0);
    lb.setRowHeight(kListRowHeight);
}

/// The switch at a row's right edge, as SettingsLookAndFeel draws toggles.
juce::Rectangle<float> rowSwitchBounds(int width, int height) {
    return juce::Rectangle<float>(static_cast<float>(width - kRowPad - kSwitchWidth), 0.0f,
                                  static_cast<float>(kSwitchWidth), static_cast<float>(height))
        .withSizeKeepingCentre(static_cast<float>(kSwitchWidth), 18.0f);
}

void paintRowSwitch(juce::Graphics& g, juce::Rectangle<float> track, bool on) {
    g.setColour(on ? ActiveTheme::getAccentColour()
                   : ActiveTheme::getColour(ActiveTheme::SURFACE_HOVER));
    g.fillRoundedRectangle(track, track.getHeight() * 0.5f);
    const float knob = track.getHeight() - 4.0f;
    g.setColour(juce::Colours::white.withAlpha(on ? 0.95f : 0.7f));
    g.fillEllipse(on ? track.getRight() - knob - 2.0f : track.getX() + 2.0f, track.getY() + 2.0f,
                  knob, knob);
}

void paintRowBase(juce::Graphics& g, int width, int height, bool selected) {
    if (selected) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::SURFACE).withAlpha(0.6f));
        g.fillRect(0, 0, width, height);
    }
    g.setColour(ActiveTheme::getBorderColour().withMultipliedAlpha(0.6f));
    g.fillRect(0, height - 1, width, 1);
}

/// The dashed box a list shows when it has nothing in it.
void paintEmptyState(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                     const juce::String& body) {
    juce::Path outline;
    outline.addRoundedRectangle(area.toFloat().reduced(0.5f), 6.0f);
    juce::Path dashed;
    const float dashes[] = {4.0f, 4.0f};
    juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
    g.setColour(ActiveTheme::getBorderColour());
    g.fillPath(dashed);

    auto text = area.withSizeKeepingCentre(juce::jmin(420, area.getWidth() - 40), 90);
    g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
    g.setFont(FontManager::getInstance().getUIFontMedium(14.0f));
    g.drawText(title, text.removeFromTop(26), juce::Justification::centred);
    g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    g.setFont(FontManager::getInstance().getUIFont(12.5f));
    g.drawFittedText(body, text, juce::Justification::centredTop, 4);
}

juce::String displayNameForDevice(const juce::Array<juce::MidiDeviceInfo>& devices,
                                  const juce::String& id) {
    if (id.isEmpty())
        return {};
    for (const auto& dev : devices)
        if (dev.identifier == id || dev.name == id)
            return dev.name;
    return id;
}

void addMidiDeviceMenuItems(juce::PopupMenu& menu, const juce::Array<juce::MidiDeviceInfo>& devices,
                            const juce::String& currentId) {
    menu.addItem(1, tr("controllers.port.none"), true, currentId.isEmpty());
    menu.addSeparator();
    for (int i = 0; i < devices.size(); ++i) {
        const auto& dev = devices[i];
        menu.addItem(i + 2, dev.name, true, dev.identifier == currentId || dev.name == currentId);
    }
}

}  // namespace

// =============================================================================
// ControllerProfilesPage
// =============================================================================

class ControllerProfilesPage : public juce::Component,
                               private ControllerRegistryListener,
                               private juce::Timer {
  public:
    ControllerProfilesPage() {
        addButton_.setButtonText(tr("controllers.add_profile"));
        addButton_.onClick = [this]() { onAddClicked(); };
        MagdaAlertWindow::setRole(addButton_, MagdaAlertWindow::ButtonRole::Primary);
        toolbar_.addButton(addButton_);

        uploadButton_.setButtonText(trEllipsis("controllers.upload_profile"));
        uploadButton_.onClick = [this]() { onUploadClicked(); };
        toolbar_.addButton(uploadButton_);

        openFolderButton_.setButtonText(tr("controllers.open_profiles_folder"));
        openFolderButton_.onClick = [this]() { onOpenFolderClicked(); };
        toolbar_.addButton(openFolderButton_);

        listModel_.controllers = &controllers_;
        listModel_.liveInputs = &liveInputs_;
        listModel_.isConnected = [this](const Controller& c) { return isControllerConnected(c); };
        listModel_.isEnabled = [](const Controller& c) {
            return BindingRegistry::getInstance().hasAnyBindingForController(c.id);
        };
        listModel_.onRowClicked = [this](int row, const juce::MouseEvent& e) {
            onRowClicked(row, e);
        };

        list_ = std::make_unique<juce::ListBox>("profiles", &listModel_);
        styleListBox(*list_);
        addAndMakeVisible(*list_);

        refreshLiveDevices();
        if (ControllerRegistry::getInstance().rematchInputPorts(liveInputs_))
            persist();
        rebuildList();

        ControllerRegistry::getInstance().addListener(this);
        startTimer(kPollIntervalMs);
    }

    ~ControllerProfilesPage() override {
        stopTimer();
        ControllerRegistry::getInstance().removeListener(this);
        if (list_)
            list_->setModel(nullptr);
    }

    juce::Component& getToolbar() {
        return toolbar_;
    }

    void resized() override {
        list_->setBounds(getLocalBounds().reduced(0, 4));
    }

    void paint(juce::Graphics& g) override {
        if (controllers_.empty())
            paintEmptyState(g, getLocalBounds().reduced(0, 8).withHeight(240),
                            tr("controllers.no_profiles"), tr("controllers.no_profiles_hint"));
    }

  private:
    struct ControllerListModel : public juce::ListBoxModel {
        std::vector<Controller>* controllers = nullptr;
        juce::Array<juce::MidiDeviceInfo>* liveInputs = nullptr;
        std::function<bool(const Controller&)> isConnected;
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

    void controllerRegistryChanged() override {
        rebuildList();
    }

    void timerCallback() override {
        auto previous = liveInputs_;
        refreshLiveDevices();
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

    void refreshLiveDevices() {
        liveInputs_ = juce::MidiInput::getAvailableDevices();
    }

    void rebuildList() {
        controllers_ = ControllerRegistry::getInstance().all();
        if (list_)
            list_->updateContent();
        repaint();
    }

    static void persist() {
        auto& cfg = Config::getInstance();
        cfg.setControllers(ControllerRegistry::getInstance().saveToConfig());
        cfg.setGlobalBindings(BindingRegistry::getInstance().saveGlobal());
        cfg.save();
    }

    bool isControllerConnected(const Controller& c) const {
        // Shared predicate: robust identifier-or-name match, one definition
        // across the footer, this dialog, and the device indicator dots.
        return magda::controllers::isControllerConnected(c, liveInputs_);
    }

    void onAddClicked();
    void onUploadClicked();
    void onOpenFolderClicked();
    void importProfileFile(const juce::File& file, const juce::String& title);
    void onProfilePicked(const ControllerProfile& profile);
    void onPortPicked(const ControllerProfile& profile, const juce::MidiDeviceInfo& dev);

    void onRowClicked(int row, const juce::MouseEvent& e);
    void onRowToggled(int row);
    void onRowPortRequested(int row);
    void onRowRemoveRequested(int row);

    std::vector<Controller> controllers_;
    juce::Array<juce::MidiDeviceInfo> liveInputs_;

    SettingsToolbar toolbar_;
    juce::TextButton openFolderButton_;
    juce::TextButton uploadButton_;
    juce::TextButton addButton_;
    ControllerListModel listModel_;
    std::unique_ptr<juce::ListBox> list_;
    std::unique_ptr<juce::FileChooser> uploadChooser_;
};

void ControllerProfilesPage::ControllerListModel::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                                                   int width, int height,
                                                                   bool rowIsSelected) {
    if (!controllers || rowNumber < 0 || rowNumber >= static_cast<int>(controllers->size()))
        return;

    const auto& c = (*controllers)[static_cast<size_t>(rowNumber)];
    const bool connected = isConnected ? isConnected(c) : false;
    const bool enabled = isEnabled ? isEnabled(c) : true;
    const bool active = enabled && connected;
    paintRowBase(g, width, height, rowIsSelected);

    const auto track = rowSwitchBounds(width, height);
    paintRowSwitch(g, track, enabled);

    // The port, clicked to change it.
    auto portText =
        liveInputs != nullptr ? displayNameForDevice(*liveInputs, c.inputPort) : c.inputPort;
    if (portText.isEmpty())
        portText = tr("controllers.port.none");
    const auto portArea =
        juce::Rectangle<int>(static_cast<int>(track.getX()) - 12 - kProfilePortOutWidth, 0,
                             kProfilePortOutWidth, height);
    g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    g.setFont(FontManager::getInstance().getMonoFont(11.0f));
    g.drawText(portText, portArea, juce::Justification::centredRight, true);

    const float dot = 7.0f;
    g.setColour(active ? ActiveTheme::getColour(ActiveTheme::ACCENT_POSITIVE)
                       : ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
    g.fillEllipse(kRowPad + 2.0f, (static_cast<float>(height) - dot) * 0.5f, dot, dot);

    const int textX = kRowPad + 20;
    const int textW = juce::jmax(40, portArea.getX() - textX - 8);
    juce::String name = c.vendor.isEmpty() ? c.name : c.vendor + "  \xc2\xb7  " + c.name;
    g.setColour(active ? ActiveTheme::getTextColour()
                       : ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    g.setFont(FontManager::getInstance().getUIFont(13.0f));
    g.drawText(name, textX, 5, textW, 20, juce::Justification::centredLeft, true);

    const auto status = !enabled    ? tr("controllers.disabled")
                        : connected ? tr("controllers.connected")
                                    : tr("controllers.not_connected");
    g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
    g.setFont(FontManager::getInstance().getUIFont(11.0f));
    g.drawText(status, textX, 23, textW, 16, juce::Justification::centredLeft, true);
}

// -- Profiles page handlers ---------------------------------------------------

void ControllerProfilesPage::onOpenFolderClicked() {
    auto dir = ControllerProfileRegistry::userControllersDirectory();
    if (!dir.isDirectory())
        dir.createDirectory();
    dir.revealToUser();
    // Re-scan when the user comes back — they may have dropped/removed files.
    ControllerProfileRegistry::getInstance().load();
    rebuildList();
}

void ControllerProfilesPage::onUploadClicked() {
    auto title = trEllipsis("controllers.upload_profile");
    uploadChooser_ = std::make_unique<juce::FileChooser>(
        title, juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.json");
    juce::Component::SafePointer<ControllerProfilesPage> safeThis(this);
    uploadChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                    juce::FileBrowserComponent::canSelectFiles,
                                [safeThis, title](const juce::FileChooser& fc) {
                                    auto file = fc.getResult();
                                    if (file == juce::File{})
                                        return;
                                    if (safeThis == nullptr)
                                        return;
                                    safeThis->importProfileFile(file, title);
                                });
}

void ControllerProfilesPage::importProfileFile(const juce::File& file, const juce::String& title) {
    auto fail = [&](const juce::String& reason) {
        juce::AlertWindow::showMessageBox(juce::AlertWindow::WarningIcon, title, reason);
    };

    auto parsed = juce::JSON::parse(file.loadFileAsString());
    if (parsed.isVoid())
        return fail(tr("controllers.upload_invalid_json")
                        .replace("{0}", magda::technicalText(magda::TechnicalTextToken::Json)));

    std::vector<ProfileValidationIssue> issues;
    auto profileOpt = decodeControllerProfile(parsed, &issues);
    if (!profileOpt.has_value() && issues.empty())
        return fail(tr("controllers.upload_invalid_profile")
                        .replace("{0}", magda::technicalText(magda::TechnicalTextToken::Json)));

    if (profileOpt.has_value()) {
        auto crossField = validateControllerProfile(*profileOpt);
        issues.insert(issues.end(), crossField.begin(), crossField.end());
    }
    if (!issues.empty()) {
        juce::String body = tr("controllers.upload_validation_failed");
        for (const auto& issue : issues)
            body += juce::String::fromUTF8("\n  • ") + tr(issue.key).replace("{0}", issue.arg);
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

    reg.load();
    rebuildList();

    juce::AlertWindow::showMessageBox(
        juce::AlertWindow::InfoIcon, title,
        tr("controllers.upload_success").replace("{0}", profileOpt->id));
}

void ControllerProfilesPage::onAddClicked() {
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
                           auto idx = static_cast<size_t>(result - 1);
                           if (idx >= profiles.size())
                               return;
                           onProfilePicked(profiles[idx]);
                       });
}

void ControllerProfilesPage::onProfilePicked(const ControllerProfile& profile) {
    auto mat = materialiseControllerFromProfile(profile, {}, {});
    ControllerRegistry::getInstance().add(mat.controller);
    for (const auto& b : mat.bindings)
        BindingRegistry::getInstance().add(BindingScope::Global, b);

    persist();
    rebuildList();
}

void ControllerProfilesPage::onPortPicked(const ControllerProfile& profile,
                                          const juce::MidiDeviceInfo& dev) {
    auto& controllerReg = ControllerRegistry::getInstance();
    auto& bindingReg = BindingRegistry::getInstance();

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

void ControllerProfilesPage::onRowClicked(int row, const juce::MouseEvent& e) {
    if (row < 0 || row >= static_cast<int>(controllers_.size()))
        return;
    if (e.mods.isPopupMenu() || e.mods.isRightButtonDown() || e.mods.isCtrlDown()) {
        onRowRemoveRequested(row);
        return;
    }
    const int width = list_ != nullptr ? list_->getWidth() : 0;
    const auto track = rowSwitchBounds(width, kListRowHeight);
    if (e.x >= static_cast<int>(track.getX()) - 4) {
        onRowToggled(row);
        return;
    }
    if (e.x >= static_cast<int>(track.getX()) - 12 - kProfilePortOutWidth)
        onRowPortRequested(row);
}

void ControllerProfilesPage::onRowPortRequested(int row) {
    if (row < 0 || row >= static_cast<int>(controllers_.size()))
        return;
    refreshLiveDevices();
    const auto current = controllers_[static_cast<size_t>(row)];

    juce::PopupMenu menu;
    addMidiDeviceMenuItems(menu, liveInputs_, current.inputPort);
    menu.showMenuAsync(juce::PopupMenu::Options(), [this, row](int result) {
        if (result <= 0)
            return;
        auto controllers = controllers_;
        if (row < 0 || row >= static_cast<int>(controllers.size()))
            return;

        auto selected = controllers[static_cast<size_t>(row)];
        if (result == 1) {
            selected.inputPort = {};
            selected.inputPortName = {};
        } else {
            const int idx = result - 2;
            if (idx < 0 || idx >= liveInputs_.size())
                return;
            selected.inputPort = liveInputs_[idx].identifier;
            selected.inputPortName = liveInputs_[idx].name;

            for (auto& other : controllers) {
                if (other.id != selected.id && other.inputPort == selected.inputPort) {
                    other.inputPort = {};
                    other.inputPortName = {};
                    ControllerRegistry::getInstance().update(other);
                }
            }
        }

        ControllerRegistry::getInstance().update(selected);
        persist();
        rebuildList();
    });
}

void ControllerProfilesPage::onRowToggled(int row) {
    if (row < 0 || row >= static_cast<int>(controllers_.size()))
        return;
    const auto& c = controllers_[static_cast<size_t>(row)];
    auto& controllerReg = ControllerRegistry::getInstance();
    auto& bindingReg = BindingRegistry::getInstance();

    if (bindingReg.hasAnyBindingForController(c.id)) {
        bindingReg.removeAllForController(BindingScope::Global, c.id);
        bindingReg.removeAllForController(BindingScope::Project, c.id);
    } else {
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
        for (auto& b : mat.bindings) {
            b.source.controllerId = c.id;
            bindingReg.add(BindingScope::Global, b);
        }
    }
    persist();
    rebuildList();
}

void ControllerProfilesPage::onRowRemoveRequested(int row) {
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

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, id, name, profileId](int result) {
        if (result == 2) {
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

// =============================================================================
// LuaScriptsPage
// =============================================================================

class LuaScriptsPage : public juce::Component {
  public:
    LuaScriptsPage() {
        addScriptButton_.setButtonText(tr("controllers.scripts.add"));
        addScriptButton_.onClick = [this]() { onAddScriptClicked(); };
        MagdaAlertWindow::setRole(addScriptButton_, MagdaAlertWindow::ButtonRole::Primary);
        toolbar_.addButton(addScriptButton_);

        importButton_.setButtonText(trEllipsis("controllers.scripts.import"));
        importButton_.onClick = [this]() { onImportClicked(); };
        toolbar_.addButton(importButton_);

        openScriptsFolderButton_.setButtonText(tr("controllers.scripts.open_folder"));
        openScriptsFolderButton_.onClick = [this]() { onOpenScriptsFolderClicked(); };
        toolbar_.addButton(openScriptsFolderButton_);

        reloadLuaButton_.setButtonText(tr("controllers.scripts.reload"));
        reloadLuaButton_.onClick = [this]() { onReloadLuaClicked(); };
        toolbar_.addButton(reloadLuaButton_);

        listModel_.scripts = &scripts_;
        listModel_.activeScriptName = []() { return scripting_app::activeLuaScriptName(); };
        listModel_.portsForScript = [](const juce::String& name) {
            return scripting_app::luaScriptPorts(name);
        };
        listModel_.liveInputs = &liveInputs_;
        listModel_.liveOutputs = &liveOutputs_;
        listModel_.onRowClicked = [this](int row, const juce::MouseEvent& e) {
            onRowClicked(row, e);
        };

        list_ = std::make_unique<juce::ListBox>("scripts", &listModel_);
        styleListBox(*list_);
        addAndMakeVisible(*list_);

        rebuildScripts();
    }

    ~LuaScriptsPage() override {
        if (list_)
            list_->setModel(nullptr);
    }

    juce::Component& getToolbar() {
        return toolbar_;
    }

    void resized() override {
        list_->setBounds(getLocalBounds().reduced(0, 4));
    }

    void paint(juce::Graphics& g) override {
        if (scripts_.empty())
            paintEmptyState(g, getLocalBounds().reduced(0, 8).withHeight(240),
                            tr("controllers.scripts.empty_title"),
                            tr("controllers.scripts.empty_body"));
    }

  private:
    struct ScriptListModel : public juce::ListBoxModel {
        std::vector<juce::File>* scripts = nullptr;
        std::function<juce::String()> activeScriptName;
        std::function<scripting_app::LuaScriptPorts(const juce::String&)> portsForScript;
        juce::Array<juce::MidiDeviceInfo>* liveInputs = nullptr;
        juce::Array<juce::MidiDeviceInfo>* liveOutputs = nullptr;
        std::function<void(int, const juce::MouseEvent&)> onRowClicked;

        int getNumRows() override {
            return scripts ? static_cast<int>(scripts->size()) : 0;
        }
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height,
                              bool rowIsSelected) override;
        void listBoxItemClicked(int row, const juce::MouseEvent& e) override {
            if (onRowClicked)
                onRowClicked(row, e);
        }
    };

    void rebuildScripts() {
        liveInputs_ = juce::MidiInput::getAvailableDevices();
        liveOutputs_ = juce::MidiOutput::getAvailableDevices();
        scripts_ = scripting_app::enumerateLuaScripts();
        if (list_)
            list_->updateContent();
        repaint();
    }

    void onRowClicked(int row, const juce::MouseEvent& e) {
        if (row < 0 || row >= static_cast<int>(scripts_.size()))
            return;
        if (e.mods.isPopupMenu() || e.mods.isRightButtonDown() || e.mods.isCtrlDown()) {
            onRowMenu(row);
            return;
        }
        const int portInX = list_ ? list_->getWidth() - kScriptPortInWidth - 6 : 0;
        const int portOutX = portInX - kScriptPortOutWidth - 8;
        if (e.x >= portInX) {
            onRowOutputRequested(row);
            return;
        }
        if (e.x >= portOutX) {
            onRowDawInputRequested(row);
            return;
        }
        scripting_app::loadLuaScript(scripts_[static_cast<size_t>(row)]);
        rebuildScripts();
    }

    void onRowOutputRequested(int row) {
        if (row < 0 || row >= static_cast<int>(scripts_.size()))
            return;
        rebuildScripts();
        const auto scriptName = scripts_[static_cast<size_t>(row)].getFileName();
        auto ports = scripting_app::luaScriptPorts(scriptName);

        juce::PopupMenu menu;
        addMidiDeviceMenuItems(menu, liveOutputs_, ports.midiOutputPort);
        menu.showMenuAsync(juce::PopupMenu::Options(), [this, scriptName](int result) {
            if (result <= 0)
                return;
            auto ports = scripting_app::luaScriptPorts(scriptName);
            if (result == 1) {
                ports.midiOutputPort = {};
            } else {
                const int idx = result - 2;
                if (idx < 0 || idx >= liveOutputs_.size())
                    return;
                ports.midiOutputPort = liveOutputs_[idx].identifier;
            }
            scripting_app::setLuaScriptPorts(scriptName, ports);
            rebuildScripts();
        });
    }

    void onRowDawInputRequested(int row) {
        if (row < 0 || row >= static_cast<int>(scripts_.size()))
            return;
        rebuildScripts();
        const auto scriptName = scripts_[static_cast<size_t>(row)].getFileName();
        auto ports = scripting_app::luaScriptPorts(scriptName);

        juce::PopupMenu menu;
        addMidiDeviceMenuItems(menu, liveInputs_, ports.dawInputPort);
        menu.showMenuAsync(juce::PopupMenu::Options(), [this, scriptName](int result) {
            if (result <= 0)
                return;
            auto ports = scripting_app::luaScriptPorts(scriptName);
            if (result == 1) {
                ports.dawInputPort = {};
            } else {
                const int idx = result - 2;
                if (idx < 0 || idx >= liveInputs_.size())
                    return;
                ports.dawInputPort = liveInputs_[idx].identifier;
            }
            scripting_app::setLuaScriptPorts(scriptName, ports);
            rebuildScripts();
        });
    }

    void onRowMenu(int row) {
        if (row < 0 || row >= static_cast<int>(scripts_.size()))
            return;
        const auto file = scripts_[static_cast<size_t>(row)];
        const bool isActive = file.getFileName() == scripting_app::activeLuaScriptName();
        const bool isFactory = scripting_app::isFactoryLuaScript(file);

        juce::PopupMenu menu;
        menu.addItem(1, tr("controllers.scripts.reveal"));
        if (isActive)
            menu.addItem(2, tr("controllers.scripts.unload"));
        if (isFactory)
            menu.addItem(3, tr("controllers.scripts.disable"));

        juce::Component::SafePointer<LuaScriptsPage> self(this);
        menu.showMenuAsync(juce::PopupMenu::Options(), [self, file](int result) {
            if (result == 1) {
                if (file.existsAsFile())
                    file.revealToUser();
            } else if (result == 2) {
                scripting_app::unloadLuaScript();
                if (auto* page = self.getComponent())
                    page->rebuildScripts();
            } else if (result == 3) {
                auto& cfg = magda::Config::getInstance();
                auto enabled = cfg.getEnabledFactoryLuaScripts();
                const auto name = file.getFileName().toStdString();
                std::erase(enabled, name);
                cfg.setEnabledFactoryLuaScripts(std::move(enabled));
                cfg.save();
                if (file.getFileName() == scripting_app::activeLuaScriptName())
                    scripting_app::unloadLuaScript();
                if (auto* page = self.getComponent())
                    page->rebuildScripts();
            }
        });
    }

    void onAddScriptClicked() {
        auto available = scripting_app::enumerateAvailableFactoryLuaScripts();
        if (available.empty()) {
            juce::AlertWindow::showMessageBox(juce::AlertWindow::InfoIcon,
                                              tr("controllers.scripts.add"),
                                              tr("controllers.scripts.add_no_options"));
            return;
        }
        juce::PopupMenu menu;
        for (size_t i = 0; i < available.size(); ++i)
            menu.addItem(static_cast<int>(i + 1), available[i].getFileName());

        juce::Component::SafePointer<LuaScriptsPage> self(this);
        const auto& availableCopy = available;
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addScriptButton_),
                           [self, availableCopy](int result) {
                               if (result <= 0)
                                   return;
                               const auto idx = static_cast<size_t>(result - 1);
                               if (idx >= availableCopy.size())
                                   return;
                               auto& cfg = magda::Config::getInstance();
                               auto enabled = cfg.getEnabledFactoryLuaScripts();
                               const auto name = availableCopy[idx].getFileName().toStdString();
                               if (!std::ranges::contains(enabled, name)) {
                                   enabled.push_back(name);
                                   cfg.setEnabledFactoryLuaScripts(std::move(enabled));
                                   cfg.save();
                               }
                               if (auto* page = self.getComponent())
                                   page->rebuildScripts();
                           });
    }

    void onReloadLuaClicked() {
        if (!scripting_app::reloadActiveLuaScript() && scripting_app::hasAnyLuaScripts()) {
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::WarningIcon)
                    .withTitle(
                        tr("controllers.tab.scripts")
                            .replace("{0}", magda::technicalText(magda::TechnicalTextToken::Lua)))
                    .withMessage(tr("controllers.scripts.reload_failed"))
                    .withButton(tr("dialogs.ok")),
                nullptr);
        }
        rebuildScripts();
    }

    void onOpenScriptsFolderClicked() {
        scripting_app::revealLuaScriptsFolder();
        rebuildScripts();
    }

    void onImportClicked() {
        auto title = trEllipsis("controllers.scripts.import");
        importChooser_ = std::make_unique<juce::FileChooser>(
            title, juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.lua");
        juce::Component::SafePointer<LuaScriptsPage> self(this);
        importChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                        juce::FileBrowserComponent::canSelectFiles,
                                    [self, title](const juce::FileChooser& fc) {
                                        auto file = fc.getResult();
                                        if (file == juce::File{})
                                            return;
                                        if (auto* page = self.getComponent())
                                            page->importScriptFile(file, title);
                                    });
    }

    void importScriptFile(const juce::File& file, const juce::String& title) {
        auto fail = [&](const juce::String& reason) {
            juce::AlertWindow::showMessageBox(juce::AlertWindow::WarningIcon, title, reason);
        };
        if (!file.existsAsFile() || !file.hasFileExtension("lua"))
            return fail(tr("controllers.scripts.import_invalid")
                            .replace("{0}", magda::technicalText(magda::TechnicalTextToken::Lua)));

        auto scriptsDir = scripting_app::luaScriptsFolder();
        if (!scriptsDir.isDirectory())
            return fail(tr("controllers.scripts.import_copy_failed"));

        auto destFile = scriptsDir.getChildFile(file.getFileName());
        if (destFile.existsAsFile()) {
            bool ok = juce::AlertWindow::showOkCancelBox(
                juce::AlertWindow::QuestionIcon, title,
                tr("controllers.scripts.import_overwrite").replace("{0}", file.getFileName()),
                tr("dialogs.ok"), tr("dialogs.cancel"));
            if (!ok)
                return;
        }
        if (!file.copyFileTo(destFile))
            return fail(tr("controllers.scripts.import_copy_failed"));

        rebuildScripts();
        juce::AlertWindow::showMessageBox(
            juce::AlertWindow::InfoIcon, title,
            tr("controllers.scripts.import_success").replace("{0}", file.getFileName()));
    }

    std::vector<juce::File> scripts_;
    juce::Array<juce::MidiDeviceInfo> liveInputs_;
    juce::Array<juce::MidiDeviceInfo> liveOutputs_;

    SettingsToolbar toolbar_;
    juce::TextButton addScriptButton_;
    juce::TextButton openScriptsFolderButton_;
    juce::TextButton importButton_;
    juce::TextButton reloadLuaButton_;
    ScriptListModel listModel_;
    std::unique_ptr<juce::ListBox> list_;
    std::unique_ptr<juce::FileChooser> importChooser_;
};

void LuaScriptsPage::ScriptListModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width,
                                                       int height, bool rowIsSelected) {
    if (!scripts || rowNumber < 0 || rowNumber >= static_cast<int>(scripts->size()))
        return;

    const auto& file = (*scripts)[static_cast<size_t>(rowNumber)];
    const juce::String name = file.getFileName();
    const juce::String active = activeScriptName ? activeScriptName() : juce::String{};
    const bool isActive = name == active && active.isNotEmpty();

    paintRowBase(g, width, height, rowIsSelected);

    const int pad = kRowPad;
    const int dotSize = 7;
    const int dotX = pad;
    const int textX = dotX + dotSize + 8;
    const int dotY = (height - dotSize) / 2;
    const int portInX = width - kScriptPortInWidth - pad;
    const int portOutX = portInX - kScriptPortOutWidth - 8;
    const int nameW = juce::jmax(40, portOutX - textX - 8);

    if (isActive) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::ACCENT_POSITIVE));
        g.fillEllipse(static_cast<float>(dotX), static_cast<float>(dotY),
                      static_cast<float>(dotSize), static_cast<float>(dotSize));
    } else {
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        g.drawEllipse(static_cast<float>(dotX), static_cast<float>(dotY),
                      static_cast<float>(dotSize), static_cast<float>(dotSize), 1.0f);
    }

    g.setColour(isActive ? ActiveTheme::getTextColour()
                         : ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    g.setFont(FontManager::getInstance().getUIFont(13.0f));
    g.drawText(name, textX, 5, nameW, 20, juce::Justification::centredLeft, true);

    if (isActive) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        g.setFont(FontManager::getInstance().getUIFont(11.0f));
        g.drawText(tr("controllers.scripts.active"), textX, 23, nameW, 16,
                   juce::Justification::centredLeft, true);
    }

    auto ports = portsForScript ? portsForScript(name) : scripting_app::LuaScriptPorts{};
    auto portInText = liveOutputs != nullptr
                          ? displayNameForDevice(*liveOutputs, ports.midiOutputPort)
                          : ports.midiOutputPort;
    auto portOutText = liveInputs != nullptr ? displayNameForDevice(*liveInputs, ports.dawInputPort)
                                             : ports.dawInputPort;
    if (portInText.isEmpty())
        portInText = tr("controllers.port.none");
    if (portOutText.isEmpty())
        portOutText = tr("controllers.port.none");

    g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
    g.setFont(FontManager::getInstance().getUIFont(9.0f));
    g.drawText(tr("controllers.port.port_out"), portOutX, 4, kScriptPortOutWidth, 12,
               juce::Justification::centredLeft, true);
    g.drawText(tr("controllers.port.port_in"), portInX, 4, kScriptPortInWidth, 12,
               juce::Justification::centredLeft, true);

    g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    g.setFont(FontManager::getInstance().getUIFont(11.0f));
    g.drawText(portOutText, portOutX, 18, kScriptPortOutWidth, 18, juce::Justification::centredLeft,
               true);
    g.drawText(portInText, portInX, 18, kScriptPortInWidth, 18, juce::Justification::centredLeft,
               true);
}

// =============================================================================
// Controllers section (#3162)
// =============================================================================

namespace {

/** @brief MIDI profiles and Lua scripts; each edit is committed as it is made. */
class ControllersSection : public SettingsSection {
  public:
    ControllersSection() : SettingsSection(SettingsSectionId::Controllers) {}

    juce::String getTitle() const override {
        return tr("settings.section.controllers");
    }
    std::vector<Tab> getTabs() override {
        return {{tr("controllers.tab.profiles")
                     .replace("{0}", magda::technicalText(magda::TechnicalTextToken::Midi)),
                 &profilesPage_,
                 {},
                 &profilesPage_.getToolbar()},
                {tr("controllers.tab.scripts")
                     .replace("{0}", magda::technicalText(magda::TechnicalTextToken::Lua)),
                 &scriptsPage_,
                 {},
                 &scriptsPage_.getToolbar()}};
    }
    bool appliesImmediately() const override {
        return true;
    }

  private:
    ControllerProfilesPage profilesPage_;
    LuaScriptsPage scriptsPage_;
};

}  // namespace

std::unique_ptr<SettingsSection> createControllersSection() {
    return std::make_unique<ControllersSection>();
}

}  // namespace magda
