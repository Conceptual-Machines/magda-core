#include "PluginSettingsDialog.hpp"

#include <algorithm>

#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "core/Config.hpp"
#include "engine/PluginScanCoordinator.hpp"
#include "engine/TracktionEngineWrapper.hpp"

namespace magda {

// =============================================================================
// DirectoryListModel
// =============================================================================

int PluginSettingsDialog::DirectoryListModel::getNumRows() {
    return paths ? static_cast<int>(paths->size()) : 0;
}

void PluginSettingsDialog::DirectoryListModel::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                                                int width, int height,
                                                                bool rowIsSelected) {
    if (!paths || rowNumber < 0 || rowNumber >= static_cast<int>(paths->size()))
        return;

    if (rowIsSelected) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.3f));
        g.fillRect(0, 0, width, height);
    }

    g.setColour(DarkTheme::getTextColour());
    g.setFont(FontManager::getInstance().getUIFont(12.0f));
    g.drawText(juce::String((*paths)[static_cast<size_t>(rowNumber)]), 4, 0, width - 8, height,
               juce::Justification::centredLeft);
}

// =============================================================================
// ExcludedTableModel
// =============================================================================

int PluginSettingsDialog::ExcludedTableModel::getNumRows() {
    return entries ? static_cast<int>(entries->size()) : 0;
}

void PluginSettingsDialog::ExcludedTableModel::paintRowBackground(juce::Graphics& g,
                                                                  int /*rowNumber*/, int width,
                                                                  int height, bool rowIsSelected) {
    if (rowIsSelected) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.3f));
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    }
    g.fillRect(0, 0, width, height);
}

void PluginSettingsDialog::ExcludedTableModel::paintCell(juce::Graphics& g, int rowNumber,
                                                         int columnId, int width, int height,
                                                         bool /*rowIsSelected*/) {
    if (!entries || rowNumber < 0 || rowNumber >= static_cast<int>(entries->size()))
        return;

    const auto& entry = (*entries)[static_cast<size_t>(rowNumber)];

    g.setColour(DarkTheme::getTextColour());
    g.setFont(FontManager::getInstance().getUIFont(11.0f));

    juce::String text;
    switch (columnId) {
        case 1: {
            juce::File f(entry.path);
            text = f.getFileName();
            if (text.isEmpty())
                text = entry.path;
            break;
        }
        case 2:
            text = entry.reason;
            break;
        case 3:
            text = entry.timestamp;
            break;
    }

    g.drawText(text, 4, 0, width - 8, height, juce::Justification::centredLeft);
}

juce::Component* PluginSettingsDialog::ExcludedTableModel::refreshComponentForCell(
    int, int, bool, juce::Component*) {
    return nullptr;
}

// =============================================================================
// PluginSettingsDialog
// =============================================================================

PluginSettingsDialog::PluginSettingsDialog(TracktionEngineWrapper* engine)
    : scanProgressBar_(scanProgress_), engine_(engine) {
    // Load current data
    customPaths_ = Config::getInstance().getCustomPluginPaths();

    if (engine_) {
        auto* coordinator = engine_->getPluginScanCoordinator();
        if (coordinator)
            excludedPlugins_ = coordinator->getExcludedPlugins();
    }

    // Populate system plugin directories from format manager
    if (engine_ && engine_->getEngine()) {
        auto& formatManager = engine_->getEngine()->getPluginManager().pluginFormatManager;
        for (int i = 0; i < formatManager.getNumFormats(); ++i) {
            auto* format = formatManager.getFormat(i);
            if (!format)
                continue;
            juce::String formatName = format->getName();
            if (!formatName.containsIgnoreCase("VST3") &&
                !formatName.containsIgnoreCase("AudioUnit"))
                continue;
            auto searchPaths = format->getDefaultLocationsToSearch();
            for (int j = 0; j < searchPaths.getNumPaths(); ++j) {
                auto path = searchPaths[j].getFullPathName().toStdString();
                if (std::find(systemPaths_.begin(), systemPaths_.end(), path) == systemPaths_.end())
                    systemPaths_.push_back(path);
            }
        }
    }

    // Wire up models
    systemDirListModel_.paths = &systemPaths_;
    dirListModel_.paths = &customPaths_;
    excludedTableModel_.entries = &excludedPlugins_;

    // System directories section (read-only)
    setupSectionHeader(systemDirsHeader_, "System Plugin Directories");

    systemDirsList_.setModel(&systemDirListModel_);
    systemDirsList_.setColour(juce::ListBox::backgroundColourId,
                              DarkTheme::getColour(DarkTheme::SURFACE));
    systemDirsList_.setColour(juce::ListBox::outlineColourId, DarkTheme::getBorderColour());
    systemDirsList_.setOutlineThickness(1);
    systemDirsList_.setRowHeight(22);
    addAndMakeVisible(systemDirsList_);

    // Custom directories section
    setupSectionHeader(directoriesHeader_, "Custom Plugin Directories");

    directoriesList_.setModel(&dirListModel_);
    directoriesList_.setColour(juce::ListBox::backgroundColourId,
                               DarkTheme::getColour(DarkTheme::SURFACE));
    directoriesList_.setColour(juce::ListBox::outlineColourId, DarkTheme::getBorderColour());
    directoriesList_.setOutlineThickness(1);
    directoriesList_.setRowHeight(22);
    addAndMakeVisible(directoriesList_);

    addDirButton_.setButtonText("Add...");
    addDirButton_.onClick = [this]() {
        fileChooser_ = std::make_unique<juce::FileChooser>("Select Plugin Directory");
        fileChooser_->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this](const juce::FileChooser& fc) {
                auto result = fc.getResult();
                if (result.exists()) {
                    customPaths_.push_back(result.getFullPathName().toStdString());
                    directoriesList_.updateContent();
                    directoriesList_.repaint();
                }
            });
    };
    addAndMakeVisible(addDirButton_);

    removeDirButton_.setButtonText("Remove");
    removeDirButton_.onClick = [this]() {
        int selected = directoriesList_.getSelectedRow();
        if (selected >= 0 && selected < static_cast<int>(customPaths_.size())) {
            customPaths_.erase(customPaths_.begin() + selected);
            directoriesList_.updateContent();
            directoriesList_.repaint();
        }
    };
    addAndMakeVisible(removeDirButton_);

    // Scan section
    scanButton_.setButtonText("Scan for Plugins");
    scanButton_.onClick = [this]() {
        if (!engine_)
            return;
        // Apply settings first so custom paths are used during scan
        applySettings();

        scanButton_.setEnabled(false);
        scanProgress_ = 0.0;
        scanStatusLabel_.setText("Starting scan...", juce::dontSendNotification);
        scanProgressBar_.setVisible(true);
        scanStatusLabel_.setVisible(true);

        auto safeThis = juce::Component::SafePointer<PluginSettingsDialog>(this);

        engine_->startPluginScan([safeThis](float progress, const juce::String& pluginName) {
            juce::MessageManager::callAsync([safeThis, progress, pluginName]() {
                if (safeThis == nullptr)
                    return;
                safeThis->scanProgress_ = static_cast<double>(progress);
                juce::File f(pluginName);
                safeThis->scanStatusLabel_.setText("Scanning: " + f.getFileName(),
                                                   juce::dontSendNotification);
            });
        });

        engine_->onPluginScanComplete = [safeThis](bool /*success*/, int numPlugins,
                                                   const juce::StringArray& failedPlugins) {
            juce::MessageManager::callAsync([safeThis, numPlugins, failedPlugins]() {
                if (safeThis == nullptr)
                    return;
                safeThis->scanButton_.setEnabled(true);
                safeThis->scanProgress_ = -1.0;
                safeThis->scanProgressBar_.setVisible(false);
                safeThis->scanStatusLabel_.setText(
                    "Found " + juce::String(numPlugins) + " plugins" +
                        (failedPlugins.size() > 0
                             ? ", " + juce::String(failedPlugins.size()) + " failed"
                             : ""),
                    juce::dontSendNotification);

                // Refresh excluded plugins list
                if (safeThis->engine_) {
                    auto* coordinator = safeThis->engine_->getPluginScanCoordinator();
                    if (coordinator)
                        safeThis->excludedPlugins_ = coordinator->getExcludedPlugins();
                    safeThis->excludedTable_.updateContent();
                    safeThis->excludedTable_.repaint();
                }
            });
        };
    };
    addAndMakeVisible(scanButton_);

    viewReportButton_.setButtonText("View Scan Report");
    viewReportButton_.onClick = [this]() {
        if (engine_) {
            auto* coordinator = engine_->getPluginScanCoordinator();
            if (coordinator) {
                auto reportFile = coordinator->getScanReportFile();
                if (reportFile.existsAsFile())
                    reportFile.startAsProcess();
            }
        }
    };
    addAndMakeVisible(viewReportButton_);

    scanProgressBar_.setPercentageDisplay(true);
    scanProgressBar_.setVisible(false);
    addAndMakeVisible(scanProgressBar_);

    scanStatusLabel_.setColour(juce::Label::textColourId,
                               DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    scanStatusLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    scanStatusLabel_.setVisible(false);
    addAndMakeVisible(scanStatusLabel_);

    // Excluded plugins section
    setupSectionHeader(excludedHeader_, "Excluded Plugins");

    excludedTable_.setModel(&excludedTableModel_);
    excludedTable_.setColour(juce::ListBox::backgroundColourId,
                             DarkTheme::getColour(DarkTheme::SURFACE));
    excludedTable_.setColour(juce::ListBox::outlineColourId, DarkTheme::getBorderColour());
    excludedTable_.setOutlineThickness(1);
    excludedTable_.getHeader().addColumn("Plugin", 1, 250, 100, 400);
    excludedTable_.getHeader().addColumn("Reason", 2, 80, 60, 150);
    excludedTable_.getHeader().addColumn("Date", 3, 150, 80, 250);
    excludedTable_.getHeader().setColour(juce::TableHeaderComponent::backgroundColourId,
                                         DarkTheme::getColour(DarkTheme::SURFACE));
    excludedTable_.getHeader().setColour(juce::TableHeaderComponent::textColourId,
                                         DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    excludedTable_.setMultipleSelectionEnabled(true);
    addAndMakeVisible(excludedTable_);

    removeSelectedButton_.setButtonText("Remove Selected");
    removeSelectedButton_.onClick = [this]() {
        auto selectedRows = excludedTable_.getSelectedRows();
        std::vector<int> indices;
        for (int i = 0; i < selectedRows.size(); ++i) {
            int idx = selectedRows[i];
            if (idx >= 0 && idx < static_cast<int>(excludedPlugins_.size()))
                indices.push_back(idx);
        }
        std::sort(indices.rbegin(), indices.rend());
        for (int idx : indices) {
            excludedPlugins_.erase(excludedPlugins_.begin() + idx);
        }
        excludedTable_.updateContent();
        excludedTable_.repaint();
    };
    addAndMakeVisible(removeSelectedButton_);

    resetAllButton_.setButtonText("Reset All");
    resetAllButton_.onClick = [this]() {
        excludedPlugins_.clear();
        excludedTable_.updateContent();
        excludedTable_.repaint();
    };
    addAndMakeVisible(resetAllButton_);

    // OK / Cancel
    okButton_.setButtonText("OK");
    okButton_.onClick = [this]() {
        applySettings();
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(1);
    };
    addAndMakeVisible(okButton_);

    cancelButton_.setButtonText("Cancel");
    cancelButton_.onClick = [this]() {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };
    addAndMakeVisible(cancelButton_);

    setSize(550, 650);
}

PluginSettingsDialog::~PluginSettingsDialog() {
    systemDirsList_.setModel(nullptr);
    directoriesList_.setModel(nullptr);
    excludedTable_.setModel(nullptr);
}

void PluginSettingsDialog::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
}

void PluginSettingsDialog::resized() {
    auto bounds = getLocalBounds().reduced(16);
    const int headerHeight = 24;
    const int buttonHeight = 28;
    const int buttonWidth = 90;
    const int spacing = 8;

    // System directories section
    systemDirsHeader_.setBounds(bounds.removeFromTop(headerHeight));
    bounds.removeFromTop(4);

    int systemDirsHeight = std::max(44, static_cast<int>(systemPaths_.size()) * 22 + 2);
    systemDirsList_.setBounds(bounds.removeFromTop(systemDirsHeight));

    bounds.removeFromTop(spacing);

    // Custom directories section
    directoriesHeader_.setBounds(bounds.removeFromTop(headerHeight));
    bounds.removeFromTop(4);

    auto dirArea = bounds.removeFromTop(88);
    auto dirButtons = dirArea.removeFromRight(buttonWidth + 4);
    directoriesList_.setBounds(dirArea);
    addDirButton_.setBounds(dirButtons.removeFromTop(buttonHeight));
    dirButtons.removeFromTop(4);
    removeDirButton_.setBounds(dirButtons.removeFromTop(buttonHeight));

    bounds.removeFromTop(spacing * 2);

    // Scan section
    auto scanRow = bounds.removeFromTop(buttonHeight);
    scanButton_.setBounds(scanRow.removeFromLeft(140));
    scanRow.removeFromLeft(spacing);
    viewReportButton_.setBounds(scanRow.removeFromLeft(130));
    scanRow.removeFromLeft(spacing);
    scanProgressBar_.setBounds(scanRow);

    bounds.removeFromTop(2);
    scanStatusLabel_.setBounds(bounds.removeFromTop(18));

    bounds.removeFromTop(spacing);

    // Excluded plugins section
    excludedHeader_.setBounds(bounds.removeFromTop(headerHeight));
    bounds.removeFromTop(4);

    // Reserve space for bottom buttons
    auto bottomArea = bounds.removeFromBottom(buttonHeight);
    bounds.removeFromBottom(spacing);

    // Excluded buttons row
    auto excludedButtonRow = bounds.removeFromBottom(buttonHeight);
    bounds.removeFromBottom(4);

    // Excluded table takes remaining space
    excludedTable_.setBounds(bounds);

    // Excluded buttons - right aligned
    {
        auto btnArea = excludedButtonRow;
        resetAllButton_.setBounds(btnArea.removeFromRight(buttonWidth));
        btnArea.removeFromRight(4);
        removeSelectedButton_.setBounds(btnArea.removeFromRight(120));
    }

    // Bottom OK/Cancel buttons
    {
        auto btnArea = bottomArea;
        okButton_.setBounds(btnArea.removeFromRight(buttonWidth));
        btnArea.removeFromRight(4);
        cancelButton_.setBounds(btnArea.removeFromRight(buttonWidth));
    }
}

void PluginSettingsDialog::applySettings() {
    Config::getInstance().setCustomPluginPaths(customPaths_);

    if (engine_) {
        auto* coordinator = engine_->getPluginScanCoordinator();
        if (coordinator) {
            coordinator->clearExclusions();
            for (const auto& entry : excludedPlugins_)
                coordinator->excludePlugin(entry.path, entry.reason);
        }
    }
}

void PluginSettingsDialog::showDialog(TracktionEngineWrapper* engine, juce::Component* /*parent*/) {
    auto* dialog = new PluginSettingsDialog(engine);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Plugin Settings";
    options.dialogBackgroundColour = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
    options.content.setOwned(dialog);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    options.launchAsync();
}

void PluginSettingsDialog::setupSectionHeader(juce::Label& header, const juce::String& text) {
    header.setText(text, juce::dontSendNotification);
    header.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    header.setFont(juce::Font(14.0f, juce::Font::bold));
    header.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(header);
}

}  // namespace magda
