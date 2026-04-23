#include "ControllersDialog.hpp"

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
    const bool active = c.enabled && connected;

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
    if (!c.enabled)
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

    // Controllers list
    listModel_.controllers = &controllers_;
    listModel_.isConnected = [this](const Controller& c) { return isControllerConnected(c); };
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

    // Section header row: label on the left, add button on the right
    auto headerRow = bounds.removeFromTop(labelH);
    addButton_.setBounds(headerRow.removeFromRight(addBtnW));
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

void ControllersDialog::onAddClicked() {
    auto profiles = ControllerProfileRegistry::getInstance().all();
    if (profiles.empty()) {
        juce::AlertWindow::showMessageBox(juce::AlertWindow::InfoIcon,
                                          tr("controllers.add_profile"),
                                          tr("controllers.no_profiles"));
        return;
    }

    juce::PopupMenu menu;
    for (size_t i = 0; i < profiles.size(); ++i) {
        const auto& p = profiles[i];
        juce::String label = p.vendor.isEmpty() ? p.name : p.vendor + "  \xc2\xb7  " + p.name;
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
    auto mat = materialiseControllerFromProfile(profile, dev.identifier, {}, dev.name);

    ControllerRegistry::getInstance().add(mat.controller);
    for (const auto& b : mat.bindings)
        BindingRegistry::getInstance().add(BindingScope::Global, b);

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
    ControllerRegistry::getInstance().setEnabled(c.id, !c.enabled);
    persist();
    rebuildList();
}

void ControllersDialog::onRowRemoveRequested(int row) {
    if (row < 0 || row >= static_cast<int>(controllers_.size()))
        return;

    const auto& c = controllers_[static_cast<size_t>(row)];

    juce::PopupMenu menu;
    menu.addItem(1, tr("controllers.remove"));

    const auto id = c.id;
    const auto name = c.name;

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(list_.get()), [this, id, name](int result) {
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

                    auto bindings = BindingRegistry::getInstance().bindings(BindingScope::Global);
                    for (const auto& b : bindings) {
                        if (b.source.controllerId == id)
                            BindingRegistry::getInstance().remove(BindingScope::Global, b.id);
                    }

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
