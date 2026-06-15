#include "PreferencesDialog.hpp"

#include <juce_gui_extra/juce_gui_extra.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <vector>

#include "../../media_db/MediaDatabase.hpp"
#include "../../media_db/MediaDbContext.hpp"
#include "../../media_db/MediaDbQuery.hpp"
#include "../../media_db/PresetDbIndexer.hpp"
#include "../../project/ProjectManager.hpp"
#include "../components/common/TextSlider.hpp"
#include "../state/TimelineController.hpp"
#include "../state/TimelineEvents.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/DialogLookAndFeel.hpp"
#include "../themes/FontManager.hpp"
#include "../windows/MainWindow.hpp"
#include "core/AppPaths.hpp"
#include "core/Config.hpp"
#include "core/GestureRouter.hpp"
#include "core/StringTable.hpp"
#include "core/UIScale.hpp"

// ---------------------------------------------------------------------------
// Setup helpers — internal linkage, shared by all page classes
// ---------------------------------------------------------------------------
namespace {

void setupTextSlider(juce::Component& owner, magda::daw::ui::TextSlider& slider, juce::Label& label,
                     const juce::String& labelText, double min, double max, double interval,
                     int decimals = 0, const juce::String& suffix = {}) {
    label.setText(labelText, juce::dontSendNotification);
    label.setFont(magda::FontManager::getInstance().getUIFont(12.0f));
    label.setColour(juce::Label::textColourId,
                    magda::DarkTheme::getColour(magda::DarkTheme::TEXT_PRIMARY));
    label.setJustificationType(juce::Justification::centredLeft);
    owner.addAndMakeVisible(label);

    slider.setRange(min, max, interval);
    slider.setOrientation(magda::daw::ui::TextSlider::Orientation::Horizontal);
    slider.setShowFillIndicator(false);
    slider.setValueFormatter(
        [decimals, suffix](double value) { return juce::String(value, decimals) + suffix; });
    slider.setValueParser([suffix](const juce::String& text) {
        auto trimmed = text.trim();
        if (suffix.isNotEmpty() && trimmed.endsWithIgnoreCase(suffix))
            trimmed = trimmed.dropLastCharacters(suffix.length()).trim();
        return trimmed.getDoubleValue();
    });
    owner.addAndMakeVisible(slider);
}

void setupToggle(juce::Component& owner, juce::ToggleButton& toggle, const juce::String& text) {
    toggle.setButtonText(text);
    toggle.setColour(juce::ToggleButton::textColourId,
                     magda::DarkTheme::getColour(magda::DarkTheme::TEXT_PRIMARY));
    toggle.setColour(juce::ToggleButton::tickColourId,
                     magda::DarkTheme::getColour(magda::DarkTheme::ACCENT_BLUE));
    toggle.setColour(juce::ToggleButton::tickDisabledColourId,
                     magda::DarkTheme::getColour(magda::DarkTheme::TEXT_DIM));
    owner.addAndMakeVisible(toggle);
}

void setupSectionHeader(juce::Component& owner, juce::Label& header, const juce::String& text) {
    header.setText(text, juce::dontSendNotification);
    header.setColour(juce::Label::textColourId,
                     magda::DarkTheme::getColour(magda::DarkTheme::TEXT_SECONDARY));
    header.setFont(magda::FontManager::getInstance().getUIFontBold(14.0f));
    header.setJustificationType(juce::Justification::centredLeft);
    owner.addAndMakeVisible(header);
}

void setupPathRowLabel(juce::Component& owner, juce::Label& label, const juce::String& text,
                       float size = 12.0f) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(magda::FontManager::getInstance().getUIFont(size));
    label.setColour(juce::Label::textColourId,
                    magda::DarkTheme::getColour(magda::DarkTheme::TEXT_PRIMARY));
    label.setJustificationType(juce::Justification::centredLeft);
    owner.addAndMakeVisible(label);
}

void setupPathTextEditor(juce::Component& owner, juce::TextEditor& editor) {
    editor.setReadOnly(true);
    editor.setFont(magda::FontManager::getInstance().getUIFont(12.0f));
    editor.setColour(juce::TextEditor::backgroundColourId,
                     magda::DarkTheme::getColour(magda::DarkTheme::SURFACE));
    editor.setColour(juce::TextEditor::textColourId,
                     magda::DarkTheme::getColour(magda::DarkTheme::TEXT_PRIMARY));
    editor.setColour(juce::TextEditor::outlineColourId,
                     magda::DarkTheme::getColour(magda::DarkTheme::BORDER));
    owner.addAndMakeVisible(editor);
}

juce::Rectangle<int> getPreferencesDialogContentSize() {
    constexpr int preferredW = 760;
    constexpr int preferredH = 620;
    constexpr int minW = 520;
    constexpr int minH = 380;

    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) {
        const int maxW = display->userArea.getWidth() - 48;
        const int maxH = display->userArea.getHeight() - 96;
        return {juce::jmax(minW, juce::jmin(preferredW, maxW)),
                juce::jmax(minH, juce::jmin(preferredH, maxH))};
    }

    return {preferredW, preferredH};
}

}  // namespace

// ---------------------------------------------------------------------------
// Tab page components
// ---------------------------------------------------------------------------
namespace magda {

// ---- General tab: Zoom, Timeline, UI behaviour -----------------------------

class GeneralPage : public juce::Component {
  public:
    GeneralPage() {
        setupSectionHeader(*this, zoomHeader, tr("preferences.section.zoom"));
        setupTextSlider(*this, zoomInSensitivitySlider, zoomInLabel,
                        tr("preferences.slider.zoom_in_sensitivity"), 5.0, 100.0, 1.0);
        setupTextSlider(*this, zoomOutSensitivitySlider, zoomOutLabel,
                        tr("preferences.slider.zoom_out_sensitivity"), 5.0, 100.0, 1.0);
        setupTextSlider(*this, zoomShiftSensitivitySlider, zoomShiftLabel,
                        tr("preferences.slider.zoom_shift_sensitivity"), 1.0, 50.0, 0.5, 1);

        setupSectionHeader(*this, timelineHeader, tr("preferences.section.timeline"));
        setupTextSlider(*this, timelineLengthSlider, timelineLengthLabel,
                        tr("preferences.slider.default_length"), 16.0, 4096.0, 1.0, 0, " bars");
        timelineLengthSlider.setSkewForCentre(256.0);
        setupTextSlider(*this, viewDurationSlider, viewDurationLabel,
                        tr("preferences.slider.default_view"), 4.0, 128.0, 1.0, 0, " bars");

        setupSectionHeader(*this, transportHeader, tr("preferences.section.transport"));
        setupToggle(*this, stopUpdatesPlayheadToggle,
                    tr("preferences.toggle.stop_updates_playhead"));

        setupSectionHeader(*this, autoSaveHeader, tr("preferences.section.autosave"));
        setupToggle(*this, autoSaveToggle, tr("preferences.toggle.enable_autosave"));
        setupTextSlider(*this, autoSaveIntervalSlider, autoSaveIntervalLabel,
                        tr("preferences.slider.interval"), 10.0, 300.0, 10.0, 0, " sec");

        setupSectionHeader(*this, layoutHeader, tr("preferences.section.layout"));
        setupToggle(*this, headersOnRightToggle, tr("preferences.toggle.headers_on_right"));
        setupToggle(*this, autoHideScrollbarsToggle,
                    tr("preferences.toggle.autohide_arrangement_scrollbars"));

        setupSectionHeader(*this, behaviorHeader, tr("preferences.section.behavior"));
        setupToggle(*this, confirmTrackDeleteToggle, tr("preferences.toggle.confirm_track_delete"));
        setupToggle(*this, autoMonitorToggle, tr("preferences.toggle.auto_monitor"));
        setupToggle(*this, openMacrosOnSelectToggle,
                    tr("preferences.toggle.open_macros_on_select"));
        setupToggle(*this, showTooltipsToggle, tr("preferences.toggle.show_tooltips"));
        setupToggle(*this, openPluginWindowOnDropToggle,
                    tr("preferences.toggle.open_plugin_window_on_drop"));

        setupSectionHeader(*this, languageHeader, tr("preferences.language.header"));
        setupComboLabel(languageLabel, tr("preferences.language.label"));
        styleCombo(languageCombo);
        addAndMakeVisible(languageCombo);

        restartHint.setText(tr("preferences.language.restart_required"),
                            juce::dontSendNotification);
        restartHint.setFont(FontManager::getInstance().getUIFont(11.0f));
        restartHint.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_DIM));
        restartHint.setJustificationType(juce::Justification::centredLeft);
        restartHint.setVisible(false);
        addAndMakeVisible(restartHint);

        languageCombo.onChange = [this] {
            int idx = languageCombo.getSelectedId() - 1;
            if (idx >= 0 && idx < static_cast<int>(availableLanguages_.size()))
                restartHint.setVisible(availableLanguages_[idx] != initialLanguage_);
        };

        setupSectionHeader(*this, scaleHeader, tr("preferences.section.scale"));
        setupComboLabel(scaleLabel, tr("preferences.scale.label"));
        styleCombo(scaleCombo);
        scaleCombo.addItem(tr("preferences.scale.auto"), 1);
        scaleCombo.addItem("100%", 2);
        scaleCombo.addItem("125%", 3);
        scaleCombo.addItem("150%", 4);
        scaleCombo.addItem("175%", 5);
        scaleCombo.addItem("200%", 6);
        addAndMakeVisible(scaleCombo);

        setupTextSlider(*this, fontScaleSlider, fontScaleLabel, tr("preferences.font_scale.label"),
                        80.0, 150.0, 5.0, 0, "%");
    }

    int getPreferredHeight(int width) const {
        const int height =
            shouldUseSingleColumnLayout(width)
                ? getSingleColumnPreferredHeight()
                : juce::jmax(getLeftColumnPreferredHeight(), getRightColumnPreferredHeight());

        return juce::jmax(height, width < 520 ? 760 : 0);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(16);
        const int rowH = 32;
        const int sliderH = 24;
        const int headerH = 28;
        const int secGap = 12;

        if (!shouldUseSingleColumnLayout(getWidth())) {
            layoutTwoColumns(bounds, rowH, sliderH, headerH, secGap);
            return;
        }

        layoutSingleColumn(bounds, rowH, sliderH, headerH, secGap);
    }

    void loadSettings(Config& config) {
        zoomInSensitivitySlider.setValue(config.getZoomInSensitivity(), juce::dontSendNotification);
        zoomOutSensitivitySlider.setValue(config.getZoomOutSensitivity(),
                                          juce::dontSendNotification);
        zoomShiftSensitivitySlider.setValue(config.getZoomInSensitivityShift(),
                                            juce::dontSendNotification);
        timelineLengthSlider.setValue(config.getDefaultTimelineLengthBars(),
                                      juce::dontSendNotification);
        viewDurationSlider.setValue(config.getDefaultZoomViewBars(), juce::dontSendNotification);
        stopUpdatesPlayheadToggle.setToggleState(config.getStopUpdatesPlayhead(),
                                                 juce::dontSendNotification);
        autoSaveToggle.setToggleState(config.getAutoSaveEnabled(), juce::dontSendNotification);
        autoSaveIntervalSlider.setValue(config.getAutoSaveIntervalSeconds(),
                                        juce::dontSendNotification);
        headersOnRightToggle.setToggleState(config.getScrollbarOnLeft(),
                                            juce::dontSendNotification);
        autoHideScrollbarsToggle.setToggleState(config.getArrangementScrollbarsAutoHide(),
                                                juce::dontSendNotification);
        confirmTrackDeleteToggle.setToggleState(config.getConfirmTrackDelete(),
                                                juce::dontSendNotification);
        autoMonitorToggle.setToggleState(config.getAutoMonitorSelectedTrack(),
                                         juce::dontSendNotification);
        openMacrosOnSelectToggle.setToggleState(config.getOpenMacrosOnSelect(),
                                                juce::dontSendNotification);
        showTooltipsToggle.setToggleState(config.getShowTooltips(), juce::dontSendNotification);
        openPluginWindowOnDropToggle.setToggleState(config.getOpenPluginWindowOnDrop(),
                                                    juce::dontSendNotification);

        languageCombo.clear(juce::dontSendNotification);
        availableLanguages_.clear();

        auto langDir = magda::StringTable::findLangDirectory();
        if (langDir.isDirectory()) {
            for (const auto& f : langDir.findChildFiles(juce::File::findFiles, false, "*.json"))
                availableLanguages_.push_back(f.getFileNameWithoutExtension());
        }

        if (availableLanguages_.empty())
            availableLanguages_.push_back("en");

        auto currentLang = juce::String(config.getLanguage());
        initialLanguage_ = currentLang;
        int selectedId = 1;
        for (int i = 0; i < static_cast<int>(availableLanguages_.size()); ++i) {
            languageCombo.addItem(availableLanguages_[i], i + 1);
            if (availableLanguages_[i] == currentLang)
                selectedId = i + 1;
        }
        languageCombo.setSelectedId(selectedId, juce::dontSendNotification);
        restartHint.setVisible(false);

        scaleCombo.setSelectedId(scaleIdForValue(config.getUIScale()), juce::dontSendNotification);
        fontScaleSlider.setValue(config.getUIFontScale() * 100.0, juce::dontSendNotification);
    }

    void applySettings(Config& config) {
        config.setZoomInSensitivity(zoomInSensitivitySlider.getValue());
        config.setZoomOutSensitivity(zoomOutSensitivitySlider.getValue());
        config.setZoomInSensitivityShift(zoomShiftSensitivitySlider.getValue());
        config.setZoomOutSensitivityShift(zoomShiftSensitivitySlider.getValue());
        config.setDefaultTimelineLengthBars(static_cast<int>(timelineLengthSlider.getValue()));
        config.setDefaultZoomViewBars(static_cast<int>(viewDurationSlider.getValue()));
        config.setStopUpdatesPlayhead(stopUpdatesPlayheadToggle.getToggleState());
        config.setAutoSaveEnabled(autoSaveToggle.getToggleState());
        config.setAutoSaveIntervalSeconds(static_cast<int>(autoSaveIntervalSlider.getValue()));
        config.setScrollbarOnLeft(headersOnRightToggle.getToggleState());
        config.setArrangementScrollbarsAutoHide(autoHideScrollbarsToggle.getToggleState());
        config.setConfirmTrackDelete(confirmTrackDeleteToggle.getToggleState());
        config.setAutoMonitorSelectedTrack(autoMonitorToggle.getToggleState());
        config.setOpenMacrosOnSelect(openMacrosOnSelectToggle.getToggleState());
        config.setShowTooltips(showTooltipsToggle.getToggleState());
        config.setOpenPluginWindowOnDrop(openPluginWindowOnDropToggle.getToggleState());

        int selIdx = languageCombo.getSelectedId() - 1;
        if (selIdx >= 0 && selIdx < static_cast<int>(availableLanguages_.size())) {
            auto newLang = availableLanguages_[selIdx];
            if (newLang != juce::String(config.getLanguage())) {
                config.setLanguage(newLang.toStdString());
                StringTable::getInstance().loadLanguage(newLang);
            }
        }

        double newScale = scaleValueForId(scaleCombo.getSelectedId());
        if (newScale > 0.0) {
            applyUIScale(newScale);
        } else {
            applyUIScale(dpiOnlyAutoScale(), /*persist=*/false);
            config.setUIScale(0.0);
            config.save();
        }

        config.setUIFontScale(fontScaleSlider.getValue() / 100.0);
    }

  private:
    static constexpr int kTwoColumnMinWidth = 720;

    static bool shouldUseSingleColumnLayout(int width) {
        return width < kTwoColumnMinWidth;
    }

    static int getSingleColumnPreferredHeight() {
        constexpr int padding = 16;
        constexpr int rowH = 32;
        constexpr int headerH = 28;
        constexpr int secGap = 12;

        return padding + headerH + 4 + (rowH * 3) + 8 + secGap + headerH + 4 + (rowH * 2) + 4 +
               secGap + headerH + 4 + rowH + secGap + headerH + 4 + rowH + 4 + rowH + secGap +
               headerH + 4 + rowH + secGap + headerH + 4 + (rowH * 5) + 12 + secGap + headerH + 4 +
               rowH + 18 + secGap + headerH + 4 + (rowH * 2) + 4 + padding;
    }

    static int getLeftColumnPreferredHeight() {
        constexpr int padding = 16;
        constexpr int rowH = 32;
        constexpr int headerH = 28;
        constexpr int secGap = 12;

        return padding + headerH + 4 + (rowH * 3) + 8 + secGap + headerH + 4 + (rowH * 2) + 4 +
               secGap + headerH + 4 + rowH + secGap + headerH + 4 + rowH + 4 + rowH + padding;
    }

    static int getRightColumnPreferredHeight() {
        constexpr int padding = 16;
        constexpr int rowH = 32;
        constexpr int headerH = 28;
        constexpr int secGap = 12;

        return padding + headerH + 4 + rowH + secGap + headerH + 4 + (rowH * 5) + 12 + secGap +
               headerH + 4 + rowH + 18 + secGap + headerH + 4 + (rowH * 2) + 4 + padding;
    }

    void layoutSingleColumn(juce::Rectangle<int> bounds, int rowH, int sliderH, int headerH,
                            int secGap) {
        // Zoom
        zoomHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        layoutTextSliderRow(bounds, zoomInLabel, zoomInSensitivitySlider, rowH, sliderH);
        bounds.removeFromTop(4);
        layoutTextSliderRow(bounds, zoomOutLabel, zoomOutSensitivitySlider, rowH, sliderH);
        bounds.removeFromTop(4);
        layoutTextSliderRow(bounds, zoomShiftLabel, zoomShiftSensitivitySlider, rowH, sliderH);
        bounds.removeFromTop(secGap);

        // Timeline
        timelineHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        layoutTextSliderRow(bounds, timelineLengthLabel, timelineLengthSlider, rowH, sliderH);
        bounds.removeFromTop(4);
        layoutTextSliderRow(bounds, viewDurationLabel, viewDurationSlider, rowH, sliderH);
        bounds.removeFromTop(secGap);

        // Transport
        transportHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        stopUpdatesPlayheadToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(secGap);

        // Auto-Save
        autoSaveHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        autoSaveToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        layoutTextSliderRow(bounds, autoSaveIntervalLabel, autoSaveIntervalSlider, rowH, sliderH);
        bounds.removeFromTop(secGap);

        // Layout
        layoutHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        headersOnRightToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        autoHideScrollbarsToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(secGap);

        // Behaviour
        behaviorHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        confirmTrackDeleteToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        autoMonitorToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        openMacrosOnSelectToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        showTooltipsToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        openPluginWindowOnDropToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(secGap);

        // Language
        languageHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        layoutComboRow(bounds, languageLabel, languageCombo, rowH);
        restartHint.setBounds(bounds.removeFromTop(18));
        bounds.removeFromTop(secGap);

        // UI scale
        scaleHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        layoutComboRow(bounds, scaleLabel, scaleCombo, rowH);
        bounds.removeFromTop(4);
        layoutTextSliderRow(bounds, fontScaleLabel, fontScaleSlider, rowH, sliderH);
    }

    void layoutTwoColumns(juce::Rectangle<int> bounds, int rowH, int sliderH, int headerH,
                          int secGap) {
        constexpr int colGap = 28;

        const int colW = (bounds.getWidth() - colGap) / 2;
        auto left = bounds.removeFromLeft(colW);
        bounds.removeFromLeft(colGap);
        auto right = bounds;

        // Zoom
        zoomHeader.setBounds(left.removeFromTop(headerH));
        left.removeFromTop(4);
        layoutTextSliderRow(left, zoomInLabel, zoomInSensitivitySlider, rowH, sliderH);
        left.removeFromTop(4);
        layoutTextSliderRow(left, zoomOutLabel, zoomOutSensitivitySlider, rowH, sliderH);
        left.removeFromTop(4);
        layoutTextSliderRow(left, zoomShiftLabel, zoomShiftSensitivitySlider, rowH, sliderH);
        left.removeFromTop(secGap);

        // Timeline
        timelineHeader.setBounds(left.removeFromTop(headerH));
        left.removeFromTop(4);
        layoutTextSliderRow(left, timelineLengthLabel, timelineLengthSlider, rowH, sliderH);
        left.removeFromTop(4);
        layoutTextSliderRow(left, viewDurationLabel, viewDurationSlider, rowH, sliderH);
        left.removeFromTop(secGap);

        // Transport
        transportHeader.setBounds(left.removeFromTop(headerH));
        left.removeFromTop(4);
        stopUpdatesPlayheadToggle.setBounds(left.removeFromTop(rowH).reduced(0, 4));
        left.removeFromTop(secGap);

        // Auto-Save
        autoSaveHeader.setBounds(left.removeFromTop(headerH));
        left.removeFromTop(4);
        autoSaveToggle.setBounds(left.removeFromTop(rowH).reduced(0, 4));
        left.removeFromTop(4);
        layoutTextSliderRow(left, autoSaveIntervalLabel, autoSaveIntervalSlider, rowH, sliderH);

        // Layout
        layoutHeader.setBounds(right.removeFromTop(headerH));
        right.removeFromTop(4);
        headersOnRightToggle.setBounds(right.removeFromTop(rowH).reduced(0, 4));
        right.removeFromTop(4);
        autoHideScrollbarsToggle.setBounds(right.removeFromTop(rowH).reduced(0, 4));
        right.removeFromTop(secGap);

        // Behaviour
        behaviorHeader.setBounds(right.removeFromTop(headerH));
        right.removeFromTop(4);
        confirmTrackDeleteToggle.setBounds(right.removeFromTop(rowH).reduced(0, 4));
        right.removeFromTop(4);
        autoMonitorToggle.setBounds(right.removeFromTop(rowH).reduced(0, 4));
        right.removeFromTop(4);
        openMacrosOnSelectToggle.setBounds(right.removeFromTop(rowH).reduced(0, 4));
        right.removeFromTop(4);
        showTooltipsToggle.setBounds(right.removeFromTop(rowH).reduced(0, 4));
        right.removeFromTop(4);
        openPluginWindowOnDropToggle.setBounds(right.removeFromTop(rowH).reduced(0, 4));
        right.removeFromTop(secGap);

        // Language
        languageHeader.setBounds(right.removeFromTop(headerH));
        right.removeFromTop(4);
        layoutComboRow(right, languageLabel, languageCombo, rowH);
        restartHint.setBounds(right.removeFromTop(18));
        right.removeFromTop(secGap);

        // UI scale
        scaleHeader.setBounds(right.removeFromTop(headerH));
        right.removeFromTop(4);
        layoutComboRow(right, scaleLabel, scaleCombo, rowH);
        right.removeFromTop(4);
        layoutTextSliderRow(right, fontScaleLabel, fontScaleSlider, rowH, sliderH);
    }

    void setupComboLabel(juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(FontManager::getInstance().getUIFont(12.0f));
        label.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(label);
    }

    void styleCombo(juce::ComboBox& combo) {
        combo.setColour(juce::ComboBox::backgroundColourId,
                        DarkTheme::getColour(DarkTheme::SURFACE));
        combo.setColour(juce::ComboBox::textColourId,
                        DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        combo.setColour(juce::ComboBox::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
    }

    static void layoutTextSliderRow(juce::Rectangle<int>& bounds, juce::Label& label,
                                    magda::daw::ui::TextSlider& slider, int rowH, int sliderH) {
        constexpr int sliderW = 96;
        constexpr int gap = 12;
        auto row = bounds.removeFromTop(rowH);
        auto sliderArea = row.removeFromRight(sliderW);
        row.removeFromRight(gap);
        label.setBounds(row);
        slider.setBounds(sliderArea.reduced(0, (rowH - sliderH) / 2));
    }

    static void layoutComboRow(juce::Rectangle<int>& bounds, juce::Label& label,
                               juce::ComboBox& combo, int rowH) {
        const int comboW = juce::jlimit(160, 240, bounds.getWidth() / 2);
        constexpr int gap = 12;
        auto row = bounds.removeFromTop(rowH);
        auto comboArea = row.removeFromRight(comboW);
        row.removeFromRight(gap);
        label.setBounds(row);
        combo.setBounds(comboArea.reduced(0, 4));
    }

    static int scaleIdForValue(double v) {
        if (v <= 0.0)
            return 1;
        if (std::abs(v - 1.0) < 0.01)
            return 2;
        if (std::abs(v - 1.25) < 0.01)
            return 3;
        if (std::abs(v - 1.5) < 0.01)
            return 4;
        if (std::abs(v - 1.75) < 0.01)
            return 5;
        if (std::abs(v - 2.0) < 0.01)
            return 6;
        return 1;
    }

    static double scaleValueForId(int id) {
        switch (id) {
            case 2:
                return 1.0;
            case 3:
                return 1.25;
            case 4:
                return 1.5;
            case 5:
                return 1.75;
            case 6:
                return 2.0;
            default:
                return 0.0;
        }
    }

    juce::Label zoomHeader, timelineHeader, transportHeader, autoSaveHeader;
    magda::daw::ui::TextSlider zoomInSensitivitySlider, zoomOutSensitivitySlider,
        zoomShiftSensitivitySlider;
    juce::Label zoomInLabel, zoomOutLabel, zoomShiftLabel;
    magda::daw::ui::TextSlider timelineLengthSlider, viewDurationSlider;
    juce::Label timelineLengthLabel, viewDurationLabel;
    juce::ToggleButton stopUpdatesPlayheadToggle;
    juce::ToggleButton autoSaveToggle;
    magda::daw::ui::TextSlider autoSaveIntervalSlider;
    juce::Label autoSaveIntervalLabel;
    juce::Label layoutHeader, behaviorHeader, languageHeader, scaleHeader;
    juce::ToggleButton headersOnRightToggle;
    juce::ToggleButton autoHideScrollbarsToggle;
    juce::ToggleButton confirmTrackDeleteToggle, autoMonitorToggle, openMacrosOnSelectToggle;
    juce::ToggleButton showTooltipsToggle;
    juce::ToggleButton openPluginWindowOnDropToggle;
    juce::Label languageLabel;
    juce::ComboBox languageCombo;
    juce::Label restartHint;
    std::vector<juce::String> availableLanguages_;
    juce::String initialLanguage_;
    juce::Label scaleLabel;
    juce::ComboBox scaleCombo;
    juce::Label fontScaleLabel;
    magda::daw::ui::TextSlider fontScaleSlider;
};

// ---- Colours tab: Track colour palette ------------------------------------

class ColoursPage : public juce::Component {
  public:
    static constexpr int MAX_PALETTE_SIZE = 16;

    ColoursPage() {
        setupSectionHeader(*this, coloursHeader, tr("preferences.section.track_colour_palette"));

        colourHeaderLabel.setText(tr("preferences.colours.colour"), juce::dontSendNotification);
        colourHeaderLabel.setFont(FontManager::getInstance().getUIFont(11.0f));
        colourHeaderLabel.setColour(juce::Label::textColourId,
                                    DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        addAndMakeVisible(colourHeaderLabel);

        hexHeaderLabel.setText(tr("preferences.colours.hex_rgb"), juce::dontSendNotification);
        hexHeaderLabel.setFont(FontManager::getInstance().getUIFont(11.0f));
        hexHeaderLabel.setColour(juce::Label::textColourId,
                                 DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        addAndMakeVisible(hexHeaderLabel);

        nameHeaderLabel.setText(tr("preferences.colours.name"), juce::dontSendNotification);
        nameHeaderLabel.setFont(FontManager::getInstance().getUIFont(11.0f));
        nameHeaderLabel.setColour(juce::Label::textColourId,
                                  DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        addAndMakeVisible(nameHeaderLabel);

        addColourButton.setButtonText(tr("preferences.button.add_colour"));
        addColourButton.onClick = [this]() {
            addColourRow(0xFF808080, tr("preferences.colours.new_default_name").toStdString());
        };
        addAndMakeVisible(addColourButton);

        // Clip colour mode
        setupSectionHeader(*this, clipColourHeader, tr("preferences.section.clip_colours"));

        clipColourModeLabel.setText(tr("preferences.label.new_clip_colour"),
                                    juce::dontSendNotification);
        clipColourModeLabel.setFont(FontManager::getInstance().getUIFont(12.0f));
        clipColourModeLabel.setColour(juce::Label::textColourId,
                                      DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        clipColourModeLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(clipColourModeLabel);

        clipColourModeCombo.addItem(tr("preferences.option.inherit_from_track"), 1);
        clipColourModeCombo.addItem(tr("preferences.option.cycle_palette"), 2);
        clipColourModeCombo.setColour(juce::ComboBox::backgroundColourId,
                                      DarkTheme::getColour(DarkTheme::SURFACE));
        clipColourModeCombo.setColour(juce::ComboBox::textColourId,
                                      DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        clipColourModeCombo.setColour(juce::ComboBox::outlineColourId,
                                      DarkTheme::getColour(DarkTheme::BORDER));
        addAndMakeVisible(clipColourModeCombo);
    }

    int getPreferredHeight(int) const {
        constexpr int padding = 16;
        constexpr int headerH = 28;
        constexpr int colourRowH = 26;

        return padding + headerH + 4 + 18 + 4 + ((colourRowH + 2) * MAX_PALETTE_SIZE) + 4 + 24 +
               16 + headerH + 4 + 32 + padding;
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(16);
        const int headerH = 28;

        coloursHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);

        // Column headers
        {
            auto headerRow = bounds.removeFromTop(18);
            colourHeaderLabel.setBounds(headerRow.removeFromLeft(28));
            headerRow.removeFromLeft(8);
            hexHeaderLabel.setBounds(headerRow.removeFromLeft(80));
            headerRow.removeFromLeft(8);
            nameHeaderLabel.setBounds(headerRow.removeFromLeft(140));
        }
        bounds.removeFromTop(4);

        // Palette rows
        const int colourRowH = 26;
        for (size_t i = 0; i < colourSwatches_.size(); ++i) {
            auto row = bounds.removeFromTop(colourRowH);
            colourSwatches_[i]->setBounds(row.removeFromLeft(24).reduced(0, 2));
            row.removeFromLeft(12);
            hexEditors_[i]->setBounds(row.removeFromLeft(80).reduced(0, 2));
            row.removeFromLeft(8);
            nameEditors_[i]->setBounds(row.removeFromLeft(140).reduced(0, 2));
            row.removeFromLeft(8);
            deleteButtons_[i]->setBounds(row.removeFromLeft(20).reduced(0, 2));
            bounds.removeFromTop(2);
        }

        // Add button
        if (static_cast<int>(colourSwatches_.size()) < MAX_PALETTE_SIZE) {
            addColourButton.setVisible(true);
            bounds.removeFromTop(4);
            addColourButton.setBounds(bounds.removeFromTop(24).removeFromLeft(100));
        } else {
            addColourButton.setVisible(false);
        }

        // Clip colour mode
        bounds.removeFromTop(16);
        clipColourHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        {
            auto row = bounds.removeFromTop(32);
            clipColourModeLabel.setBounds(row.removeFromLeft(140));
            clipColourModeCombo.setBounds(row.reduced(0, 4));
        }
    }

    void loadSettings(Config& config) {
        clearColourRows();
        const auto& palette = config.getTrackColourPalette();
        for (const auto& entry : palette)
            addColourRow(entry.colour, entry.name);

        clipColourModeCombo.setSelectedId(config.getClipColourMode() + 1,
                                          juce::dontSendNotification);
    }

    void applySettings(Config& config) {
        std::vector<Config::TrackColourEntry> palette;
        for (size_t i = 0; i < hexEditors_.size(); ++i) {
            Config::TrackColourEntry entry;
            entry.colour =
                static_cast<uint32_t>(hexEditors_[i]->getText().getHexValue64() | 0xFF000000ULL);
            entry.name = nameEditors_[i]->getText().toStdString();
            if (entry.name.empty())
                entry.name = tr("preferences.colours.default_name_prefix").toStdString() + " " +
                             std::to_string(i + 1);
            palette.push_back(entry);
        }
        config.setTrackColourPalette(palette);
        config.setClipColourMode(clipColourModeCombo.getSelectedId() - 1);
    }

  private:
    void addColourRow(uint32_t colour, const std::string& name) {
        if (static_cast<int>(colourSwatches_.size()) >= MAX_PALETTE_SIZE)
            return;

        auto idx = colourSwatches_.size();

        // Swatch (colour preview — painted in our paint() override)
        auto swatch = std::make_unique<juce::Component>();
        swatch->setPaintingIsUnclipped(true);
        addAndMakeVisible(*swatch);
        colourSwatches_.push_back(std::move(swatch));
        swatchColours_.push_back(juce::Colour(colour));

        // Hex editor (RGB only, no alpha — we force 0xFF)
        auto hex = std::make_unique<juce::TextEditor>();
        hex->setFont(FontManager::getInstance().getUIFont(12.0f));
        hex->setColour(juce::TextEditor::backgroundColourId,
                       DarkTheme::getColour(DarkTheme::SURFACE));
        hex->setColour(juce::TextEditor::textColourId,
                       DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        hex->setColour(juce::TextEditor::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
        hex->setInputRestrictions(6, "0123456789ABCDEFabcdef");
        hex->setText(juce::String::toHexString(static_cast<int>(colour & 0x00FFFFFF))
                         .paddedLeft('0', 6)
                         .toUpperCase(),
                     juce::dontSendNotification);
        hex->onTextChange = [this, idx]() { updateSwatchColour(idx); };
        addAndMakeVisible(*hex);
        hexEditors_.push_back(std::move(hex));

        // Name editor
        auto nameEd = std::make_unique<juce::TextEditor>();
        nameEd->setFont(FontManager::getInstance().getUIFont(12.0f));
        nameEd->setColour(juce::TextEditor::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
        nameEd->setColour(juce::TextEditor::textColourId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        nameEd->setColour(juce::TextEditor::outlineColourId,
                          DarkTheme::getColour(DarkTheme::BORDER));
        nameEd->setText(juce::String(name), juce::dontSendNotification);
        addAndMakeVisible(*nameEd);
        nameEditors_.push_back(std::move(nameEd));

        // Delete button
        auto del = std::make_unique<juce::TextButton>("x");
        del->onClick = [this, idx]() { removeColourRow(idx); };
        addAndMakeVisible(*del);
        deleteButtons_.push_back(std::move(del));

        resized();
        repaint();
    }

    void removeColourRow(size_t idx) {
        if (idx >= colourSwatches_.size())
            return;
        removeChildComponent(colourSwatches_[idx].get());
        removeChildComponent(hexEditors_[idx].get());
        removeChildComponent(nameEditors_[idx].get());
        removeChildComponent(deleteButtons_[idx].get());

        colourSwatches_.erase(colourSwatches_.begin() + static_cast<ptrdiff_t>(idx));
        swatchColours_.erase(swatchColours_.begin() + static_cast<ptrdiff_t>(idx));
        hexEditors_.erase(hexEditors_.begin() + static_cast<ptrdiff_t>(idx));
        nameEditors_.erase(nameEditors_.begin() + static_cast<ptrdiff_t>(idx));
        deleteButtons_.erase(deleteButtons_.begin() + static_cast<ptrdiff_t>(idx));

        // Rebind callbacks with correct indices
        for (size_t i = 0; i < deleteButtons_.size(); ++i)
            deleteButtons_[i]->onClick = [this, i]() { removeColourRow(i); };
        for (size_t i = 0; i < hexEditors_.size(); ++i)
            hexEditors_[i]->onTextChange = [this, i]() { updateSwatchColour(i); };

        resized();
        repaint();
    }

    void clearColourRows() {
        for (auto& s : colourSwatches_)
            removeChildComponent(s.get());
        for (auto& h : hexEditors_)
            removeChildComponent(h.get());
        for (auto& n : nameEditors_)
            removeChildComponent(n.get());
        for (auto& d : deleteButtons_)
            removeChildComponent(d.get());
        colourSwatches_.clear();
        swatchColours_.clear();
        hexEditors_.clear();
        nameEditors_.clear();
        deleteButtons_.clear();
    }

    void updateSwatchColour(size_t idx) {
        if (idx >= hexEditors_.size())
            return;
        auto hexVal = hexEditors_[idx]->getText().getHexValue64();
        swatchColours_[idx] = juce::Colour(static_cast<uint32_t>(hexVal | 0xFF000000ULL));
        repaint();
    }

    void paint(juce::Graphics& g) override {
        for (size_t i = 0; i < colourSwatches_.size(); ++i) {
            auto area = colourSwatches_[i]->getBounds().toFloat().reduced(1.0f);
            g.setColour(swatchColours_[i]);
            g.fillRoundedRectangle(area, 3.0f);
            g.setColour(swatchColours_[i].brighter(0.3f));
            g.drawRoundedRectangle(area, 3.0f, 1.0f);
        }
    }

    juce::Label coloursHeader;
    juce::Label colourHeaderLabel, hexHeaderLabel, nameHeaderLabel;
    std::vector<std::unique_ptr<juce::Component>> colourSwatches_;
    std::vector<juce::Colour> swatchColours_;
    std::vector<std::unique_ptr<juce::TextEditor>> hexEditors_;
    std::vector<std::unique_ptr<juce::TextEditor>> nameEditors_;
    std::vector<std::unique_ptr<juce::TextButton>> deleteButtons_;
    juce::TextButton addColourButton;

    // Clip colour mode
    juce::Label clipColourHeader;
    juce::Label clipColourModeLabel;
    juce::ComboBox clipColourModeCombo;
};

// ---- Rendering tab --------------------------------------------------------

class RenderingPage : public juce::Component {
  public:
    RenderingPage() {
        // --- Output Folder ---
        setupSectionHeader(*this, renderHeader, tr("preferences.section.output"));

        renderFolderLabel.setText(tr("preferences.label.render_output_folder"),
                                  juce::dontSendNotification);
        renderFolderLabel.setFont(FontManager::getInstance().getUIFont(12.0f));
        renderFolderLabel.setColour(juce::Label::textColourId,
                                    DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        renderFolderLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(renderFolderLabel);

        renderFolderValue.setText(tr("preferences.label.default_beside_source"),
                                  juce::dontSendNotification);
        renderFolderValue.setFont(FontManager::getInstance().getUIFont(12.0f));
        renderFolderValue.setColour(juce::Label::textColourId,
                                    DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        renderFolderValue.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(renderFolderValue);

        renderFolderBrowseButton.setButtonText(tr("preferences.button.browse"));
        renderFolderBrowseButton.onClick = [this]() {
            fileChooser_ = std::make_unique<juce::FileChooser>(
                tr("preferences.dialog.select_render_output_folder"));
            fileChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                          juce::FileBrowserComponent::canSelectDirectories,
                                      [this](const juce::FileChooser& fc) {
                                          auto result = fc.getResult();
                                          if (result.exists()) {
                                              renderFolderPath_ =
                                                  result.getFullPathName().toStdString();
                                              renderFolderValue.setText(result.getFullPathName(),
                                                                        juce::dontSendNotification);
                                          }
                                      });
        };
        addAndMakeVisible(renderFolderBrowseButton);

        renderFolderClearButton.setButtonText(tr("preferences.button.clear"));
        renderFolderClearButton.onClick = [this]() {
            renderFolderPath_.clear();
            renderFolderValue.setText(tr("preferences.label.default_beside_source"),
                                      juce::dontSendNotification);
        };
        addAndMakeVisible(renderFolderClearButton);

        // --- Format ---
        setupSectionHeader(*this, formatHeader, tr("preferences.section.format"));

        setupComboLabel(sampleRateLabel, tr("preferences.label.sample_rate"));
        sampleRateCombo.addItem("44100 Hz", 1);
        sampleRateCombo.addItem("48000 Hz", 2);
        sampleRateCombo.addItem("96000 Hz", 3);
        sampleRateCombo.addItem("192000 Hz", 4);
        styleCombo(sampleRateCombo);
        addAndMakeVisible(sampleRateCombo);

        setupComboLabel(bitDepthLabel, tr("preferences.label.export_bit_depth"));
        bitDepthCombo.addItem(tr("preferences.option.bit_depth_16"), 1);
        bitDepthCombo.addItem(tr("preferences.option.bit_depth_24"), 2);
        bitDepthCombo.addItem(tr("preferences.option.bit_depth_32_float"), 3);
        styleCombo(bitDepthCombo);
        addAndMakeVisible(bitDepthCombo);

        setupComboLabel(bounceBitDepthLabel, tr("preferences.label.bounce_bit_depth"));
        bounceBitDepthCombo.addItem(tr("preferences.option.bit_depth_16"), 1);
        bounceBitDepthCombo.addItem(tr("preferences.option.bit_depth_24"), 2);
        bounceBitDepthCombo.addItem(tr("preferences.option.bit_depth_32_float"), 3);
        styleCombo(bounceBitDepthCombo);
        addAndMakeVisible(bounceBitDepthCombo);

        // --- File Naming ---
        setupSectionHeader(*this, namingHeader, tr("preferences.section.file_naming"));

        setupComboLabel(patternLabel, tr("preferences.label.export_pattern"));
        patternEditor.setFont(FontManager::getInstance().getUIFont(12.0f));
        patternEditor.setColour(juce::TextEditor::backgroundColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
        patternEditor.setColour(juce::TextEditor::textColourId,
                                DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        patternEditor.setColour(juce::TextEditor::outlineColourId,
                                DarkTheme::getColour(DarkTheme::BORDER));
        addAndMakeVisible(patternEditor);

        setupComboLabel(bouncePatternLabel, tr("preferences.label.bounce_pattern"));
        bouncePatternEditor.setFont(FontManager::getInstance().getUIFont(12.0f));
        bouncePatternEditor.setColour(juce::TextEditor::backgroundColourId,
                                      DarkTheme::getColour(DarkTheme::SURFACE));
        bouncePatternEditor.setColour(juce::TextEditor::textColourId,
                                      DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        bouncePatternEditor.setColour(juce::TextEditor::outlineColourId,
                                      DarkTheme::getColour(DarkTheme::BORDER));
        addAndMakeVisible(bouncePatternEditor);

        patternHint.setText(tr("preferences.label.pattern_tokens_hint"),
                            juce::dontSendNotification);
        patternHint.setFont(FontManager::getInstance().getUIFont(10.0f));
        patternHint.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_DIM));
        patternHint.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(patternHint);
    }

    int getPreferredHeight(int) const {
        constexpr int padding = 16;
        constexpr int rowH = 32;
        constexpr int headerH = 28;
        constexpr int secGap = 12;

        return padding + headerH + 4 + rowH + 4 + rowH + secGap + headerH + 4 + (rowH * 3) + 8 +
               secGap + headerH + 4 + rowH + 4 + rowH + 2 + 18 + padding;
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(16);
        const int rowH = 32;
        const int headerH = 28;
        const int labelW = 140;
        const int secGap = 12;

        // Output folder
        renderHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        renderFolderLabel.setBounds(bounds.removeFromTop(rowH));
        bounds.removeFromTop(4);
        {
            auto row = bounds.removeFromTop(rowH);
            auto buttonsArea = row.removeFromRight(140);
            renderFolderValue.setBounds(row);
            renderFolderClearButton.setBounds(buttonsArea.removeFromRight(60).reduced(0, 2));
            buttonsArea.removeFromRight(4);
            renderFolderBrowseButton.setBounds(buttonsArea.reduced(0, 2));
        }
        bounds.removeFromTop(secGap);

        // Format
        formatHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        layoutComboRow(bounds, sampleRateLabel, sampleRateCombo, rowH, labelW);
        bounds.removeFromTop(4);
        layoutComboRow(bounds, bitDepthLabel, bitDepthCombo, rowH, labelW);
        bounds.removeFromTop(4);
        layoutComboRow(bounds, bounceBitDepthLabel, bounceBitDepthCombo, rowH, labelW);
        bounds.removeFromTop(secGap);

        // File naming
        namingHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        {
            auto row = bounds.removeFromTop(rowH);
            patternLabel.setBounds(row.removeFromLeft(labelW));
            patternEditor.setBounds(row.reduced(0, 4));
        }
        bounds.removeFromTop(4);
        {
            auto row = bounds.removeFromTop(rowH);
            bouncePatternLabel.setBounds(row.removeFromLeft(labelW));
            bouncePatternEditor.setBounds(row.reduced(0, 4));
        }
        bounds.removeFromTop(2);
        patternHint.setBounds(bounds.removeFromTop(18).withTrimmedLeft(labelW));
    }

    void loadSettings(Config& config) {
        // Folder
        renderFolderPath_ = config.getRenderFolder();
        if (renderFolderPath_.empty()) {
            renderFolderValue.setText(tr("preferences.label.default_beside_source"),
                                      juce::dontSendNotification);
        } else {
            renderFolderValue.setText(juce::String(renderFolderPath_), juce::dontSendNotification);
        }

        // Sample rate
        double sr = config.getRenderSampleRate();
        if (sr >= 192000.0)
            sampleRateCombo.setSelectedId(4, juce::dontSendNotification);
        else if (sr >= 96000.0)
            sampleRateCombo.setSelectedId(3, juce::dontSendNotification);
        else if (sr >= 48000.0)
            sampleRateCombo.setSelectedId(2, juce::dontSendNotification);
        else
            sampleRateCombo.setSelectedId(1, juce::dontSendNotification);

        // Bit depth
        int bd = config.getRenderBitDepth();
        if (bd >= 32)
            bitDepthCombo.setSelectedId(3, juce::dontSendNotification);
        else if (bd >= 24)
            bitDepthCombo.setSelectedId(2, juce::dontSendNotification);
        else
            bitDepthCombo.setSelectedId(1, juce::dontSendNotification);

        // Bounce bit depth
        int bbd = config.getBounceBitDepth();
        if (bbd >= 32)
            bounceBitDepthCombo.setSelectedId(3, juce::dontSendNotification);
        else if (bbd >= 24)
            bounceBitDepthCombo.setSelectedId(2, juce::dontSendNotification);
        else
            bounceBitDepthCombo.setSelectedId(1, juce::dontSendNotification);

        // File patterns
        patternEditor.setText(juce::String(config.getRenderFilePattern()),
                              juce::dontSendNotification);
        bouncePatternEditor.setText(juce::String(config.getBounceFilePattern()),
                                    juce::dontSendNotification);
    }

    void applySettings(Config& config) {
        config.setRenderFolder(renderFolderPath_);

        static constexpr double sampleRates[] = {44100.0, 48000.0, 96000.0, 192000.0};
        int srIdx = sampleRateCombo.getSelectedId() - 1;
        if (srIdx >= 0 && srIdx < 4)
            config.setRenderSampleRate(sampleRates[srIdx]);

        static constexpr int bitDepths[] = {16, 24, 32};
        int bdIdx = bitDepthCombo.getSelectedId() - 1;
        if (bdIdx >= 0 && bdIdx < 3)
            config.setRenderBitDepth(bitDepths[bdIdx]);

        auto pattern = patternEditor.getText().toStdString();
        if (pattern.empty())
            pattern = "<project-name>_<date-time>";
        config.setRenderFilePattern(pattern);

        auto bouncePattern = bouncePatternEditor.getText().toStdString();
        if (bouncePattern.empty())
            bouncePattern = "<clip-name>_<date-time>";
        config.setBounceFilePattern(bouncePattern);

        int bbdIdx = bounceBitDepthCombo.getSelectedId() - 1;
        if (bbdIdx >= 0 && bbdIdx < 3)
            config.setBounceBitDepth(bitDepths[bbdIdx]);
    }

  private:
    void setupComboLabel(juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(FontManager::getInstance().getUIFont(12.0f));
        label.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(label);
    }

    void styleCombo(juce::ComboBox& combo) {
        combo.setColour(juce::ComboBox::backgroundColourId,
                        DarkTheme::getColour(DarkTheme::SURFACE));
        combo.setColour(juce::ComboBox::textColourId,
                        DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        combo.setColour(juce::ComboBox::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
    }

    static void layoutComboRow(juce::Rectangle<int>& bounds, juce::Label& label,
                               juce::ComboBox& combo, int rowH, int labelW) {
        auto row = bounds.removeFromTop(rowH);
        label.setBounds(row.removeFromLeft(labelW));
        combo.setBounds(row.reduced(0, 4));
    }

    juce::Label renderHeader, formatHeader, namingHeader;
    juce::Label renderFolderLabel;
    juce::Label renderFolderValue;
    juce::TextButton renderFolderBrowseButton;
    juce::TextButton renderFolderClearButton;
    std::string renderFolderPath_;

    juce::Label sampleRateLabel, bitDepthLabel, bounceBitDepthLabel;
    juce::ComboBox sampleRateCombo, bitDepthCombo, bounceBitDepthCombo;

    juce::Label patternLabel, bouncePatternLabel, patternHint;
    juce::TextEditor patternEditor, bouncePatternEditor;

    std::unique_ptr<juce::FileChooser> fileChooser_;
};

// ---- Paths tab (configurable user-data locations) -------------------------
//
// Two pickers: Data Folder (logs, config, scripts, plugin caches) and
// Presets Folder (Chains, Racks, Devices). Render Folder lives on the
// Rendering tab — kept there to avoid duplication.
//
// UX: when the user picks a new folder via Browse, immediately surface a
// migration prompt (Copy / Don't copy / Cancel). The user's answer is
// remembered as `pendingCopyData_` / `pendingCopyPresets_`. Cancel reverts
// the pick entirely. When the user clicks OK / Apply on the dialog,
// PreferencesDialog::applySettings persists the new path(s) to Config,
// runs any pending copy, and (for the data folder) quits so the app
// restarts at the new path.

class PathsPage : public juce::Component {
  public:
    PathsPage() {
        // --- Data Folder ---
        setupSectionHeader(*this, dataHeader_, tr("preferences.paths.section.data"));

        dataLabel_.setText(tr("preferences.paths.label.folder"), juce::dontSendNotification);
        dataLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
        dataLabel_.setColour(juce::Label::textColourId,
                             DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        dataLabel_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(dataLabel_);

        dataValue_.setFont(FontManager::getInstance().getUIFont(12.0f));
        dataValue_.setColour(juce::Label::textColourId,
                             DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        dataValue_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(dataValue_);

        dataBrowse_.setButtonText(tr("preferences.button.browse"));
        dataBrowse_.onClick = [this]() {
            pickFolder(Kind::Data, tr("preferences.paths.dialog.choose_data"));
        };
        addAndMakeVisible(dataBrowse_);

        dataReveal_.setButtonText(tr(
#if JUCE_MAC
            "preferences.button.reveal_finder"
#elif JUCE_WINDOWS
            "preferences.button.reveal_explorer"
#else
            "preferences.button.reveal_files"
#endif
            ));
        dataReveal_.onClick = [this]() {
            revealConfigured(dataPath_, magda::paths::alwaysOSDefault());
        };
        addAndMakeVisible(dataReveal_);

        dataReset_.setButtonText(tr("preferences.button.reset"));
        dataReset_.onClick = [this]() {
            dataPath_.clear();
            updateDisplay();
        };
        addAndMakeVisible(dataReset_);

        dataNote_.setFont(FontManager::getInstance().getUIFont(11.0f));
        dataNote_.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_DIM));
        dataNote_.setJustificationType(juce::Justification::centredLeft);
        dataNote_.setText(tr("preferences.paths.note.data"), juce::dontSendNotification);
        addAndMakeVisible(dataNote_);

        // --- Presets Folder ---
        setupSectionHeader(*this, presetsHeader_, tr("preferences.paths.section.presets"));

        presetsLabel_.setText(tr("preferences.paths.label.folder"), juce::dontSendNotification);
        presetsLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
        presetsLabel_.setColour(juce::Label::textColourId,
                                DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        presetsLabel_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(presetsLabel_);

        presetsValue_.setFont(FontManager::getInstance().getUIFont(12.0f));
        presetsValue_.setColour(juce::Label::textColourId,
                                DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        presetsValue_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(presetsValue_);

        presetsBrowse_.setButtonText(tr("preferences.button.browse"));
        presetsBrowse_.onClick = [this]() {
            pickFolder(Kind::Presets, tr("preferences.paths.dialog.choose_presets"));
        };
        addAndMakeVisible(presetsBrowse_);

        presetsReveal_.setButtonText(tr(
#if JUCE_MAC
            "preferences.button.reveal_finder"
#elif JUCE_WINDOWS
            "preferences.button.reveal_explorer"
#else
            "preferences.button.reveal_files"
#endif
            ));
        presetsReveal_.onClick = [this]() { revealConfigured(presetsPath_, presetsDefaultDir()); };
        addAndMakeVisible(presetsReveal_);

        presetsReset_.setButtonText(tr("preferences.button.reset"));
        presetsReset_.onClick = [this]() {
            presetsPath_.clear();
            updateDisplay();
        };
        addAndMakeVisible(presetsReset_);

        presetsNote_.setFont(FontManager::getInstance().getUIFont(11.0f));
        presetsNote_.setColour(juce::Label::textColourId,
                               DarkTheme::getColour(DarkTheme::TEXT_DIM));
        presetsNote_.setJustificationType(juce::Justification::centredLeft);
        presetsNote_.setText(tr("preferences.paths.note.presets"), juce::dontSendNotification);
        addAndMakeVisible(presetsNote_);

        // --- Hint about Render Folder ---
        renderHint_.setFont(FontManager::getInstance().getUIFont(11.0f));
        renderHint_.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_DIM));
        renderHint_.setJustificationType(juce::Justification::centredLeft);
        renderHint_.setText(tr("preferences.paths.note.render_hint"), juce::dontSendNotification);
        addAndMakeVisible(renderHint_);

        // --- Media Library ---
        setupSectionHeader(*this, editorHeader_, "External Audio Editor");
        setupPathRowLabel(*this, editorLocationCaption_, "Editor");
        setupPathTextEditor(*this, editorLocationField_);

        editorBrowse_.setButtonText("Browse...");
        editorBrowse_.onClick = [this]() { browseForExternalEditor(); };
        addAndMakeVisible(editorBrowse_);

        editorReset_.setButtonText("Reset");
        editorReset_.onClick = [this]() {
            magda::Config::getInstance().setExternalAudioEditorPath(std::string{});
            magda::Config::getInstance().save();
            refreshMediaLibrarySettings();
        };
        addAndMakeVisible(editorReset_);

        setupSectionHeader(*this, dbHeader_, "Media Database");
        setupPathRowLabel(*this, dbLocationCaption_, "Database location");
        setupPathTextEditor(*this, dbLocationField_);

        dbBrowse_.setButtonText("Browse...");
        dbBrowse_.onClick = [this]() { browseForDbLocation(); };
        addAndMakeVisible(dbBrowse_);

        dbReset_.setButtonText("Reset");
        dbReset_.onClick = [this]() {
            magda::Config::getInstance().setMediaDbDir(std::string{});
            magda::Config::getInstance().save();
            magda::media::MediaDbContext::getInstance().resetForReopen();
            refreshMediaLibrarySettings();
        };
        addAndMakeVisible(dbReset_);

        dbStats_.setFont(magda::FontManager::getInstance().getUIFont(11.0f));
        dbStats_.setColour(juce::Label::textColourId,
                           magda::DarkTheme::getColour(magda::DarkTheme::TEXT_SECONDARY));
        dbStats_.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(dbStats_);

        wipeButton_.setButtonText("Wipe database...");
        wipeButton_.onClick = [this]() { confirmAndWipe(); };
        addAndMakeVisible(wipeButton_);

        indexPresetsButton_.setButtonText("Index presets");
        indexPresetsButton_.onClick = [this]() { runPresetIndex(); };
        addAndMakeVisible(indexPresetsButton_);
    }

    int getPreferredHeight(int) const {
        constexpr int padding = 20;
        constexpr int rowH = 28;
        constexpr int gap = 6;
        constexpr int sectionGap = 18;

        return padding + rowH + gap + rowH + gap + rowH + gap + rowH + sectionGap + rowH + gap +
               rowH + gap + rowH + gap + rowH + sectionGap + rowH + gap + rowH + sectionGap + rowH +
               gap + rowH + gap + rowH + gap + rowH + sectionGap + rowH + padding;
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(20);
        const int rowH = 28;
        const int gap = 6;
        const int sectionGap = 18;
        const int revealW = 130;
        const int browseW = 100;
        const int resetW = 80;
        const int labelW = 120;

        // Data section: header → button row (Reveal + Browse + Reset, left-
        // aligned) → path row (Folder: <value>) → note.
        dataHeader_.setBounds(bounds.removeFromTop(rowH));
        bounds.removeFromTop(gap);
        auto dataButtons = bounds.removeFromTop(rowH);
        dataReveal_.setBounds(dataButtons.removeFromLeft(revealW));
        dataButtons.removeFromLeft(gap);
        dataBrowse_.setBounds(dataButtons.removeFromLeft(browseW));
        dataButtons.removeFromLeft(gap);
        dataReset_.setBounds(dataButtons.removeFromLeft(resetW));
        bounds.removeFromTop(gap);
        auto dataPathRow = bounds.removeFromTop(rowH);
        dataLabel_.setBounds(dataPathRow.removeFromLeft(60));
        dataValue_.setBounds(dataPathRow);
        bounds.removeFromTop(gap);
        dataNote_.setBounds(bounds.removeFromTop(rowH));

        bounds.removeFromTop(sectionGap);

        // Presets section
        presetsHeader_.setBounds(bounds.removeFromTop(rowH));
        bounds.removeFromTop(gap);
        auto presetsButtons = bounds.removeFromTop(rowH);
        presetsReveal_.setBounds(presetsButtons.removeFromLeft(revealW));
        presetsButtons.removeFromLeft(gap);
        presetsBrowse_.setBounds(presetsButtons.removeFromLeft(browseW));
        presetsButtons.removeFromLeft(gap);
        presetsReset_.setBounds(presetsButtons.removeFromLeft(resetW));
        bounds.removeFromTop(gap);
        auto presetsPathRow = bounds.removeFromTop(rowH);
        presetsLabel_.setBounds(presetsPathRow.removeFromLeft(60));
        presetsValue_.setBounds(presetsPathRow);
        bounds.removeFromTop(gap);
        presetsNote_.setBounds(bounds.removeFromTop(rowH));

        bounds.removeFromTop(sectionGap);

        // External editor section
        editorHeader_.setBounds(bounds.removeFromTop(rowH));
        bounds.removeFromTop(gap);
        auto editorRow = bounds.removeFromTop(rowH);
        editorLocationCaption_.setBounds(editorRow.removeFromLeft(labelW));
        editorReset_.setBounds(editorRow.removeFromRight(70).reduced(0, 1));
        editorRow.removeFromRight(4);
        editorBrowse_.setBounds(editorRow.removeFromRight(90).reduced(0, 1));
        editorRow.removeFromRight(4);
        editorLocationField_.setBounds(editorRow.reduced(0, 1));

        bounds.removeFromTop(sectionGap);

        // Media database section
        dbHeader_.setBounds(bounds.removeFromTop(rowH));
        bounds.removeFromTop(gap);
        auto dbLocRow = bounds.removeFromTop(rowH);
        dbLocationCaption_.setBounds(dbLocRow.removeFromLeft(labelW));
        dbReset_.setBounds(dbLocRow.removeFromRight(70).reduced(0, 1));
        dbLocRow.removeFromRight(4);
        dbBrowse_.setBounds(dbLocRow.removeFromRight(90).reduced(0, 1));
        dbLocRow.removeFromRight(4);
        dbLocationField_.setBounds(dbLocRow.reduced(0, 1));
        bounds.removeFromTop(gap);

        dbStats_.setBounds(bounds.removeFromTop(20));
        bounds.removeFromTop(gap);

        auto dbActionRow = bounds.removeFromTop(rowH);
        wipeButton_.setBounds(dbActionRow.removeFromLeft(180));
        dbActionRow.removeFromLeft(gap);
        indexPresetsButton_.setBounds(dbActionRow.removeFromLeft(160));

        bounds.removeFromTop(sectionGap);

        renderHint_.setBounds(bounds.removeFromTop(rowH));
    }

    enum class Kind { Data, Presets };

    void loadSettings(Config& config) {
        dataPath_ = config.getDataDir();
        presetsPath_ = config.getPresetsDir();
        originalDataPath_ = dataPath_;
        originalPresetsPath_ = presetsPath_;
        pendingCopyData_ = false;
        pendingCopyPresets_ = false;
        updateDisplay();
        refreshMediaLibrarySettings();
    }

    void applySettings(Config& /*config*/) {
        // Path commits run from PreferencesDialog::applySettings — it knows
        // the order in which to commit and how to handle the data-folder
        // restart. Migration prompts already happened at Browse time; this
        // page only carries the user's answers forward.
    }

    bool dataDirChanged() const {
        return dataPath_ != originalDataPath_;
    }
    bool presetsDirChanged() const {
        return presetsPath_ != originalPresetsPath_;
    }
    bool shouldCopyData() const {
        return pendingCopyData_;
    }
    bool shouldCopyPresets() const {
        return pendingCopyPresets_;
    }
    std::string getOriginalDataPath() const {
        return originalDataPath_;
    }
    std::string getNewDataPath() const {
        return dataPath_;
    }
    std::string getOriginalPresetsPath() const {
        return originalPresetsPath_;
    }
    std::string getNewPresetsPath() const {
        return presetsPath_;
    }

  private:
    static juce::File presetsDefaultDir() {
        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile("MAGDA")
            .getChildFile("Presets");
    }

    // Reveal the configured folder if it exists on disk, falling back to the
    // platform default. If neither exists yet, create the default first so
    // revealToUser has something to point at.
    static void revealConfigured(const std::string& configured, const juce::File& defaultDir) {
        juce::File target = configured.empty() ? defaultDir : juce::File(juce::String(configured));
        if (!target.isDirectory())
            target = defaultDir;
        if (!target.isDirectory())
            target.createDirectory();
        target.revealToUser();
    }

    void pickFolder(Kind kind, const juce::String& title) {
        const juce::File defaultDir =
            (kind == Kind::Data) ? magda::paths::alwaysOSDefault() : presetsDefaultDir();
        const std::string& target = (kind == Kind::Data) ? dataPath_ : presetsPath_;

        // Only pass `initial` to FileChooser when it actually resolves to a
        // real directory. Passing a non-existent path can make NSOpenPanel
        // on macOS silently drop the dialog.
        juce::File initial = target.empty() ? juce::File() : juce::File(juce::String(target));
        if (!initial.isDirectory())
            initial = defaultDir;
        if (!initial.isDirectory())
            initial = juce::File();

        fileChooser_ = initial == juce::File()
                           ? std::make_unique<juce::FileChooser>(title)
                           : std::make_unique<juce::FileChooser>(title, initial);

        juce::Component::SafePointer<PathsPage> self(this);
        fileChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                      juce::FileBrowserComponent::canSelectDirectories,
                                  [self, kind](const juce::FileChooser& fc) {
                                      auto result = fc.getResult();
                                      if (auto* page = self.getComponent()) {
                                          if (result.exists() && result.isDirectory())
                                              page->onFolderPicked(kind, result);
                                      }
                                  });
    }

    void onFolderPicked(Kind kind, const juce::File& picked) {
        const std::string newPathStr = picked.getFullPathName().toStdString();
        std::string& target = (kind == Kind::Data) ? dataPath_ : presetsPath_;
        const std::string& original =
            (kind == Kind::Data) ? originalDataPath_ : originalPresetsPath_;

        // No-op pick: same as the currently-stored path. Nothing to ask.
        if (newPathStr == target)
            return;

        // If the user has effectively re-selected the original path, just
        // wipe the in-flight change without prompting.
        if (newPathStr == original) {
            target = newPathStr;
            (kind == Kind::Data ? pendingCopyData_ : pendingCopyPresets_) = false;
            updateDisplay();
            return;
        }

        // Resolve from/to for the prompt body. Empty `original` means the
        // user is on the platform default.
        const juce::File defaultDir =
            (kind == Kind::Data) ? magda::paths::alwaysOSDefault() : presetsDefaultDir();
        juce::String oldPath =
            original.empty() ? defaultDir.getFullPathName() : juce::String(original);
        juce::String newPath = picked.getFullPathName();

        const juce::String titleKey = (kind == Kind::Data)
                                          ? "preferences.paths.migration.data_title"
                                          : "preferences.paths.migration.presets_title";
        const juce::String messageKey = (kind == Kind::Data)
                                            ? "preferences.paths.migration.data_message"
                                            : "preferences.paths.migration.presets_message";

        juce::Component::SafePointer<PathsPage> self(this);
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::QuestionIcon)
                .withTitle(tr(titleKey))
                .withMessage(tr(messageKey).replace("{0}", oldPath).replace("{1}", newPath))
                .withButton(tr("preferences.paths.migration.button.copy"))
                .withButton(tr("preferences.paths.migration.button.dont_copy"))
                .withButton(tr("preferences.paths.migration.button.cancel")),
            [self, kind, newPathStr](int result) {
                auto* page = self.getComponent();
                if (page == nullptr)
                    return;
                // JUCE 3-button mapping: 1=first ("Copy"), 2=second
                // ("Don't copy"), 0=third/escape ("Cancel").
                if (result == 0)
                    return;  // Cancel: leave configured path untouched.
                page->setPendingPath(kind, newPathStr, /*copyFiles=*/result == 1);
            });
    }

    void setPendingPath(Kind kind, const std::string& newPath, bool copyFiles) {
        if (kind == Kind::Data) {
            dataPath_ = newPath;
            pendingCopyData_ = copyFiles;
        } else {
            presetsPath_ = newPath;
            pendingCopyPresets_ = copyFiles;
        }
        updateDisplay();
    }

    void updateDisplay() {
        if (dataPath_.empty()) {
            dataValue_.setText("default: " + magda::paths::alwaysOSDefault().getFullPathName(),
                               juce::dontSendNotification);
        } else {
            dataValue_.setText(juce::String(dataPath_), juce::dontSendNotification);
        }

        if (presetsPath_.empty()) {
            auto def = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                           .getChildFile("MAGDA")
                           .getChildFile("Presets");
            presetsValue_.setText("default: " + def.getFullPathName(), juce::dontSendNotification);
        } else {
            presetsValue_.setText(juce::String(presetsPath_), juce::dontSendNotification);
        }
    }

    void refreshMediaLibrarySettings() {
        auto& ctx = magda::media::MediaDbContext::getInstance();

        const auto editorPath =
            juce::String(magda::Config::getInstance().getExternalAudioEditorPath());
        editorLocationField_.setText(editorPath.isEmpty() ? juce::String("Not configured")
                                                          : editorPath,
                                     juce::dontSendNotification);

        dbLocationField_.setText(juce::String(ctx.dbPath().parent_path().string()),
                                 juce::dontSendNotification);

        int files = 0;
        int embeddings = 0;
        if (ctx.ensureInitialized()) {
            magda::media::MediaDbQuery probe(ctx.db(), nullptr, nullptr);
            files = probe.totalFiles();
            embeddings = probe.totalEmbeddings();
        }
        dbStats_.setText(juce::String(files) + " indexed files, " + juce::String(embeddings) +
                             " audio embeddings",
                         juce::dontSendNotification);
        wipeButton_.setEnabled(files > 0);
    }

    void browseForDbLocation() {
        const auto currentDir = juce::File(juce::String(
            magda::media::MediaDbContext::getInstance().dbPath().parent_path().string()));
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Choose a folder for the media database",
            currentDir.exists() ? currentDir
                                : juce::File::getSpecialLocation(juce::File::userHomeDirectory));

        juce::Component::SafePointer<PathsPage> self(this);
        fileChooser_->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [self](const juce::FileChooser& fc) {
                const auto picked = fc.getResult();
                if (picked.isDirectory()) {
                    magda::Config::getInstance().setMediaDbDir(
                        picked.getFullPathName().toStdString());
                    magda::Config::getInstance().save();
                    magda::media::MediaDbContext::getInstance().resetForReopen();
                    if (auto* page = self.getComponent())
                        page->refreshMediaLibrarySettings();
                }
            });
    }

    void browseForExternalEditor() {
        const auto configured =
            juce::String(magda::Config::getInstance().getExternalAudioEditorPath());
        juce::File initial = configured.isNotEmpty()
                                 ? juce::File(configured)
                                 : juce::File::getSpecialLocation(juce::File::userHomeDirectory);
        if (!initial.exists()) {
            initial = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
        }

        fileChooser_ =
            std::make_unique<juce::FileChooser>("Choose an external audio editor", initial);
        auto chooserFlags =
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
#if JUCE_MAC
        chooserFlags |= juce::FileBrowserComponent::canSelectDirectories;
#endif

        juce::Component::SafePointer<PathsPage> self(this);
        fileChooser_->launchAsync(chooserFlags, [self](const juce::FileChooser& fc) {
            const auto picked = fc.getResult();
            if (!picked.exists()) {
                return;
            }
#if JUCE_MAC
            if (!picked.existsAsFile() && !picked.isBundle()) {
                return;
            }
#else
            if (!picked.existsAsFile()) {
                return;
            }
#endif
            magda::Config::getInstance().setExternalAudioEditorPath(
                picked.getFullPathName().toStdString());
            magda::Config::getInstance().save();
            if (auto* page = self.getComponent())
                page->refreshMediaLibrarySettings();
        });
    }

    void runPresetIndex() {
        auto& ctx = magda::media::MediaDbContext::getInstance();
        if (!ctx.ensureInitialized()) {
            return;
        }
        const auto presetsRoot =
            std::filesystem::path(magda::paths::presetsDir().getFullPathName().toStdString());

        indexPresetsButton_.setEnabled(false);
        indexPresetsButton_.setButtonText("Indexing presets...");

        const juce::Component::SafePointer<PathsPage> self(this);
        juce::Thread::launch([self, presetsRoot]() {
            auto& ctx = magda::media::MediaDbContext::getInstance();
            magda::media::PresetDbIndexer indexer(ctx.db());
            const auto stats = indexer.indexAll(presetsRoot);
            ctx.bumpMediaRevision();
            juce::MessageManager::callAsync([self, stats]() {
                if (auto* page = self.getComponent()) {
                    page->indexPresetsButton_.setEnabled(true);
                    page->indexPresetsButton_.setButtonText("Index presets");
                    page->refreshMediaLibrarySettings();
                    juce::Logger::writeToLog(juce::String("[indexPresets] inserted=") +
                                             juce::String(stats.inserted) +
                                             " updated=" + juce::String(stats.updated) +
                                             " skipped=" + juce::String(stats.skipped) +
                                             " failed=" + juce::String(stats.failed));
                }
            });
        });
    }

    void confirmAndWipe() {
        const juce::Component::SafePointer<PathsPage> self(this);
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions{}
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Wipe media database")
                .withMessage("This deletes every indexed file, embedding, and tag from the media "
                             "database.\nYour audio files on disk are untouched.\n\nContinue?")
                .withButton("Wipe")
                .withButton("Cancel"),
            [self](int result) {
                if (auto* page = self.getComponent(); page != nullptr && result == 1) {
                    magda::media::MediaDbContext::getInstance().wipeAll();
                    page->refreshMediaLibrarySettings();
                }
            });
    }

    juce::Label dataHeader_, dataLabel_, dataValue_, dataNote_;
    juce::TextButton dataBrowse_, dataReveal_, dataReset_;
    std::string dataPath_;
    std::string originalDataPath_;
    bool pendingCopyData_ = false;

    juce::Label presetsHeader_, presetsLabel_, presetsValue_, presetsNote_;
    juce::TextButton presetsBrowse_, presetsReveal_, presetsReset_;
    std::string presetsPath_;
    std::string originalPresetsPath_;
    bool pendingCopyPresets_ = false;

    juce::Label renderHint_;

    juce::Label editorHeader_;
    juce::Label editorLocationCaption_;
    juce::TextEditor editorLocationField_;
    juce::TextButton editorBrowse_;
    juce::TextButton editorReset_;

    juce::Label dbHeader_;
    juce::Label dbLocationCaption_;
    juce::TextEditor dbLocationField_;
    juce::TextButton dbBrowse_;
    juce::TextButton dbReset_;
    juce::Label dbStats_;
    juce::TextButton wipeButton_;
    juce::TextButton indexPresetsButton_;

    std::unique_ptr<juce::FileChooser> fileChooser_;
};

// ---- Shortcuts tab ---------------------------------------------------------

class ShortcutsPage : public juce::Component {
  public:
    explicit ShortcutsPage(juce::ApplicationCommandManager* commandManager)
        : tabbedComponent_(juce::TabbedButtonBar::TabsAtTop) {
        auto tabBg = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
        keyboardPage_ = std::make_unique<KeyboardShortcutsPage>(commandManager);
        gesturesPage_ = std::make_unique<GestureBindingsPage>();
        tabbedComponent_.addTab("Keyboard", tabBg, keyboardPage_.get(), false);
        tabbedComponent_.addTab("Gestures", tabBg, gesturesPage_.get(), false);
        tabbedComponent_.setTabBarDepth(32);
        addAndMakeVisible(tabbedComponent_);
    }

    void resized() override {
        tabbedComponent_.setBounds(getLocalBounds());
    }

    void loadSettings(Config& config) {
        keyboardPage_->loadSettings(config);
        gesturesPage_->loadSettings(config);
    }

    void applySettings(Config& config) {
        keyboardPage_->applySettings(config);
        gesturesPage_->applySettings(config);
    }

  private:
    class KeyboardShortcutsPage : public juce::Component {
      public:
        explicit KeyboardShortcutsPage(juce::ApplicationCommandManager* commandManager) {
            setupSectionHeader(*this, header_, tr("preferences.section.keyboard_shortcuts"));

            if (commandManager != nullptr) {
                keyEditor_ = std::make_unique<juce::KeyMappingEditorComponent>(
                    *commandManager->getKeyMappings(), true);
                keyEditor_->setColours(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND),
                                       DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
                addAndMakeVisible(*keyEditor_);
            } else {
                missingLabel_.setText("Keyboard shortcuts are available from the main window.",
                                      juce::dontSendNotification);
                missingLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
                missingLabel_.setColour(juce::Label::textColourId,
                                        DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                missingLabel_.setJustificationType(juce::Justification::centredLeft);
                addAndMakeVisible(missingLabel_);
            }
        }

        void resized() override {
            auto bounds = getLocalBounds().reduced(16);
            header_.setBounds(bounds.removeFromTop(28));
            bounds.removeFromTop(6);

            if (keyEditor_)
                keyEditor_->setBounds(bounds);
            else
                missingLabel_.setBounds(bounds.removeFromTop(28));
        }

        void loadSettings(Config&) {}
        void applySettings(Config&) {}

      private:
        juce::Label header_;
        juce::Label missingLabel_;
        std::unique_ptr<juce::KeyMappingEditorComponent> keyEditor_;
    };

    class GestureBindingsPage : public juce::Component {
      public:
        GestureBindingsPage() {
            setupSectionHeader(*this, header_, "Mouse gestures");

            resetButton_.setButtonText("Reset");
            resetButton_.onClick = [this] {
                auto& router = GestureRouter::getInstance();
                router.resetToDefaults();
                router.saveToConfig();
                capturingRow_ = nullptr;
                loadFromRouter();
            };
            addAndMakeVisible(resetButton_);

            setupHeaderLabel(contextHeader_, "Gesture");
            setupHeaderLabel(axisHeader_, "Area");
            setupHeaderLabel(modifierHeader_, "Input");
            setupHeaderLabel(sensitivityHeader_, "Sensitivity");
            setupHeaderLabel(invertHeader_, "Invert");

            viewport_.setViewedComponent(&content_, false);
            viewport_.setScrollBarsShown(true, false);
            viewport_.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
            content_.setViewportIgnoreDragFlag(true);
            addAndMakeVisible(viewport_);
            setWantsKeyboardFocus(true);
            addMouseListener(this, true);

            for (auto context : contexts()) {
                for (auto axis : {GestureAxis::Vertical, GestureAxis::Horizontal}) {
                    for (auto mods : modifiers())
                        addGestureRow(context,
                                      {GestureInputKind::Wheel, GestureArea::Main, axis, mods});
                }

                for (const auto& row : dragRows()) {
                    if (row.context == context)
                        addGestureRow(row.context, row.input);
                }
            }
        }

        void resized() override {
            auto bounds = getLocalBounds().reduced(16);
            auto headerRow = bounds.removeFromTop(28);
            header_.setBounds(headerRow.removeFromLeft(juce::jmax(220, headerRow.getWidth() - 88)));
            resetButton_.setBounds(headerRow.removeFromRight(72).reduced(0, 2));
            bounds.removeFromTop(6);

            auto columns = bounds.removeFromTop(20);
            layoutColumnHeaders(columns);
            bounds.removeFromTop(4);

            viewport_.setBounds(bounds);
            layoutRows(viewport_.getMaximumVisibleWidth());
        }

        void mouseWheelMove(const juce::MouseEvent& e,
                            const juce::MouseWheelDetails& wheel) override {
            if (!handleCaptureWheel(e, wheel))
                juce::Component::mouseWheelMove(e, wheel);
        }

        bool handleCaptureWheel(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
            if (capturingRow_ == nullptr)
                return false;

            const GestureAxis axis = std::abs(wheel.deltaX) > std::abs(wheel.deltaY)
                                         ? GestureAxis::Horizontal
                                         : GestureAxis::Vertical;
            setRowInput(*capturingRow_, {GestureInputKind::Wheel, GestureArea::Main, axis,
                                         gestureModifierMaskFrom(e.mods)});
            setCapturingRow(nullptr);
            return true;
        }

        void mouseDown(const juce::MouseEvent& e) override {
            if (capturingRow_ == nullptr)
                return;

            captureDragStart_ = e.getPosition();
        }

        void mouseDrag(const juce::MouseEvent& e) override {
            if (capturingRow_ == nullptr)
                return;

            const auto delta = e.getPosition() - captureDragStart_;
            if (std::max(std::abs(delta.x), std::abs(delta.y)) <= 3)
                return;

            const auto area = learnedDragAreaFor(*capturingRow_);
            setRowInput(*capturingRow_, {GestureInputKind::Drag, area, GestureAxis::Vertical,
                                         gestureModifierMaskFrom(e.mods)});
            setCapturingRow(nullptr);
        }

        bool keyPressed(const juce::KeyPress& key) override {
            if (capturingRow_ != nullptr && key == juce::KeyPress::escapeKey) {
                setCapturingRow(nullptr);
                return true;
            }

            return false;
        }

        void loadSettings(Config&) {
            loadFromRouter();
        }

        void applySettings(Config&) {
            auto& router = GestureRouter::getInstance();

            for (const auto& row : rows_) {
                if (!row->contextLabel.isVisible())
                    continue;

                if (sameInput(row->currentInput, row->input))
                    continue;

                if (row->defaultBinding.action == GestureActionType::None) {
                    router.clearBinding(row->context, row->input);
                } else {
                    router.setBinding(row->context, row->input,
                                      {GestureActionType::None, row->defaultBinding.sensitivity,
                                       row->defaultBinding.invert});
                }
            }

            for (const auto& row : rows_) {
                if (!row->contextLabel.isVisible())
                    continue;

                router.setBinding(row->context, row->currentInput,
                                  {row->action, static_cast<float>(row->sensitivity.getValue()),
                                   row->invert.getToggleState()});
            }

            router.saveToConfig();
        }

      private:
        class CaptureViewport : public juce::Viewport {
          public:
            explicit CaptureViewport(GestureBindingsPage& owner) : owner_(owner) {}

            void mouseWheelMove(const juce::MouseEvent& e,
                                const juce::MouseWheelDetails& wheel) override {
                if (owner_.handleCaptureWheel(e, wheel))
                    return;
                juce::Viewport::mouseWheelMove(e, wheel);
            }

          private:
            GestureBindingsPage& owner_;
        };

        struct GestureRow {
            GestureContext context = GestureContext::Unknown;
            GestureInput input;
            GestureInput currentInput;
            GestureActionType action = GestureActionType::None;
            GestureBinding defaultBinding;
            juce::Label contextLabel;
            juce::Label axisLabel;
            juce::Label inputLabel;
            juce::TextButton learnButton;
            magda::daw::ui::TextSlider sensitivity;
            juce::ToggleButton invert;
        };

        struct DragRowSpec {
            GestureContext context;
            GestureInput input;
        };

        static std::array<GestureContext, 4> contexts() {
            return {GestureContext::Arrangement, GestureContext::PianoRoll,
                    GestureContext::DrumGrid, GestureContext::Waveform};
        }

        static std::array<uint8_t, 8> modifiers() {
            return {GestureMod_None,
                    GestureMod_Shift,
                    GestureMod_Command,
                    GestureMod_Alt,
                    static_cast<uint8_t>(GestureMod_Shift | GestureMod_Command),
                    static_cast<uint8_t>(GestureMod_Shift | GestureMod_Alt),
                    static_cast<uint8_t>(GestureMod_Command | GestureMod_Alt),
                    static_cast<uint8_t>(GestureMod_Shift | GestureMod_Command | GestureMod_Alt)};
        }

        static std::array<DragRowSpec, 11> dragRows() {
            return {
                DragRowSpec{GestureContext::Arrangement,
                            {GestureInputKind::Drag, GestureArea::Ruler, GestureAxis::Vertical,
                             GestureMod_None}},
                DragRowSpec{GestureContext::Arrangement,
                            {GestureInputKind::Drag, GestureArea::Ruler, GestureAxis::Vertical,
                             GestureMod_Shift}},
                DragRowSpec{GestureContext::Arrangement,
                            {GestureInputKind::Drag, GestureArea::Ruler, GestureAxis::Vertical,
                             GestureMod_Alt}},
                DragRowSpec{GestureContext::PianoRoll,
                            {GestureInputKind::Drag, GestureArea::Ruler, GestureAxis::Vertical,
                             GestureMod_None}},
                DragRowSpec{GestureContext::PianoRoll,
                            {GestureInputKind::Drag, GestureArea::ZoomStrip, GestureAxis::Vertical,
                             GestureMod_None}},
                DragRowSpec{GestureContext::PianoRoll,
                            {GestureInputKind::Drag, GestureArea::Keyboard, GestureAxis::Horizontal,
                             GestureMod_None}},
                DragRowSpec{GestureContext::DrumGrid,
                            {GestureInputKind::Drag, GestureArea::Ruler, GestureAxis::Vertical,
                             GestureMod_None}},
                DragRowSpec{GestureContext::DrumGrid,
                            {GestureInputKind::Drag, GestureArea::ZoomStrip, GestureAxis::Vertical,
                             GestureMod_None}},
                DragRowSpec{GestureContext::Waveform,
                            {GestureInputKind::Drag, GestureArea::Ruler, GestureAxis::Vertical,
                             GestureMod_None}},
                DragRowSpec{GestureContext::Waveform,
                            {GestureInputKind::Drag, GestureArea::Header, GestureAxis::Vertical,
                             GestureMod_None}},
                DragRowSpec{GestureContext::Waveform,
                            {GestureInputKind::Drag, GestureArea::Body, GestureAxis::Vertical,
                             GestureMod_None}},
            };
        }

        static juce::String contextName(GestureContext context) {
            switch (context) {
                case GestureContext::Arrangement:
                    return "Arrangement";
                case GestureContext::PianoRoll:
                    return "Piano roll";
                case GestureContext::DrumGrid:
                    return "Drum grid";
                case GestureContext::Waveform:
                    return "Waveform";
                default:
                    return "Unknown";
            }
        }

        static bool sameInput(const GestureInput& a, const GestureInput& b) {
            return a.kind == b.kind && a.area == b.area && a.axis == b.axis &&
                   a.modifiers == b.modifiers;
        }

        static std::vector<GestureInput> candidateInputsForMovedBinding(const GestureRow& row) {
            std::vector<GestureInput> candidates;
            auto add = [&candidates](GestureInput input) { candidates.push_back(input); };

            for (auto axis : {GestureAxis::Vertical, GestureAxis::Horizontal}) {
                for (auto mods : modifiers())
                    add({GestureInputKind::Wheel, GestureArea::Main, axis, mods});
            }

            for (auto area : {GestureArea::Ruler, GestureArea::Body, GestureArea::Header,
                              GestureArea::Keyboard, GestureArea::ZoomStrip}) {
                for (auto mods : modifiers())
                    add({GestureInputKind::Drag, area, GestureAxis::Vertical, mods});
            }

            candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                            [&row](const GestureInput& input) {
                                                return sameInput(input, row.input);
                                            }),
                             candidates.end());
            return candidates;
        }

        static bool isOverrideBinding(const GestureRouter& router, GestureContext context,
                                      const GestureInput& input, const GestureBinding& binding) {
            const auto* defaultBinding = router.findDefaultBinding(context, input);
            return defaultBinding == nullptr || *defaultBinding != binding;
        }

        static juce::String areaName(GestureContext context, GestureArea area) {
            juce::String suffix;
            switch (area) {
                case GestureArea::Main:
                    suffix = "main";
                    break;
                case GestureArea::Ruler:
                    suffix = "ruler";
                    break;
                case GestureArea::Body:
                    suffix = "body";
                    break;
                case GestureArea::Header:
                    suffix = "header";
                    break;
                case GestureArea::Keyboard:
                    suffix = "keyboard";
                    break;
                case GestureArea::ZoomStrip:
                    suffix = "zoom strip";
                    break;
            }
            return contextName(context) + " " + suffix;
        }

        static juce::String inputName(const GestureInput& input) {
            juce::String base;
            if (input.kind == GestureInputKind::Wheel)
                base = input.axis == GestureAxis::Vertical ? "wheel" : "horizontal wheel";
            else
                base = "drag";

            if (input.modifiers == GestureMod_None)
                return base;
            return modifierName(input.modifiers) + "+" + base;
        }

        static juce::String learnButtonText() {
            return "Learn";
        }

        static juce::String capturePromptText(const GestureInput& input) {
            juce::ignoreUnused(input);
            return "Learning...";
        }

        static juce::String inputLabelName(const GestureInput& input) {
            if (input.kind == GestureInputKind::Wheel)
                return "wheel: " + inputName(input);

            juce::String dragText;
            if (input.modifiers == GestureMod_None)
                dragText = "drag";
            else
                dragText = modifierName(input.modifiers) + "+drag";
            return "drag: " + dragText;
        }

        static juce::String modifierName(uint8_t modifiers) {
            if (modifiers == GestureMod_None)
                return "None";

            juce::StringArray parts;
            if ((modifiers & GestureMod_Shift) != 0)
                parts.add("Shift");
#if JUCE_MAC
            if ((modifiers & GestureMod_Command) != 0)
                parts.add("Cmd");
#else
            if ((modifiers & GestureMod_Command) != 0)
                parts.add("Ctrl");
#endif
            if ((modifiers & GestureMod_Alt) != 0)
                parts.add("Alt");
            return parts.joinIntoString("+");
        }

        static juce::String gestureName(GestureActionType action) {
            switch (action) {
                case GestureActionType::ScrollHorizontal:
                    return "Horizontal scroll gesture";
                case GestureActionType::ScrollVertical:
                    return "Vertical scroll gesture";
                case GestureActionType::ZoomHorizontal:
                    return "Horizontal zoom gesture";
                case GestureActionType::ZoomVertical:
                    return "Vertical zoom gesture";
                case GestureActionType::Pan:
                    return "Pan gesture";
                case GestureActionType::None:
                    return "Unassigned gesture";
            }
            return "Unassigned gesture";
        }

        static GestureArea defaultDragAreaFor(GestureContext context, GestureActionType action) {
            switch (action) {
                case GestureActionType::ZoomHorizontal:
                    return context == GestureContext::Waveform ? GestureArea::Body
                                                               : GestureArea::Ruler;
                case GestureActionType::ZoomVertical:
                    return GestureArea::ZoomStrip;
                default:
                    return GestureArea::Main;
            }
        }

        static GestureBinding tunedBindingForInput(GestureContext context, GestureInput input,
                                                   GestureActionType action,
                                                   const GestureBinding& fallback) {
            auto& router = GestureRouter::getInstance();
            if (const auto* exact = router.findDefaultBinding(context, input);
                exact != nullptr && exact->action == action) {
                return *exact;
            }

            if (input.kind == GestureInputKind::Drag) {
                auto alternateAxis = input;
                alternateAxis.axis = input.axis == GestureAxis::Vertical ? GestureAxis::Horizontal
                                                                         : GestureAxis::Vertical;
                if (const auto* alternate = router.findDefaultBinding(context, alternateAxis);
                    alternate != nullptr && alternate->action == action) {
                    return *alternate;
                }

                auto unmodifiedAlternate = alternateAxis;
                unmodifiedAlternate.modifiers = GestureMod_None;
                if (const auto* unmodified =
                        router.findDefaultBinding(context, unmodifiedAlternate);
                    unmodified != nullptr && unmodified->action == action) {
                    return *unmodified;
                }
            }

            return fallback;
        }

        static void setupBodyLabel(juce::Label& label, const juce::String& text) {
            label.setText(text, juce::dontSendNotification);
            label.setFont(FontManager::getInstance().getUIFont(12.0f));
            label.setColour(juce::Label::textColourId,
                            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
            label.setJustificationType(juce::Justification::centredLeft);
        }

        void setupHeaderLabel(juce::Label& label, const juce::String& text) {
            label.setText(text, juce::dontSendNotification);
            label.setFont(FontManager::getInstance().getUIFontBold(11.0f));
            label.setColour(juce::Label::textColourId,
                            DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
            label.setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(label);
        }

        void addGestureRow(GestureContext context, GestureInput input) {
            auto row = std::make_unique<GestureRow>();
            row->context = context;
            row->input = input;
            row->currentInput = input;
            auto* rowPtr = row.get();

            if (const auto* binding =
                    GestureRouter::getInstance().findDefaultBinding(context, input))
                row->defaultBinding = *binding;

            row->action = row->defaultBinding.action;
            setupBodyLabel(row->contextLabel, gestureName(row->action));
            setupBodyLabel(row->axisLabel, areaName(context, input.area));
            setupBodyLabel(row->inputLabel, inputLabelName(row->currentInput));

            row->learnButton.setButtonText(learnButtonText());
            row->learnButton.onClick = [this, rowPtr] { setCapturingRow(rowPtr); };

            row->sensitivity.setRange(0.0, 1200.0, 0.01);
            row->sensitivity.setOrientation(magda::daw::ui::TextSlider::Orientation::Horizontal);
            row->sensitivity.setShowFillIndicator(false);
            row->sensitivity.setValueFormatter(
                [](double value) { return juce::String(value, value < 10.0 ? 2 : 0); });

            row->invert.setButtonText({});
            row->invert.setColour(juce::ToggleButton::tickColourId,
                                  DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
            row->invert.setColour(juce::ToggleButton::tickDisabledColourId,
                                  DarkTheme::getColour(DarkTheme::TEXT_DIM));

            content_.addAndMakeVisible(row->contextLabel);
            content_.addAndMakeVisible(row->axisLabel);
            content_.addAndMakeVisible(row->inputLabel);
            content_.addAndMakeVisible(row->learnButton);
            content_.addAndMakeVisible(row->sensitivity);
            content_.addAndMakeVisible(row->invert);
            rows_.push_back(std::move(row));
        }

        void loadFromRouter() {
            auto& router = GestureRouter::getInstance();
            for (auto& row : rows_) {
                GestureBinding binding;
                GestureInput displayedInput = row->input;
                if (const auto* current = router.findBinding(row->context, row->input)) {
                    binding = *current;
                }

                if (binding.action == GestureActionType::None &&
                    row->defaultBinding.action != GestureActionType::None) {
                    for (const auto& candidate : candidateInputsForMovedBinding(*row)) {
                        const auto* moved = router.findBinding(row->context, candidate);
                        if (moved == nullptr || moved->action != row->defaultBinding.action)
                            continue;
                        if (!isOverrideBinding(router, row->context, candidate, *moved))
                            continue;

                        binding = *moved;
                        displayedInput = candidate;
                        break;
                    }
                }

                if (displayedInput.kind == GestureInputKind::Drag &&
                    (binding.action == GestureActionType::ZoomHorizontal ||
                     binding.action == GestureActionType::ZoomVertical) &&
                    binding.sensitivity < 2.0f) {
                    binding =
                        tunedBindingForInput(row->context, displayedInput, binding.action, binding);
                }

                row->action = binding.action;
                row->currentInput = displayedInput;
                row->axisLabel.setText(areaName(row->context, row->currentInput.area),
                                       juce::dontSendNotification);
                row->inputLabel.setText(inputLabelName(row->currentInput),
                                        juce::dontSendNotification);
                row->sensitivity.setValue(binding.sensitivity, juce::dontSendNotification);
                row->invert.setToggleState(binding.invert, juce::dontSendNotification);

                const bool visible = binding.action != GestureActionType::None;
                row->contextLabel.setText(gestureName(binding.action), juce::dontSendNotification);
                row->contextLabel.setVisible(visible);
                row->axisLabel.setVisible(visible);
                row->inputLabel.setVisible(visible);
                row->learnButton.setVisible(visible);
                row->sensitivity.setVisible(visible);
                row->invert.setVisible(visible);
            }

            setCapturingRow(nullptr);
        }

        void setRowInput(GestureRow& row, GestureInput input) {
            const auto tuned =
                tunedBindingForInput(row.context, input, row.action,
                                     {row.action, static_cast<float>(row.sensitivity.getValue()),
                                      row.invert.getToggleState()});
            row.currentInput = input;
            row.axisLabel.setText(areaName(row.context, input.area), juce::dontSendNotification);
            row.inputLabel.setText(inputLabelName(input), juce::dontSendNotification);
            row.sensitivity.setValue(tuned.sensitivity, juce::dontSendNotification);
            row.invert.setToggleState(tuned.invert, juce::dontSendNotification);
            row.learnButton.setButtonText(learnButtonText());
        }

        GestureArea learnedDragAreaFor(const GestureRow& row) const {
            if (row.currentInput.area != GestureArea::Main)
                return row.currentInput.area;
            if (row.input.area != GestureArea::Main)
                return row.input.area;
            return defaultDragAreaFor(row.context, row.action);
        }

        void setCapturingRow(GestureRow* row) {
            if (capturingRow_ != nullptr)
                capturingRow_->learnButton.setButtonText(learnButtonText());

            capturingRow_ = row;

            if (capturingRow_ != nullptr) {
                capturingRow_->learnButton.setButtonText(
                    capturePromptText(capturingRow_->currentInput));
                captureDragStart_ = {};
                grabKeyboardFocus();
            }
        }

        void layoutColumnHeaders(juce::Rectangle<int> bounds) {
            auto widths = getColumnWidths(bounds.getWidth());
            contextHeader_.setBounds(bounds.removeFromLeft(widths[0]));
            axisHeader_.setBounds(bounds.removeFromLeft(widths[1]));
            modifierHeader_.setBounds(bounds.removeFromLeft(widths[2]));
            sensitivityHeader_.setBounds(bounds.removeFromLeft(widths[3]));
            invertHeader_.setBounds(bounds);
        }

        void layoutRows(int width) {
            const int rowH = 28;
            const int gap = 3;
            auto widths = getColumnWidths(width);
            int y = 0;

            for (auto& row : rows_) {
                if (!row->contextLabel.isVisible())
                    continue;

                auto bounds = juce::Rectangle<int>(0, y, width, rowH);
                row->contextLabel.setBounds(bounds.removeFromLeft(widths[0]).reduced(0, 2));
                row->axisLabel.setBounds(bounds.removeFromLeft(widths[1]).reduced(0, 2));
                auto inputArea = bounds.removeFromLeft(widths[2]);
                row->learnButton.setBounds(inputArea.removeFromRight(96).reduced(3, 3));
                row->inputLabel.setBounds(inputArea.reduced(0, 2));
                row->sensitivity.setBounds(bounds.removeFromLeft(widths[3] - gap).reduced(0, 4));
                bounds.removeFromLeft(gap);
                row->invert.setBounds(bounds.reduced(0, 2));
                y += rowH;
            }

            content_.setSize(width, y + 8);
        }

        static std::array<int, 5> getColumnWidths(int width) {
            const int invertW = 56;
            const int sensitivityW = 104;
            const int inputW = juce::jlimit(300, 400, width / 3);
            const int areaW = 116;
            const int gestureW = juce::jmax(160, width - invertW - sensitivityW - inputW - areaW);
            return {gestureW, areaW, inputW, sensitivityW, invertW};
        }

        juce::Label header_;
        juce::TextButton resetButton_;
        juce::Label contextHeader_, axisHeader_, modifierHeader_;
        juce::Label sensitivityHeader_, invertHeader_;
        CaptureViewport viewport_{*this};
        juce::Component content_;
        std::vector<std::unique_ptr<GestureRow>> rows_;
        GestureRow* capturingRow_ = nullptr;
        juce::Point<int> captureDragStart_;
    };

    juce::TabbedComponent tabbedComponent_;
    std::unique_ptr<KeyboardShortcutsPage> keyboardPage_;
    std::unique_ptr<GestureBindingsPage> gesturesPage_;
};

// ---------------------------------------------------------------------------
// PreferencesDialog
// ---------------------------------------------------------------------------

PreferencesDialog::PreferencesDialog(juce::ApplicationCommandManager* commandManager) {
    setLookAndFeel(&daw::ui::DialogLookAndFeel::getInstance());

    generalPage = std::make_unique<GeneralPage>();
    coloursPage = std::make_unique<ColoursPage>();
    renderingPage = std::make_unique<RenderingPage>();
    pathsPage = std::make_unique<PathsPage>();
    shortcutsPage = std::make_unique<ShortcutsPage>(commandManager);

    auto setupPageViewport = [](juce::Viewport& viewport, juce::Component& page) {
        viewport.setViewedComponent(&page, false);
        viewport.setScrollBarsShown(true, false, true, false);
        viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
    };
    setupPageViewport(generalPageViewport, *generalPage);
    setupPageViewport(coloursPageViewport, *coloursPage);
    setupPageViewport(renderingPageViewport, *renderingPage);
    setupPageViewport(pathsPageViewport, *pathsPage);

    auto tabBg = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
    tabbedComponent.addTab(tr("preferences.tab.general"), tabBg, &generalPageViewport, false);
    tabbedComponent.addTab(tr("preferences.tab.colours"), tabBg, &coloursPageViewport, false);
    tabbedComponent.addTab(tr("preferences.tab.rendering"), tabBg, &renderingPageViewport, false);
    tabbedComponent.addTab(tr("preferences.tab.paths"), tabBg, &pathsPageViewport, false);
    tabbedComponent.addTab(tr("preferences.tab.shortcuts"), tabBg, shortcutsPage.get(), false);
    tabbedComponent.setTabBarDepth(36);
    addAndMakeVisible(tabbedComponent);

    okButton.setButtonText(tr("dialogs.ok"));
    okButton.onClick = [this]() {
        applySettings();
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(1);
    };
    addAndMakeVisible(okButton);

    cancelButton.setButtonText(tr("dialogs.cancel"));
    cancelButton.onClick = [this]() {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };
    addAndMakeVisible(cancelButton);

    applyButton.setButtonText(tr("dialogs.apply"));
    applyButton.onClick = [this]() { applySettings(); };
    addAndMakeVisible(applyButton);

    loadCurrentSettings();
    const auto contentSize = getPreferencesDialogContentSize();
    setSize(contentSize.getWidth(), contentSize.getHeight());
}

PreferencesDialog::~PreferencesDialog() {
    setLookAndFeel(nullptr);
}

void PreferencesDialog::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
}

void PreferencesDialog::resized() {
    const int buttonH = 28;
    const int buttonW = 80;
    const int buttonSpacing = 10;
    const int margin = 16;

    auto bounds = getLocalBounds();

    // Reserve bottom strip for the button row
    auto bottomStrip = bounds.removeFromBottom(buttonH + (margin * 2));
    tabbedComponent.setBounds(bounds);
    updatePageViewports();

    // Right-align buttons within the bottom strip
    bottomStrip.reduce(margin, margin);
    const int totalBtnW = (buttonW * 3) + (buttonSpacing * 2);
    bottomStrip.removeFromLeft(bottomStrip.getWidth() - totalBtnW);

    cancelButton.setBounds(bottomStrip.removeFromLeft(buttonW));
    bottomStrip.removeFromLeft(buttonSpacing);
    applyButton.setBounds(bottomStrip.removeFromLeft(buttonW));
    bottomStrip.removeFromLeft(buttonSpacing);
    okButton.setBounds(bottomStrip.removeFromLeft(buttonW));
}

void PreferencesDialog::updatePageViewports() {
    auto updateContentSize = [](juce::Viewport& viewport, juce::Component& page,
                                int preferredHeight) {
        const int viewW = juce::jmax(1, viewport.getMaximumVisibleWidth());
        const int viewH = juce::jmax(1, viewport.getMaximumVisibleHeight());
        page.setSize(viewW, juce::jmax(viewH, preferredHeight));
    };

    const auto updateAll = [this, &updateContentSize] {
        if (generalPage) {
            const int viewW = juce::jmax(1, generalPageViewport.getMaximumVisibleWidth());
            updateContentSize(generalPageViewport, *generalPage,
                              generalPage->getPreferredHeight(viewW));
        }
        if (coloursPage) {
            const int viewW = juce::jmax(1, coloursPageViewport.getMaximumVisibleWidth());
            updateContentSize(coloursPageViewport, *coloursPage,
                              coloursPage->getPreferredHeight(viewW));
        }
        if (renderingPage) {
            const int viewW = juce::jmax(1, renderingPageViewport.getMaximumVisibleWidth());
            updateContentSize(renderingPageViewport, *renderingPage,
                              renderingPage->getPreferredHeight(viewW));
        }
        if (pathsPage) {
            const int viewW = juce::jmax(1, pathsPageViewport.getMaximumVisibleWidth());
            updateContentSize(pathsPageViewport, *pathsPage, pathsPage->getPreferredHeight(viewW));
        }
    };

    updateAll();
    updateAll();
}

void PreferencesDialog::loadCurrentSettings() {
    auto& config = Config::getInstance();
    generalPage->loadSettings(config);
    coloursPage->loadSettings(config);
    renderingPage->loadSettings(config);
    pathsPage->loadSettings(config);
    shortcutsPage->loadSettings(config);
}

namespace {

void showMigrationFailureAsync(const juce::File& from, const juce::File& to) {
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::WarningIcon)
                                     .withTitle(tr("preferences.paths.migration.fail_title"))
                                     .withMessage(tr("preferences.paths.migration.fail_message")
                                                      .replace("{0}", from.getFullPathName())
                                                      .replace("{1}", to.getFullPathName()))
                                     .withButton(tr("dialogs.ok")),
                                 nullptr);
}

// Copy `from` into `to`. No-op if either is missing or the paths are equal.
// The destination is created if needed so copyDirectoryTo has a target.
bool copyFolderIfNeeded(const juce::File& from, const juce::File& to) {
    if (!from.isDirectory() || from.getFullPathName() == to.getFullPathName())
        return true;
    to.createDirectory();
    return from.copyDirectoryTo(to);
}

void scaleExplicitFonts(juce::Component& component, double ratio) {
    if (std::abs(ratio - 1.0) < 0.001)
        return;

    const auto scaleFont = [ratio](juce::Font font) {
        return font.withHeight(font.getHeight() * static_cast<float>(ratio));
    };

    if (auto* label = dynamic_cast<juce::Label*>(&component))
        label->setFont(scaleFont(label->getFont()));
    else if (auto* editor = dynamic_cast<juce::TextEditor*>(&component))
        editor->setFont(scaleFont(editor->getFont()));

    for (int i = 0; i < component.getNumChildComponents(); ++i) {
        if (auto* child = component.getChildComponent(i))
            scaleExplicitFonts(*child, ratio);
    }
}

void refreshFontScaleForOpenWindows(double oldScale, double newScale) {
    if (oldScale <= 0.0)
        oldScale = 1.0;

    const double ratio = newScale / oldScale;
    for (int i = juce::TopLevelWindow::getNumTopLevelWindows(); --i >= 0;) {
        if (auto* window = juce::TopLevelWindow::getTopLevelWindow(i)) {
            scaleExplicitFonts(*window, ratio);
            window->resized();
            window->repaint();
        }
    }
}

}  // namespace

void PreferencesDialog::applySettings() {
    auto& config = Config::getInstance();
    const double oldFontScale = config.getUIFontScale();

    // Snapshot the path changes the user committed at Browse time. Capture
    // these before applying pages so the values don't shift mid-flight.
    const bool dataChanged = pathsPage->dataDirChanged();
    const bool presetsChanged = pathsPage->presetsDirChanged();
    const bool copyData = pathsPage->shouldCopyData();
    const bool copyPresets = pathsPage->shouldCopyPresets();
    const juce::File dataFrom(juce::String(pathsPage->getOriginalDataPath()).isEmpty()
                                  ? magda::paths::alwaysOSDefault().getFullPathName()
                                  : juce::String(pathsPage->getOriginalDataPath()));
    const juce::File dataTo(juce::String(pathsPage->getNewDataPath()));
    const juce::File presetsFrom(
        juce::String(pathsPage->getOriginalPresetsPath()).isEmpty()
            ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                  .getChildFile("MAGDA")
                  .getChildFile("Presets")
                  .getFullPathName()
            : juce::String(pathsPage->getOriginalPresetsPath()));
    const juce::File presetsTo(juce::String(pathsPage->getNewPresetsPath()));

    // Apply non-path pages and persist.
    generalPage->applySettings(config);
    coloursPage->applySettings(config);
    renderingPage->applySettings(config);
    shortcutsPage->applySettings(config);
    pathsPage->applySettings(config);  // no-op for path values
    config.save();

    const double newFontScale = config.getUIFontScale();
    refreshFontScaleForOpenWindows(oldFontScale, newFontScale);

    // Apply auto-save settings
    ProjectManager::getInstance().setAutoSaveEnabled(config.getAutoSaveEnabled(),
                                                     config.getAutoSaveIntervalSeconds());

    // Apply timeline length to live session
    if (auto* tc = TimelineController::getCurrent()) {
        double newLength = tc->getState().tempo.barsToTime(config.getDefaultTimelineLengthBars());
        tc->dispatch(SetTimelineLengthEvent{newLength});
    }

    // Apply panel visibility and layout to live session
    for (int i = juce::TopLevelWindow::getNumTopLevelWindows(); --i >= 0;) {
        if (auto* mw = dynamic_cast<MainWindow*>(juce::TopLevelWindow::getTopLevelWindow(i))) {
            mw->applyPanelVisibilityFromConfig();
            mw->applyLayoutFromConfig();
            break;
        }
    }

    // Presets path: hot-apply (no restart needed). The user already chose
    // copy / don't-copy at Browse time; here we just commit.
    if (presetsChanged) {
        config.setPresetsDir(pathsPage->getNewPresetsPath());
        config.save();
        magda::paths::resolve();
        if (copyPresets && !copyFolderIfNeeded(presetsFrom, presetsTo))
            showMigrationFailureAsync(presetsFrom, presetsTo);
    }

    // Data path: needs a restart so the logger / plugin scanner / etc.
    // re-open at the new location. Persist Config, copy if requested, quit.
    // The Browse-time prompt already told the user the app would restart on
    // Apply, so no second confirmation is shown here.
    if (dataChanged) {
        config.setDataDir(pathsPage->getNewDataPath());
        config.save();
        magda::paths::resolve();
        if (copyData && !copyFolderIfNeeded(dataFrom, dataTo)) {
            showMigrationFailureAsync(dataFrom, dataTo);
            return;  // Don't quit if the copy failed — let the user investigate.
        }
        juce::JUCEApplication::quit();
    }
}

void PreferencesDialog::showDialog(juce::Component* parent) {
    juce::ApplicationCommandManager* commandManager = nullptr;
    if (auto* mainWindow = dynamic_cast<MainWindow*>(parent))
        commandManager = &mainWindow->getCommandManager();
    else if (auto* mainWindow =
                 parent != nullptr ? parent->findParentComponentOfClass<MainWindow>() : nullptr)
        commandManager = &mainWindow->getCommandManager();

    auto* dialog = new PreferencesDialog(commandManager);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = tr("dialogs.preferences");
    options.dialogBackgroundColour = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
    options.content.setOwned(dialog);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    options.launchAsync();
}

}  // namespace magda
