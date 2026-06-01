#include "ParameterConfigDialog.hpp"

#include <juce_audio_processors/juce_audio_processors.h>

#include "../themes/DarkTheme.hpp"
#include "../themes/DialogLookAndFeel.hpp"
#include "../themes/FontManager.hpp"
#include "audio/plugins/InternalPluginRegistry.hpp"
#include "audio/plugins/compiled/CompiledPluginRegistry.hpp"
#include "core/AppPaths.hpp"
#include "core/Config.hpp"
#include "core/TrackManager.hpp"
#include "engine/TracktionEngineWrapper.hpp"

namespace magda::daw::ui {

// Static cache of scanned plugin parameters (persists across dialog instances)
struct CachedPluginParams {
    std::vector<MockParameterInfo> parameters;
    std::vector<magda::ParameterScanInput> scanInputs;
};
static std::map<juce::String, CachedPluginParams> parameterCache_;

static bool applyConfigToMatchingDevice(const juce::String& uniqueId, magda::DeviceInfo& device) {
    const auto deviceConfigId = device.uniqueId.isNotEmpty() ? device.uniqueId : device.pluginId;
    if (deviceConfigId != uniqueId)
        return false;

    return ParameterConfigDialog::applyConfigToDevice(uniqueId, device);
}

static bool refreshElementParameterConfig(const juce::String& uniqueId,
                                          std::vector<magda::ChainElement>& elements) {
    bool changed = false;
    for (auto& element : elements) {
        if (magda::isDevice(element)) {
            changed = applyConfigToMatchingDevice(uniqueId, magda::getDevice(element)) || changed;
        } else if (magda::isRack(element)) {
            for (auto& chain : magda::getRack(element).chains)
                changed = refreshElementParameterConfig(uniqueId, chain.elements) || changed;
        }
    }
    return changed;
}

static bool refreshFlatParameterConfig(const juce::String& uniqueId,
                                       std::vector<magda::PostFxChainElement>& elements) {
    bool changed = false;
    for (auto& element : elements)
        changed = applyConfigToMatchingDevice(uniqueId, element.device) || changed;
    return changed;
}

static void refreshLiveDevicesForParameterConfig(const juce::String& uniqueId) {
    if (uniqueId.isEmpty())
        return;

    auto& tm = magda::TrackManager::getInstance();
    std::vector<magda::TrackId> trackIds;
    trackIds.reserve(tm.getTracks().size() + 1);
    trackIds.push_back(magda::MASTER_TRACK_ID);
    for (const auto& track : tm.getTracks())
        trackIds.push_back(track.id);

    for (auto trackId : trackIds) {
        auto* track = tm.getTrack(trackId);
        if (track == nullptr)
            continue;

        bool changed = refreshElementParameterConfig(uniqueId, track->chain.fxChainElements);
        changed = refreshFlatParameterConfig(uniqueId, track->chain.postFxChainElements) || changed;
        changed =
            refreshFlatParameterConfig(uniqueId, track->chain.mixerAnalysisElements) || changed;
        if (changed)
            tm.notifyTrackDevicesChanged(trackId);
    }
}

static juce::String scaleToXmlString(magda::ParameterScale scale) {
    switch (scale) {
        case magda::ParameterScale::Linear:
            return "linear";
        case magda::ParameterScale::Logarithmic:
            return "logarithmic";
        case magda::ParameterScale::Exponential:
            return "exponential";
        case magda::ParameterScale::Discrete:
            return "discrete";
        case magda::ParameterScale::Boolean:
            return "boolean";
        case magda::ParameterScale::FaderDB:
            return "fader_db";
    }
    return "linear";
}

static magda::ParameterScale xmlStringToScale(const juce::String& str) {
    if (str == "logarithmic")
        return magda::ParameterScale::Logarithmic;
    if (str == "exponential")
        return magda::ParameterScale::Exponential;
    if (str == "discrete")
        return magda::ParameterScale::Discrete;
    if (str == "boolean")
        return magda::ParameterScale::Boolean;
    if (str == "fader_db")
        return magda::ParameterScale::FaderDB;
    return magda::ParameterScale::Linear;
}

//==============================================================================
// ToggleCell - Checkbox cell for visible/use as gain columns
//==============================================================================
class ParameterConfigDialog::ToggleCell : public juce::Component {
  public:
    ToggleCell(ParameterConfigDialog& owner, int row, int column)
        : owner_(owner), row_(row), column_(column) {
        toggle_.setColour(juce::ToggleButton::tickColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        toggle_.setColour(juce::ToggleButton::tickDisabledColourId,
                          DarkTheme::getColour(DarkTheme::TEXT_DIM));
        toggle_.onClick = [this]() {
            int paramIndex = owner_.getParamIndexForRow(row_);
            if (paramIndex >= 0 && paramIndex < static_cast<int>(owner_.parameters_.size())) {
                if (column_ == ColumnIds::Visible) {
                    owner_.parameters_[static_cast<size_t>(paramIndex)].isVisible =
                        toggle_.getToggleState();
                    owner_.updateTitle();
                } else if (column_ == ColumnIds::Mini) {
                    owner_.parameters_[static_cast<size_t>(paramIndex)].inMiniMixer =
                        toggle_.getToggleState();
                    owner_.updateTitle();
                }
            }
        };
        addAndMakeVisible(toggle_);
    }

    void update(int row, int column) {
        row_ = row;
        column_ = column;
        int paramIndex = owner_.getParamIndexForRow(row_);
        if (paramIndex >= 0 && paramIndex < static_cast<int>(owner_.parameters_.size())) {
            const auto& param = owner_.parameters_[static_cast<size_t>(paramIndex)];
            if (column_ == ColumnIds::Visible) {
                toggle_.setToggleState(param.isVisible, juce::dontSendNotification);
                toggle_.setEnabled(true);
                toggle_.setVisible(true);
            } else if (column_ == ColumnIds::Mini) {
                toggle_.setToggleState(param.inMiniMixer, juce::dontSendNotification);
                toggle_.setEnabled(true);
                toggle_.setVisible(true);
            }
        }
    }

    void resized() override {
        toggle_.setBounds(getLocalBounds().reduced(4));
    }

  private:
    ParameterConfigDialog& owner_;
    int row_;
    int column_;
    juce::ToggleButton toggle_;
};

//==============================================================================
// ComboCell - Dropdown cell for unit selection
//==============================================================================
class ParameterConfigDialog::ComboCell : public juce::Component {
  public:
    ComboCell(ParameterConfigDialog& owner, int row) : owner_(owner), row_(row) {
        combo_.addItem("%", 1);
        combo_.addItem("Hz", 2);
        combo_.addItem("dB", 3);
        combo_.addItem("ms", 4);
        combo_.addItem("sec", 5);
        combo_.addItem("semitones", 6);
        combo_.addItem("cents", 7);
        combo_.addItem("BPM", 8);
        combo_.addItem("discrete", 9);
        combo_.addItem("boolean", 10);

        combo_.setColour(juce::ComboBox::backgroundColourId,
                         DarkTheme::getColour(DarkTheme::SURFACE));
        combo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
        combo_.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);

        combo_.onChange = [this]() {
            int paramIndex = owner_.getParamIndexForRow(row_);
            if (paramIndex >= 0 && paramIndex < static_cast<int>(owner_.parameters_.size())) {
                owner_.parameters_[static_cast<size_t>(paramIndex)].unit = combo_.getText();
            }
        };
        addAndMakeVisible(combo_);
    }

    void update(int row) {
        row_ = row;
        int paramIndex = owner_.getParamIndexForRow(row_);
        if (paramIndex >= 0 && paramIndex < static_cast<int>(owner_.parameters_.size())) {
            const auto& param = owner_.parameters_[static_cast<size_t>(paramIndex)];
            // Find matching item in combo list
            bool found = false;
            for (int i = 0; i < combo_.getNumItems(); ++i) {
                if (combo_.getItemText(i) == param.unit) {
                    combo_.setSelectedItemIndex(i, juce::dontSendNotification);
                    found = true;
                    break;
                }
            }
            if (!found) {
                // Unit not in the combo list — default to "%"
                combo_.setSelectedItemIndex(0, juce::dontSendNotification);
            }
        }
    }

    void resized() override {
        combo_.setBounds(getLocalBounds().reduced(2));
    }

  private:
    ParameterConfigDialog& owner_;
    int row_;
    juce::ComboBox combo_;
};

//==============================================================================
// RangeCell - Editable text cell for min/max range
//==============================================================================
class ParameterConfigDialog::RangeCell : public juce::Component {
  public:
    RangeCell(ParameterConfigDialog& owner, int row) : owner_(owner), row_(row) {
        editor_.setColour(juce::TextEditor::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
        editor_.setColour(juce::TextEditor::textColourId, DarkTheme::getTextColour());
        editor_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        editor_.setFont(FontManager::getInstance().getUIFont(11.0f));
        editor_.setJustification(juce::Justification::centredLeft);

        editor_.onFocusLost = [this]() { commitEdit(); };
        editor_.onReturnKey = [this]() { commitEdit(); };

        addAndMakeVisible(editor_);
    }

    void update(int row) {
        row_ = row;
        int paramIndex = owner_.getParamIndexForRow(row_);
        if (paramIndex < 0 || paramIndex >= static_cast<int>(owner_.parameters_.size()))
            return;

        const auto& param = owner_.parameters_[static_cast<size_t>(paramIndex)];

        if (param.scale == magda::ParameterScale::Boolean) {
            editor_.setText("on / off", false);
            editor_.setEnabled(false);
        } else if (param.scale == magda::ParameterScale::Discrete) {
            juce::String text;
            for (size_t i = 0; i < param.choices.size(); ++i) {
                if (i > 0)
                    text += ", ";
                text += param.choices[i];
            }
            editor_.setText(text, false);
            editor_.setEnabled(true);
        } else {
            auto formatValue = [](float v) -> juce::String {
                if (std::isinf(v) && v < 0)
                    return "-inf";
                if (std::isinf(v))
                    return "+inf";
                return juce::String(v, 1);
            };
            editor_.setText(formatValue(param.rangeMin) + " — " + formatValue(param.rangeMax),
                            false);
            editor_.setEnabled(true);
        }
    }

    void resized() override {
        editor_.setBounds(getLocalBounds().reduced(2));
    }

  private:
    void commitEdit() {
        int paramIndex = owner_.getParamIndexForRow(row_);
        if (paramIndex < 0 || paramIndex >= static_cast<int>(owner_.parameters_.size()))
            return;

        auto& param = owner_.parameters_[static_cast<size_t>(paramIndex)];
        if (param.scale == magda::ParameterScale::Boolean)
            return;

        if (param.scale == magda::ParameterScale::Discrete) {
            auto text = editor_.getText().trim();
            auto tokens = juce::StringArray::fromTokens(text, ",", "");
            param.choices.clear();
            for (auto& t : tokens) {
                auto trimmed = t.trim();
                if (trimmed.isNotEmpty())
                    param.choices.push_back(trimmed);
            }
            param.rangeMax =
                param.choices.empty() ? 0.0f : static_cast<float>(param.choices.size() - 1);
            return;
        }

        auto text = editor_.getText().trim();
        // Parse "min — max" or "min - max" or "min max"
        juce::String minStr, maxStr;
        // Look for em-dash separator (U+2014)
        int sepIdx = text.indexOfChar(0x2014);
        // Fall back to hyphen, but skip a leading minus sign
        if (sepIdx < 0)
            sepIdx = text.indexOf(1, "-");
        if (sepIdx < 0) {
            // Try space separation
            auto tokens = juce::StringArray::fromTokens(text, " ", "");
            if (tokens.size() >= 2) {
                minStr = tokens[0];
                maxStr = tokens[tokens.size() - 1];
            }
        } else {
            minStr = text.substring(0, sepIdx).trim();
            maxStr = text.substring(sepIdx + 1).trim();
        }

        if (minStr.isNotEmpty() && maxStr.isNotEmpty()) {
            float newMin, newMax;
            if (minStr.toLowerCase() == "-inf")
                newMin = -std::numeric_limits<float>::infinity();
            else
                newMin = minStr.getFloatValue();

            if (maxStr.toLowerCase() == "+inf" || maxStr.toLowerCase() == "inf")
                newMax = std::numeric_limits<float>::infinity();
            else
                newMax = maxStr.getFloatValue();

            param.rangeMin = newMin;
            param.rangeMax = newMax;
        }
    }

    ParameterConfigDialog& owner_;
    int row_;
    juce::TextEditor editor_;
};

//==============================================================================
// ParameterConfigDialog
//==============================================================================
ParameterConfigDialog::~ParameterConfigDialog() {
    setLookAndFeel(nullptr);
}

ParameterConfigDialog::ParameterConfigDialog(const juce::String& pluginName)
    : pluginName_(pluginName) {
    // Use the shared dialog look-and-feel so every button / combo /
    // text editor in the dialog picks up the theme font (Inter) instead
    // of JUCE's platform default. Children inherit unless they set their
    // own LaF.
    setLookAndFeel(&DialogLookAndFeel::getInstance());

    // Title
    titleLabel_.setText("Configure Parameters - " + pluginName_, juce::dontSendNotification);
    titleLabel_.setFont(FontManager::getInstance().getUIFontBold(14.0f));
    titleLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addAndMakeVisible(titleLabel_);

    // Setup table
    table_.setModel(this);
    table_.setColour(juce::ListBox::backgroundColourId,
                     DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
    table_.setColour(juce::ListBox::outlineColourId, DarkTheme::getBorderColour());
    table_.setOutlineThickness(1);
    table_.setRowHeight(28);

    auto& header = table_.getHeader();
    header.addColumn("Parameter", ParamName, 150, 100, 300);
    header.addColumn("Visible", Visible, 60, 60, 60);
    header.addColumn("Mini FX", Mini, 64, 64, 72);
    header.addColumn("Unit", Unit, 90, 70, 120);
    header.addColumn("Range", Range, 180, 120, 300);

    header.setColour(juce::TableHeaderComponent::backgroundColourId,
                     DarkTheme::getColour(DarkTheme::SURFACE));
    header.setColour(juce::TableHeaderComponent::textColourId, DarkTheme::getTextColour());

    addAndMakeVisible(table_);

    // Buttons
    okButton_.setButtonText("OK");
    okButton_.setColour(juce::TextButton::buttonColourId,
                        DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    okButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    okButton_.onClick = [this]() {
        saveParameterConfiguration();
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
            dw->exitModalState(1);
        }
    };
    addAndMakeVisible(okButton_);

    cancelButton_.setButtonText("Cancel");
    cancelButton_.setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    cancelButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    cancelButton_.onClick = [this]() {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
            dw->exitModalState(0);
        }
    };
    addAndMakeVisible(cancelButton_);

    applyButton_.setButtonText("Apply");
    applyButton_.setColour(juce::TextButton::buttonColourId,
                           DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    applyButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    applyButton_.onClick = [this]() {
        saveParameterConfiguration();
        DBG("Applied parameter config");
    };
    addAndMakeVisible(applyButton_);

    // Select/Deselect all buttons
    selectAllButton_.setButtonText("Select All");
    selectAllButton_.setColour(juce::TextButton::buttonColourId,
                               DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    selectAllButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    selectAllButton_.onClick = [this]() { selectAllParameters(); };
    addAndMakeVisible(selectAllButton_);

    deselectAllButton_.setButtonText("Deselect All");
    deselectAllButton_.setColour(juce::TextButton::buttonColourId,
                                 DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    deselectAllButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    deselectAllButton_.onClick = [this]() { deselectAllParameters(); };
    addAndMakeVisible(deselectAllButton_);

    // Detect button — run the deterministic heuristic pass (no LLM).
    // Fast, offline, picks up units/ranges that the plugin's own display
    // strings give away.
    detectButton_.setButtonText("Detect");
    detectButton_.setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    detectButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    detectButton_.onClick = [this]() { runHeuristicDetection(); };
    addAndMakeVisible(detectButton_);

    // Reset button — wipe all inferred units/ranges and put every parameter
    // back to a plain 0–100 % view.
    resetButton_.setButtonText("Reset");
    resetButton_.setColour(juce::TextButton::buttonColourId,
                           DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    resetButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    resetButton_.onClick = [this]() {
        auto* alert = new juce::AlertWindow(
            "Reset parameter configuration?",
            "Discard all inferred units, ranges, and AI-detected values for \"" + pluginName_ +
                "\" and show every parameter as a plain 0–100 % slider.\n\n"
                "This cannot be undone.",
            juce::AlertWindow::QuestionIcon);
        alert->addButton("Reset", 1, juce::KeyPress(juce::KeyPress::returnKey));
        alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<ParameterConfigDialog> safeThis(this);
        alert->enterModalState(true,
                               juce::ModalCallbackFunction::create([alert, safeThis](int result) {
                                   std::unique_ptr<juce::AlertWindow> owner(alert);
                                   if (result != 1 || safeThis == nullptr)
                                       return;
                                   safeThis->resetParameterConfiguration();
                               }),
                               false);
    };
    addAndMakeVisible(resetButton_);

    // AI Detect button
    aiDetectButton_.setButtonText("AI Detect");
    aiDetectButton_.setColour(juce::TextButton::buttonColourId,
                              DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    aiDetectButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    aiDetectButton_.onClick = [this]() {
        if (detecting_) {
            // Cancel
            if (cancelFlag_)
                cancelFlag_->store(true);
            setDetecting(false);
            aiStatusLabel_.setText("Cancelled", juce::dontSendNotification);
        } else {
            runDetection();
        }
    };
    addAndMakeVisible(aiDetectButton_);

    // AI status label (shows streaming tokens)
    aiStatusLabel_.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_DIM));
    aiStatusLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    aiStatusLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(aiStatusLabel_);

    // Search box
    searchLabel_.setText("Search:", juce::dontSendNotification);
    searchLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addAndMakeVisible(searchLabel_);

    searchBox_.setColour(juce::TextEditor::backgroundColourId,
                         DarkTheme::getColour(DarkTheme::SURFACE));
    searchBox_.setColour(juce::TextEditor::textColourId, DarkTheme::getTextColour());
    searchBox_.setColour(juce::TextEditor::outlineColourId, DarkTheme::getBorderColour());
    searchBox_.onTextChange = [this]() { filterParameters(searchBox_.getText()); };
    addAndMakeVisible(searchBox_);

    // Build mock data
    buildMockParameters();
    rebuildFilteredList();

    setSize(660, 500);
}

void ParameterConfigDialog::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
}

void ParameterConfigDialog::resized() {
    auto bounds = getLocalBounds().reduced(16);

    // Title at top
    titleLabel_.setBounds(bounds.removeFromTop(28));
    bounds.removeFromTop(12);

    // Search box
    auto searchRow = bounds.removeFromTop(28);
    bounds.removeFromTop(8);
    searchLabel_.setBounds(searchRow.removeFromLeft(50));
    searchRow.removeFromLeft(4);
    searchBox_.setBounds(searchRow);
    bounds.removeFromTop(8);

    // Select/Deselect all buttons
    auto selectionButtonRow = bounds.removeFromTop(28);
    bounds.removeFromTop(8);
    const int selButtonWidth = 90;
    const int selButtonSpacing = 8;
    selectAllButton_.setBounds(selectionButtonRow.removeFromLeft(selButtonWidth));
    selectionButtonRow.removeFromLeft(selButtonSpacing);
    deselectAllButton_.setBounds(selectionButtonRow.removeFromLeft(selButtonWidth));
    selectionButtonRow.removeFromLeft(selButtonSpacing);
    detectButton_.setBounds(selectionButtonRow.removeFromLeft(selButtonWidth));
    selectionButtonRow.removeFromLeft(selButtonSpacing);
    aiDetectButton_.setBounds(selectionButtonRow.removeFromLeft(selButtonWidth));
    selectionButtonRow.removeFromLeft(selButtonSpacing);
    resetButton_.setBounds(selectionButtonRow.removeFromLeft(selButtonWidth));
    selectionButtonRow.removeFromLeft(selButtonSpacing);
    aiStatusLabel_.setBounds(selectionButtonRow);

    // Buttons at bottom
    auto buttonRow = bounds.removeFromBottom(32);
    bounds.removeFromBottom(12);

    const int buttonWidth = 80;
    const int buttonSpacing = 8;

    okButton_.setBounds(buttonRow.removeFromRight(buttonWidth));
    buttonRow.removeFromRight(buttonSpacing);
    applyButton_.setBounds(buttonRow.removeFromRight(buttonWidth));
    buttonRow.removeFromRight(buttonSpacing);
    cancelButton_.setBounds(buttonRow.removeFromRight(buttonWidth));

    // Table takes remaining space
    table_.setBounds(bounds);
}

int ParameterConfigDialog::getNumRows() {
    return static_cast<int>(filteredIndices_.size());
}

void ParameterConfigDialog::paintRowBackground(juce::Graphics& g, int rowNumber, int width,
                                               int height, bool rowIsSelected) {
    if (rowIsSelected) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.2f));
    } else if (rowNumber % 2 == 0) {
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE).withAlpha(0.3f));
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
    }
    g.fillRect(0, 0, width, height);
}

void ParameterConfigDialog::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width,
                                      int height, bool /*rowIsSelected*/) {
    if (rowNumber >= static_cast<int>(filteredIndices_.size()))
        return;

    int paramIndex = filteredIndices_[static_cast<size_t>(rowNumber)];
    if (paramIndex >= static_cast<int>(parameters_.size()))
        return;

    const auto& param = parameters_[static_cast<size_t>(paramIndex)];

    g.setColour(DarkTheme::getTextColour());
    g.setFont(FontManager::getInstance().getUIFont(11.0f));

    if (columnId == ParamName) {
        g.drawText(param.name, 8, 0, width - 16, height, juce::Justification::centredLeft);
    }
    // Range column is handled by RangeCell component
}

juce::Component* ParameterConfigDialog::refreshComponentForCell(int rowNumber, int columnId,
                                                                bool /*isRowSelected*/,
                                                                Component* existingComponent) {
    if (rowNumber >= static_cast<int>(parameters_.size()))
        return nullptr;

    // ParamName column is painted, not a component
    if (columnId == ParamName)
        return nullptr;

    if (columnId == Visible || columnId == Mini) {
        auto* toggle = dynamic_cast<ToggleCell*>(existingComponent);
        if (toggle == nullptr) {
            toggle = new ToggleCell(*this, rowNumber, columnId);
        }
        toggle->update(rowNumber, columnId);
        return toggle;
    }

    if (columnId == Unit) {
        auto* combo = dynamic_cast<ComboCell*>(existingComponent);
        if (combo == nullptr) {
            combo = new ComboCell(*this, rowNumber);
        }
        combo->update(rowNumber);
        return combo;
    }

    if (columnId == Range) {
        auto* rangeCell = dynamic_cast<RangeCell*>(existingComponent);
        if (rangeCell == nullptr) {
            rangeCell = new RangeCell(*this, rowNumber);
        }
        rangeCell->update(rowNumber);
        return rangeCell;
    }

    return nullptr;
}

void ParameterConfigDialog::updateTitle() {
    int miniCount = 0;
    for (const auto& p : parameters_)
        if (p.inMiniMixer)
            miniCount++;

    // Internal devices expose only the Mini FX choice (no Visible column).
    if (isInternalPlugin_) {
        titleLabel_.setText(pluginName_ + " - " + juce::String(miniCount) + " / " +
                                juce::String(parameters_.size()) + " mini FX",
                            juce::dontSendNotification);
        return;
    }

    int visibleCount = 0;
    for (const auto& p : parameters_)
        if (p.isVisible)
            visibleCount++;
    titleLabel_.setText(pluginName_ + " - " + juce::String(visibleCount) + " / " +
                            juce::String(parameters_.size()) + " params visible, " +
                            juce::String(miniCount) + " mini FX",
                        juce::dontSendNotification);
}

void ParameterConfigDialog::buildMockParameters() {
    // Mock parameters that might be in a typical plugin like FabFilter Pro-Q 3
    auto makeParam = [](juce::String name, float defaultValue, bool visible, juce::String unit,
                        float min, float max, float centre) {
        MockParameterInfo param;
        param.name = std::move(name);
        param.defaultValue = defaultValue;
        param.isVisible = visible;
        param.unit = std::move(unit);
        param.rangeMin = min;
        param.rangeMax = max;
        param.rangeCenter = centre;
        return param;
    };

    parameters_ = {
        makeParam("Output Gain", 0.5f, true, "dB", -30.0f, 30.0f, 0.0f),
        makeParam("Mix", 1.0f, true, "%", 0.0f, 100.0f, 50.0f),
        makeParam("Band 1 Frequency", 0.3f, true, "Hz", 20.0f, 20000.0f, 1000.0f),
        makeParam("Band 1 Gain", 0.5f, true, "dB", -30.0f, 30.0f, 0.0f),
        makeParam("Band 1 Q", 0.5f, true, "%", 0.1f, 10.0f, 1.0f),
        makeParam("Band 1 Type", 0.0f, true, "%", 0.0f, 1.0f, 0.5f),
        makeParam("Band 2 Frequency", 0.5f, true, "Hz", 20.0f, 20000.0f, 1000.0f),
        makeParam("Band 2 Gain", 0.5f, true, "dB", -30.0f, 30.0f, 0.0f),
        makeParam("Band 2 Q", 0.5f, true, "%", 0.1f, 10.0f, 1.0f),
        makeParam("Band 3 Frequency", 0.7f, true, "Hz", 20.0f, 20000.0f, 1000.0f),
        makeParam("Band 3 Gain", 0.5f, true, "dB", -30.0f, 30.0f, 0.0f),
        makeParam("Band 3 Q", 0.5f, true, "%", 0.1f, 10.0f, 1.0f),
        makeParam("Analyzer Mode", 0.0f, false, "%", 0.0f, 1.0f, 0.5f),
        makeParam("Auto Gain", 0.0f, true, "%", 0.0f, 1.0f, 0.5f),
        makeParam("Master Level", 0.8f, true, "dB", -60.0f, 12.0f, 0.0f),
    };
}

void ParameterConfigDialog::show(const juce::String& pluginName, juce::Component* /*parent*/) {
    auto* dialog = new ParameterConfigDialog(pluginName);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Parameter Configuration";
    options.dialogBackgroundColour = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
    options.content.setOwned(dialog);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    options.launchAsync();
}

void ParameterConfigDialog::showForPlugin(const juce::String& uniqueId,
                                          const juce::String& pluginName,
                                          juce::Component* /*parent*/) {
    auto* dialog = new ParameterConfigDialog(pluginName);
    dialog->pluginUniqueId_ = uniqueId;

    // Load parameters from the plugin
    dialog->loadParameters(uniqueId);

    // MAGDA internal devices have full native UIs, so the generic "Visible"
    // param-grid selection is meaningless for them. Drop that column and expose
    // only the Mini FX choice (which params surface in the mixer mini-chain row).
    if (dialog->isInternalPlugin_)
        dialog->table_.getHeader().removeColumn(ColumnIds::Visible);

    // Rebuild filtered list to include all loaded parameters
    dialog->rebuildFilteredList();

    // Try to load saved configuration
    dialog->loadParameterConfiguration();

    // Refresh table to show loaded data
    dialog->table_.updateContent();
    dialog->updateTitle();

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Configure Parameters - " + pluginName;
    options.dialogBackgroundColour = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
    options.content.setOwned(dialog);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    options.launchAsync();
}

void ParameterConfigDialog::loadParameters(const juce::String& uniqueId) {
    // MAGDA devices are identified by their pluginId (not present in the
    // external KnownPluginList) via either the internal-device registry or the
    // compiled-Faust registry. They scan real params and hide the Visible col.
    isInternalPlugin_ = (magda::daw::audio::findInternalPluginSpec(uniqueId) != nullptr ||
                         magda::daw::audio::compiled::findCompiledPluginSpec(uniqueId) != nullptr);

    // Check if we have cached parameters for this plugin
    auto it = parameterCache_.find(uniqueId);
    if (it != parameterCache_.end()) {
        DBG("Loading cached parameters for " << uniqueId);
        parameters_ = it->second.parameters;
        scanInputs_ = it->second.scanInputs;
        return;
    }

    // MAGDA device: scan its live automatable parameters.
    if (isInternalPlugin_) {
        if (scanInternalParameters(uniqueId)) {
            parameterCache_[uniqueId] = {parameters_, scanInputs_};
            return;
        }
        DBG("Internal parameter scan failed for " << uniqueId << " - falling back to mock");
        buildMockParameters();
        return;
    }

    DBG("Scanning parameters for " << uniqueId);

    // Get access to the audio engine to load the plugin
    auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
    if (!audioEngine) {
        DBG("No audio engine available");
        buildMockParameters();
        return;
    }

    // Get the TracktionEngineWrapper to access KnownPluginList
    auto* tracktionEngine = dynamic_cast<magda::TracktionEngineWrapper*>(audioEngine);
    if (!tracktionEngine) {
        DBG("Audio engine is not TracktionEngineWrapper");
        buildMockParameters();
        return;
    }

    // Find the plugin description in the known plugin list (copy by value —
    // createPluginInstance can modify the list, invalidating pointers into it)
    auto& knownPlugins = tracktionEngine->getKnownPluginList();
    juce::PluginDescription pluginDesc;
    bool found = false;

    for (const auto& desc : knownPlugins.getTypes()) {
        if (desc.createIdentifierString() == uniqueId) {
            pluginDesc = desc;
            found = true;
            break;
        }
    }

    if (!found) {
        DBG("Plugin description not found for " << uniqueId);
        buildMockParameters();
        return;
    }

    // Instantiate the plugin temporarily to scan its parameters
    juce::String errorMessage;
    auto& formatManager = tracktionEngine->getEdit()->engine.getPluginManager().pluginFormatManager;

    auto instance = formatManager.createPluginInstance(pluginDesc, 44100.0, 512, errorMessage);

    if (!instance) {
        DBG("Failed to instantiate plugin: " << errorMessage);
        buildMockParameters();
        return;
    }

    // Scan all parameters from the plugin
    parameters_.clear();
    scanInputs_.clear();
    int numParams = instance->getParameters().size();

    // Sample points for display text extraction
    const float samplePoints[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

    for (int i = 0; i < numParams; ++i) {
        auto* param = instance->getParameters()[i];
        if (!param)
            continue;

        // Match TE's ExternalPlugin filtering: only include automatable params
        if (!param->isAutomatable())
            continue;

        auto paramName = param->getName(128);
        if (paramName.isEmpty())
            continue;

        MockParameterInfo info;
        info.name = paramName;
        info.defaultValue = param->getDefaultValue();
        info.isVisible = true;  // All visible by default
        // Plugin labels can be messy (e.g. "% [-96.0dB...6.0dB]").
        // Only keep short, clean labels as the unit; discard the rest.
        auto rawLabel = param->getLabel().trim();
        if (rawLabel.length() <= 6 && !rawLabel.contains("[") && !rawLabel.contains("("))
            info.unit = rawLabel.isEmpty() ? "%" : rawLabel;
        else
            info.unit = "%";

        // Build scan input for detection
        magda::ParameterScanInput scanInput;
        scanInput.paramIndex = i;
        scanInput.name = info.name;
        scanInput.label = rawLabel;  // Pass raw label to detector for heuristics

        // Try to get parameter range if it's a RangedAudioParameter
        if (auto* rangedParam = dynamic_cast<juce::RangedAudioParameter*>(param)) {
            auto range = rangedParam->getNormalisableRange();
            info.rangeMin = range.start;
            info.rangeMax = range.end;
            info.rangeCenter = (range.start + range.end) / 2.0f;
            scanInput.rangeMin = range.start;
            scanInput.rangeMax = range.end;
        } else {
            info.rangeMin = 0.0f;
            info.rangeMax = 1.0f;
            info.rangeCenter = 0.5f;
            scanInput.rangeMin = 0.0f;
            scanInput.rangeMax = 1.0f;
        }

        // Get state count for discrete detection
        scanInput.stateCount = param->getNumSteps();
        // JUCE returns 0x7fffffff for continuous params
        if (scanInput.stateCount > 1000)
            scanInput.stateCount = 0;

        // Collect display texts for detection (sampled) and full value table for display.
        if (scanInput.stateCount > 0 && scanInput.stateCount <= 1000) {
            // Discrete: get ALL state labels
            for (int s = 0; s < scanInput.stateCount; ++s) {
                float norm =
                    (scanInput.stateCount == 1)
                        ? 0.0f
                        : static_cast<float>(s) / static_cast<float>(scanInput.stateCount - 1);
                auto text = param->getText(norm, 128);
                scanInput.displayTexts.push_back(text);
            }
            info.valueTable = scanInput.displayTexts;
        } else {
            // Sample 5 points for detection
            for (auto sp : samplePoints) {
                scanInput.displayTexts.push_back(param->getText(sp, 128));
            }

            // If all sampled texts look like labels (start with a letter),
            // this is likely a discrete param that JUCE reports as continuous.
            // Sweep to discover all unique choices.
            bool allLabels = true;
            for (const auto& t : scanInput.displayTexts) {
                auto trimmed = t.trim();
                if (trimmed.isEmpty() || !((trimmed[0] >= 'A' && trimmed[0] <= 'Z') ||
                                           (trimmed[0] >= 'a' && trimmed[0] <= 'z'))) {
                    allLabels = false;
                    break;
                }
            }
            if (allLabels) {
                scanInput.displayTexts.clear();
                std::vector<juce::String> seen;
                for (int s = 0; s <= 1000; ++s) {
                    float norm = static_cast<float>(s) / 1000.0f;
                    auto text = param->getText(norm, 128);
                    if (seen.empty() || seen.back() != text) {
                        seen.push_back(text);
                        scanInput.displayTexts.push_back(text);
                    }
                }
                info.valueTable = scanInput.displayTexts;
            } else {
                // Continuous: collect full value table (every 0.1% step)
                const int tableSize = 1001;
                info.valueTable.reserve(tableSize);
                for (int s = 0; s < tableSize; ++s) {
                    float norm = static_cast<float>(s) / static_cast<float>(tableSize - 1);
                    info.valueTable.push_back(param->getText(norm, 128));
                }
            }
        }

        parameters_.push_back(info);
        scanInputs_.push_back(std::move(scanInput));
    }

    DBG("Scanned " << parameters_.size() << " parameters");

    // Cache the results for future use
    parameterCache_[uniqueId] = {parameters_, scanInputs_};
}

bool ParameterConfigDialog::scanInternalParameters(const juce::String& pluginId) {
    auto* audioEngine = magda::TrackManager::getInstance().getAudioEngine();
    auto* tracktionEngine = dynamic_cast<magda::TracktionEngineWrapper*>(audioEngine);
    if (tracktionEngine == nullptr) {
        DBG("No TracktionEngineWrapper for internal scan");
        return false;
    }

    auto* edit = tracktionEngine->getEdit();
    if (edit == nullptr) {
        DBG("No edit for internal scan");
        return false;
    }

    // Spin up a throwaway instance just to read its parameter list. It lives in
    // the edit's plugin cache (never added to a track) and is released when this
    // Ptr drops at the end of the scan. Internal-registry devices go through
    // their spec (which handles short ids / fresh-state quirks); compiled-Faust
    // devices instantiate by xmlTypeName via the engine's createCustomPlugin.
    namespace te = tracktion::engine;
    te::Plugin::Ptr plugin;
    if (const auto* internalSpec = magda::daw::audio::findInternalPluginSpec(pluginId)) {
        plugin = magda::daw::audio::createInternalPluginFromSpec(*internalSpec, *edit);
    } else {
        // Compiled-Faust devices: instantiate from a typed PLUGIN ValueTree
        // (the bare-string createNewPlugin overload doesn't route custom types).
        juce::ValueTree pluginState(te::IDs::PLUGIN);
        pluginState.setProperty(te::IDs::type, pluginId, nullptr);
        plugin = edit->getPluginCache().createNewPlugin(pluginState);
    }

    if (plugin == nullptr) {
        DBG("Failed to create plugin for scan: " << pluginId);
        return false;
    }

    parameters_.clear();
    scanInputs_.clear();

    const float samplePoints[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

    auto teParams = plugin->getAutomatableParameters();
    for (int i = 0; i < teParams.size(); ++i) {
        auto* param = teParams[i];
        if (param == nullptr)
            continue;

        auto name = param->getParameterName();
        if (name.isEmpty())
            continue;

        const auto range = param->getValueRange();
        const float rangeMin = range.getStart();
        const float rangeMax = range.getEnd();

        MockParameterInfo info;
        info.name = name;
        info.isVisible = true;  // unused for internal devices (Visible col hidden)
        info.defaultValue = param->getDefaultValue().value_or(rangeMin);
        info.rangeMin = rangeMin;
        info.rangeMax = rangeMax;
        info.rangeCenter = (rangeMin + rangeMax) * 0.5f;
        info.unit = "%";  // Detect can refine units/scale from the display texts

        const int numStates = param->getNumberOfStates();
        if (numStates == 2)
            info.scale = magda::ParameterScale::Boolean;
        else if (numStates > 0 && numStates <= 12)
            info.scale = magda::ParameterScale::Discrete;
        else
            info.scale = magda::ParameterScale::Linear;

        magda::ParameterScanInput scanInput;
        scanInput.paramIndex = i;
        scanInput.name = name;
        scanInput.label = param->getLabel();
        scanInput.rangeMin = rangeMin;
        scanInput.rangeMax = rangeMax;
        scanInput.stateCount = (numStates > 1 && numStates <= 1000) ? numStates : 0;

        // Sample display texts so Detect can infer real units/scales. TE's
        // valueToString expects a plugin-native (raw) value.
        for (float sp : samplePoints) {
            const float raw = rangeMin + (rangeMax - rangeMin) * sp;
            scanInput.displayTexts.push_back(param->valueToString(raw));
        }
        if (info.scale == magda::ParameterScale::Discrete ||
            info.scale == magda::ParameterScale::Boolean)
            info.valueTable = scanInput.displayTexts;

        parameters_.push_back(std::move(info));
        scanInputs_.push_back(std::move(scanInput));
    }

    DBG("Scanned " << parameters_.size() << " internal params for " << pluginId);
    return !parameters_.empty();
}

void ParameterConfigDialog::selectAllParameters() {
    // Internal devices only expose the Mini FX choice; Select All targets that.
    for (auto& param : parameters_) {
        if (isInternalPlugin_)
            param.inMiniMixer = true;
        else
            param.isVisible = true;
    }
    table_.updateContent();
    updateTitle();
}

void ParameterConfigDialog::resetParameterConfiguration() {
    // Delete the persisted XML so next time the dialog opens (or a project
    // reloads this plugin) applyConfigToDevice finds nothing and the plugin
    // keeps its native metadata.
    if (!pluginUniqueId_.isEmpty()) {
        auto configFile = magda::paths::pluginConfigsDir().getChildFile(
            pluginUniqueId_.replaceCharacters(":/\\,; ", "______") + ".xml");
        if (configFile.existsAsFile()) {
            configFile.deleteFile();
            DBG("Deleted parameter config: " << configFile.getFullPathName());
        }
    }

    // Restore scanInputs_ from the pristine cache so Detect / AI Detect can
    // be re-run later. The user-facing parameters_ list keeps names and
    // visibility, but every inferred unit/range/scale is wiped back to a
    // plain normalized percentage.
    auto it = parameterCache_.find(pluginUniqueId_);
    if (it != parameterCache_.end()) {
        scanInputs_ = it->second.scanInputs;
    }

    for (auto& param : parameters_) {
        param.unit = "%";
        param.scale = magda::ParameterScale::Linear;
        param.rangeMin = 0.0f;
        param.rangeMax = 1.0f;
        param.rangeCenter = 0.5f;
        param.choices.clear();
        param.inMiniMixer = false;
    }

    rebuildFilteredList();
    table_.updateContent();
    updateTitle();
    aiStatusLabel_.setText("Reset to 0–100 %", juce::dontSendNotification);
}

void ParameterConfigDialog::runHeuristicDetection() {
    if (scanInputs_.empty()) {
        aiStatusLabel_.setText("No parameters to detect", juce::dontSendNotification);
        return;
    }

    std::vector<magda::ParameterScanInput> visibleInputs;
    for (size_t i = 0; i < scanInputs_.size() && i < parameters_.size(); ++i) {
        if (parameters_[i].isVisible)
            visibleInputs.push_back(scanInputs_[i]);
    }

    if (visibleInputs.empty()) {
        aiStatusLabel_.setText("No visible params to detect", juce::dontSendNotification);
        return;
    }

    auto results = magda::ParameterDetector::detect(visibleInputs);

    int resolved = 0, ambiguous = 0;
    for (const auto& r : results) {
        if (r.confidence > 0.0f)
            resolved++;
        else
            ambiguous++;
    }

    applyDetectionResults(results);

    juce::String status =
        juce::String(resolved) + " / " + juce::String(visibleInputs.size()) + " resolved";
    if (ambiguous > 0)
        status += " — use AI Detect for the rest";
    aiStatusLabel_.setText(status, juce::dontSendNotification);
}

void ParameterConfigDialog::deselectAllParameters() {
    for (auto& param : parameters_) {
        if (isInternalPlugin_)
            param.inMiniMixer = false;
        else
            param.isVisible = false;
    }
    table_.updateContent();
    updateTitle();
}

void ParameterConfigDialog::setDetecting(bool detecting) {
    detecting_ = detecting;

    // During detection: disable everything except Cancel (repurpose the cancel button)
    okButton_.setEnabled(!detecting);
    applyButton_.setEnabled(!detecting);
    selectAllButton_.setEnabled(!detecting);
    deselectAllButton_.setEnabled(!detecting);
    detectButton_.setEnabled(!detecting);
    resetButton_.setEnabled(!detecting);
    searchBox_.setEnabled(!detecting);
    table_.setEnabled(!detecting);

    if (detecting) {
        aiDetectButton_.setButtonText("Cancel");
        aiDetectButton_.setColour(juce::TextButton::buttonColourId,
                                  DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
        cancelButton_.setEnabled(false);
        dotCount_ = 0;
        startTimerHz(3);
    } else {
        aiDetectButton_.setButtonText("AI Detect");
        aiDetectButton_.setColour(juce::TextButton::buttonColourId,
                                  DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
        cancelButton_.setEnabled(true);
        stopTimer();
    }
}

void ParameterConfigDialog::timerCallback() {
    dotCount_ = (dotCount_ + 1) % 4;
    juce::String dots;
    for (int i = 0; i < dotCount_; ++i)
        dots += ".";
    juce::String status = juce::String(aiResolved_) + " / " + juce::String(aiTotal_) + " resolved";
    aiStatusLabel_.setText(status + dots, juce::dontSendNotification);
}

void ParameterConfigDialog::runDetection() {
    if (scanInputs_.empty()) {
        DBG("No scan inputs available for detection");
        return;
    }

    // Filter to only visible parameters
    std::vector<magda::ParameterScanInput> visibleInputs;
    for (size_t i = 0; i < scanInputs_.size() && i < parameters_.size(); ++i) {
        if (parameters_[i].isVisible)
            visibleInputs.push_back(scanInputs_[i]);
    }

    if (visibleInputs.empty()) {
        aiStatusLabel_.setText("No visible params to detect", juce::dontSendNotification);
        return;
    }

    DBG("Running detection on " << visibleInputs.size() << " visible params (of "
                                << scanInputs_.size() << " total)");
    for (size_t i = 0; i < visibleInputs.size(); ++i) {
        const auto& vi = visibleInputs[i];
        juce::String texts;
        for (const auto& t : vi.displayTexts)
            texts += t + ", ";
        DBG("  visibleInput[" << i << "] idx=" << vi.paramIndex << " name='" << vi.name
                              << "' label='" << vi.label << "' range=[" << vi.rangeMin << ","
                              << vi.rangeMax << "] states=" << vi.stateCount << " texts=[" << texts
                              << "]");
    }

    // Run deterministic detection (instant) — resolves boolean, discrete,
    // and continuous params where display text gives a clear unit.
    // Everything else (confidence == 0) goes to AI.
    auto results = magda::ParameterDetector::detect(visibleInputs);

    int deterministicResolved = 0, ambiguousCount = 0;
    for (size_t i = 0; i < results.size(); ++i) {
        if (results[i].confidence > 0.0f) {
            deterministicResolved++;
            DBG("  RESOLVED[" << i << "] '" << visibleInputs[i].name << "' unit=" << results[i].unit
                              << " scale=" << (int)results[i].scale << " range=["
                              << results[i].minValue << "," << results[i].maxValue << "]");
        } else {
            ambiguousCount++;
            DBG("  AMBIGUOUS[" << i << "] '" << visibleInputs[i].name << "'");
        }
    }
    DBG("Deterministic: " << deterministicResolved << " resolved, " << ambiguousCount
                          << " ambiguous -> AI");

    // Map results back to the full parameters_ array
    applyDetectionResults(results);

    if (ambiguousCount > 0) {
        // Check if LLM is configured
        auto agentConfig = magda::Config::getInstance().getAgentLLMConfig("command");
        if (!agentConfig.provider.empty()) {
            // Freeze UI during AI detection
            aiTotal_ = ambiguousCount;
            aiResolved_ = 0;
            cancelFlag_ = std::make_shared<std::atomic<bool>>(false);
            setDetecting(true);

            auto safeThis = juce::Component::SafePointer<ParameterConfigDialog>(this);

            magda::ParameterDetector::detectWithAI(
                pluginName_, visibleInputs, results, 0.5f, cancelFlag_,
                // onProgress
                [safeThis](int resolved, int /*total*/) {
                    if (!safeThis)
                        return;
                    safeThis->aiResolved_ = resolved;
                },
                // onComplete
                [safeThis](std::vector<magda::DetectedParameterInfo> aiResults) {
                    if (!safeThis)
                        return;
                    safeThis->applyDetectionResults(aiResults);
                    safeThis->setDetecting(false);
                    safeThis->aiStatusLabel_.setText(juce::String(safeThis->aiResolved_) + " / " +
                                                         juce::String(safeThis->aiTotal_) +
                                                         " resolved",
                                                     juce::dontSendNotification);
                });
        } else {
            aiStatusLabel_.setText(juce::String(ambiguousCount) +
                                       " params need AI (no LLM configured)",
                                   juce::dontSendNotification);
        }
    } else {
        aiStatusLabel_.setText("All params resolved", juce::dontSendNotification);
    }
}

void ParameterConfigDialog::applyDetectionResults(
    const std::vector<magda::DetectedParameterInfo>& results) {
    for (const auto& r : results) {
        if (r.paramIndex < 0 || r.paramIndex >= static_cast<int>(parameters_.size()))
            continue;

        auto& param = parameters_[static_cast<size_t>(r.paramIndex)];
        param.scale = r.scale;
        param.rangeMin = r.minValue;
        param.rangeMax = r.maxValue;
        param.rangeCenter = (r.minValue + r.maxValue) / 2.0f;
        if (!r.choices.empty())
            param.choices = r.choices;

        // Discrete/boolean params show their scale type instead of a unit.
        // Continuous params with no detected unit default to "%".
        if (r.scale == magda::ParameterScale::Discrete) {
            param.unit = "discrete";
        } else if (r.scale == magda::ParameterScale::Boolean) {
            param.unit = "boolean";
        } else {
            param.unit = r.unit.isEmpty() ? "%" : r.unit;
        }
    }
    table_.updateContent();
    table_.repaint();
}

void ParameterConfigDialog::saveParameterConfiguration() {
    if (pluginUniqueId_.isEmpty()) {
        DBG("Cannot save parameter config - no plugin unique ID");
        return;
    }

    // Get the config directory
    auto configDir = magda::paths::pluginConfigsDir();

    if (!configDir.exists()) {
        configDir.createDirectory();
    }

    // Create config file for this plugin
    auto configFile =
        configDir.getChildFile(pluginUniqueId_.replaceCharacters(":/\\,; ", "______") + ".xml");

    juce::XmlElement root("ParameterConfig");
    root.setAttribute("pluginId", pluginUniqueId_);

    // Save visible parameters and detection data
    auto* paramsElem = root.createNewChildElement("Parameters");
    int visibleCount = 0;
    for (size_t i = 0; i < parameters_.size(); ++i) {
        const auto& p = parameters_[i];
        auto* paramElem = paramsElem->createNewChildElement("Param");
        paramElem->setAttribute("index", static_cast<int>(i));
        paramElem->setAttribute("name", p.name);
        // Internal devices intentionally expose no Visible params (only Mini FX),
        // so never persist visibility for them regardless of the default state.
        paramElem->setAttribute("visible", !isInternalPlugin_ && p.isVisible);
        paramElem->setAttribute("mini", p.inMiniMixer);
        paramElem->setAttribute("unit", p.unit);
        paramElem->setAttribute("scale", scaleToXmlString(p.scale));
        paramElem->setAttribute("min", static_cast<double>(p.rangeMin));
        paramElem->setAttribute("max", static_cast<double>(p.rangeMax));
        paramElem->setAttribute("center", static_cast<double>(p.rangeCenter));
        // Save discrete choices
        if (!p.choices.empty()) {
            auto* choicesElem = paramElem->createNewChildElement("Choices");
            for (const auto& choice : p.choices) {
                auto* c = choicesElem->createNewChildElement("Choice");
                c->setAttribute("label", choice);
            }
        }
        // Save full value table (pipe-separated for compactness)
        if (!p.valueTable.empty()) {
            juce::String tableStr;
            for (size_t j = 0; j < p.valueTable.size(); ++j) {
                if (j > 0)
                    tableStr += "|";
                tableStr += p.valueTable[j];
            }
            paramElem->setAttribute("valueTable", tableStr);
        }
        if (p.isVisible)
            visibleCount++;
    }

    if (root.writeTo(configFile)) {
        DBG("Saved parameter config for " << pluginUniqueId_ << " - " << visibleCount
                                          << " visible params to " << configFile.getFullPathName());
        refreshLiveDevicesForParameterConfig(pluginUniqueId_);
    } else {
        DBG("Failed to save parameter config for " << pluginUniqueId_);
    }
}

void ParameterConfigDialog::loadParameterConfiguration() {
    if (pluginUniqueId_.isEmpty()) {
        DBG("Cannot load parameter config - no plugin unique ID");
        return;
    }

    auto configDir = magda::paths::pluginConfigsDir();

    auto configFile =
        configDir.getChildFile(pluginUniqueId_.replaceCharacters(":/\\,; ", "______") + ".xml");

    if (!configFile.existsAsFile()) {
        return;
    }

    auto xml = juce::parseXML(configFile);
    if (!xml) {
        DBG("Failed to parse config file for " << pluginUniqueId_);
        return;
    }

    // First, mark all as invisible
    for (auto& param : parameters_) {
        param.isVisible = false;
    }

    int loadedCount = 0;

    // New format: Parameters element with full detection data
    if (auto* paramsElem = xml->getChildByName("Parameters")) {
        for (auto* paramElem : paramsElem->getChildIterator()) {
            int index = paramElem->getIntAttribute("index", -1);
            if (index >= 0 && index < static_cast<int>(parameters_.size())) {
                auto& p = parameters_[static_cast<size_t>(index)];
                p.isVisible = paramElem->getBoolAttribute("visible", false);
                p.inMiniMixer = paramElem->getBoolAttribute("mini", false);
                if (paramElem->hasAttribute("unit"))
                    p.unit = paramElem->getStringAttribute("unit");
                if (paramElem->hasAttribute("scale"))
                    p.scale = xmlStringToScale(paramElem->getStringAttribute("scale"));
                if (paramElem->hasAttribute("min"))
                    p.rangeMin = static_cast<float>(paramElem->getDoubleAttribute("min"));
                if (paramElem->hasAttribute("max"))
                    p.rangeMax = static_cast<float>(paramElem->getDoubleAttribute("max"));
                if (paramElem->hasAttribute("center"))
                    p.rangeCenter = static_cast<float>(paramElem->getDoubleAttribute("center"));
                // Load discrete choices
                if (auto* choicesElem = paramElem->getChildByName("Choices")) {
                    p.choices.clear();
                    for (auto* c : choicesElem->getChildIterator()) {
                        p.choices.push_back(c->getStringAttribute("label"));
                    }
                }
                // Load value table
                if (paramElem->hasAttribute("valueTable")) {
                    auto tableStr = paramElem->getStringAttribute("valueTable");
                    p.valueTable.clear();
                    auto tokens = juce::StringArray::fromTokens(tableStr, "|", "");
                    for (const auto& t : tokens)
                        p.valueTable.push_back(t);
                }
                if (p.isVisible)
                    loadedCount++;
            }
        }
    }
    // Legacy format: VisibleParameters only
    else if (auto* visibleParams = xml->getChildByName("VisibleParameters")) {
        for (auto* paramElem : visibleParams->getChildIterator()) {
            int index = paramElem->getIntAttribute("index", -1);
            if (index >= 0 && index < static_cast<int>(parameters_.size())) {
                parameters_[static_cast<size_t>(index)].isVisible = true;
                loadedCount++;
            }
        }
    }

    DBG("Loaded parameter config for " << pluginUniqueId_ << " - " << loadedCount
                                       << " visible params");
}

bool ParameterConfigDialog::applyConfigToDevice(const juce::String& uniqueId,
                                                magda::DeviceInfo& device) {
    if (uniqueId.isEmpty()) {
        DBG("Cannot apply config - no plugin unique ID");
        return false;
    }

    auto configDir = magda::paths::pluginConfigsDir();

    auto configFile =
        configDir.getChildFile(uniqueId.replaceCharacters(":/\\,; ", "______") + ".xml");

    if (!configFile.existsAsFile()) {
        return false;
    }

    auto xml = juce::parseXML(configFile);
    if (!xml) {
        DBG("Failed to parse config file for " << uniqueId);
        return false;
    }

    // Load parameters from new format or legacy format
    device.visibleParameters.clear();
    device.miniMixerParameters.clear();

    // device.parameters now holds only the plugin's own params — TE's slot
    // dry/wet live in device.wrapperParameters — so the XML's stored index
    // maps 1:1 to the device array. (Configs saved before the wrapper-param
    // split assumed indices 0/1 were dry/wet; those will resolve to the
    // wrong slots once and need to be re-saved.)
    if (auto* paramsElem = xml->getChildByName("Parameters")) {
        for (auto* paramElem : paramsElem->getChildIterator()) {
            int deviceIndex = paramElem->getIntAttribute("index", -1);
            if (deviceIndex < 0)
                continue;

            auto xmlName = paramElem->getStringAttribute("name");
            bool visible = paramElem->getBoolAttribute("visible", false);
            bool mini = paramElem->getBoolAttribute("mini", false);

            if (visible && deviceIndex < static_cast<int>(device.parameters.size())) {
                device.visibleParameters.push_back(deviceIndex);
            }
            if (mini && deviceIndex < static_cast<int>(device.parameters.size())) {
                device.miniMixerParameters.push_back(deviceIndex);
            }

            // Apply detection data to device parameters
            if (deviceIndex < static_cast<int>(device.parameters.size())) {
                auto& p = device.parameters[static_cast<size_t>(deviceIndex)];
                if (paramElem->hasAttribute("unit"))
                    p.unit = paramElem->getStringAttribute("unit");
                if (paramElem->hasAttribute("scale"))
                    p.scale = xmlStringToScale(paramElem->getStringAttribute("scale"));
                if (paramElem->hasAttribute("min"))
                    p.minValue = static_cast<float>(paramElem->getDoubleAttribute("min"));
                if (paramElem->hasAttribute("max"))
                    p.maxValue = static_cast<float>(paramElem->getDoubleAttribute("max"));
                // Load discrete choices
                if (auto* choicesElem = paramElem->getChildByName("Choices")) {
                    p.choices.clear();
                    for (auto* c : choicesElem->getChildIterator()) {
                        p.choices.push_back(c->getStringAttribute("label"));
                    }
                }
                // Load value table
                if (paramElem->hasAttribute("valueTable")) {
                    auto tableStr = paramElem->getStringAttribute("valueTable");
                    p.valueTable.clear();
                    auto tokens = juce::StringArray::fromTokens(tableStr, "|", "");
                    for (const auto& t : tokens)
                        p.valueTable.push_back(t);
                }
            }
        }
    } else if (auto* visibleParams = xml->getChildByName("VisibleParameters")) {
        for (auto* paramElem : visibleParams->getChildIterator()) {
            int deviceIndex = paramElem->getIntAttribute("index", -1);
            if (deviceIndex >= 0 && deviceIndex < static_cast<int>(device.parameters.size())) {
                device.visibleParameters.push_back(deviceIndex);
            }
        }
    }

    return true;
}

#ifdef MAGDA_ENABLE_TEST_HOOKS
void ParameterConfigDialog::refreshLiveDevicesForParameterConfigForTest(
    const juce::String& uniqueId) {
    refreshLiveDevicesForParameterConfig(uniqueId);
}
#endif

void ParameterConfigDialog::rebuildFilteredList() {
    filteredIndices_.clear();
    for (size_t i = 0; i < parameters_.size(); ++i) {
        filteredIndices_.push_back(static_cast<int>(i));
    }
}

void ParameterConfigDialog::filterParameters(const juce::String& searchText) {
    currentSearchText_ = searchText;
    filteredIndices_.clear();

    if (searchText.isEmpty()) {
        // No filter - show all
        rebuildFilteredList();
    } else {
        // Filter by search text
        juce::String lowerSearch = searchText.toLowerCase();
        for (size_t i = 0; i < parameters_.size(); ++i) {
            if (parameters_[i].name.toLowerCase().contains(lowerSearch)) {
                filteredIndices_.push_back(static_cast<int>(i));
            }
        }
    }

    table_.updateContent();
}

int ParameterConfigDialog::getParamIndexForRow(int row) const {
    if (row >= 0 && row < static_cast<int>(filteredIndices_.size())) {
        return filteredIndices_[static_cast<size_t>(row)];
    }
    return -1;
}

}  // namespace magda::daw::ui
