#include "drum_grid/PadDeviceSlot.hpp"

#include <BinaryData.h>
#include <tracktion_engine/tracktion_engine.h>

#include "audio/plugins/MagdaSamplerPlugin.hpp"
#include "compiled/CompiledPluginPresentation.hpp"
#include "custom_ui/FaustCustomUIRegistry.hpp"
#include "custom_ui/FaustUI.hpp"
#include "layout/DeviceSlotHeaderLayout.hpp"
#include "slot/DeviceSlotInlineUiFactory.hpp"
#include "ui/debug/DebugSettings.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace te = tracktion::engine;

namespace magda::daw::ui {

namespace {

juce::String linkPathString(const magda::ChainNodePath& path) {
    return path.isValid() ? path.toString() : juce::String("<invalid>");
}

juce::String yesNo(bool value) {
    return value ? "yes" : "no";
}

}  // namespace

PadDeviceSlot::PadDeviceSlot() {
    nameLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    nameLabel_.setJustificationType(juce::Justification::centredLeft);
    nameLabel_.setInterceptsMouseClicks(true, false);
    nameLabel_.addMouseListener(this, false);
    addAndMakeVisible(nameLabel_);

    deleteButton_.setButtonText(juce::CharPointer_UTF8("\xc3\x97"));  // multiplication sign
    auto deleteColour = DarkTheme::getColour(DarkTheme::ACCENT_PURPLE)
                            .interpolatedWith(DarkTheme::getColour(DarkTheme::STATUS_ERROR), 0.5f)
                            .darker(0.2f);
    deleteButton_.setColour(juce::TextButton::buttonColourId, deleteColour);
    deleteButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    deleteButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    deleteButton_.onClick = [this]() {
        if (onDeleteClicked)
            onDeleteClicked();
    };
    addAndMakeVisible(deleteButton_);

    // UI button to open plugin native window
    uiButton_ = std::make_unique<magda::SvgButton>("UI", BinaryData::open_in_new_svg,
                                                   BinaryData::open_in_new_svgSize);
    uiButton_->setIconPadding(2.5f);
    uiButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    uiButton_->setClickingTogglesState(true);
    uiButton_->setNormalColor(juce::Colour(0xFFB3B3B3).withAlpha(0.5f));
    uiButton_->setActiveColor(juce::Colours::white);
    uiButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    addChildComponent(*uiButton_);

    // On/power button
    onButton_ = std::make_unique<magda::SvgButton>("Power", BinaryData::power_on_svg,
                                                   BinaryData::power_on_svgSize);
    onButton_->setClickingTogglesState(true);
    onButton_->setToggleState(true, juce::dontSendNotification);
    onButton_->setNormalColor(DarkTheme::getColour(DarkTheme::STATUS_ERROR));
    onButton_->setActiveColor(juce::Colours::white);
    onButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_GREEN).darker(0.3f));
    onButton_->setActive(true);
    onButton_->onClick = [this]() {
        if (plugin_) {
            bool active = onButton_->getToggleState();
            onButton_->setActive(active);
            plugin_->setEnabled(active);
        }
    };
    addAndMakeVisible(*onButton_);

    // Gain slider (header, right side)
    gainSlider_.setFormat(TextSlider::Format::Decibels);
    gainSlider_.setRange(-60.0, 12.0, 0.1);
    gainSlider_.setValue(0.0, juce::dontSendNotification);
    gainSlider_.setShowFillIndicator(false);
    gainSlider_.onValueChanged = [this](double value) {
        if (onGainDbChanged)
            onGainDbChanged(static_cast<float>(value));
    };
    addAndMakeVisible(gainSlider_);

    // Level meter (right edge of content area)
    addAndMakeVisible(levelMeter_);

    // Create param slots
    for (int i = 0; i < PLUGIN_PARAM_SLOTS; ++i) {
        paramSlots_[static_cast<size_t>(i)] = std::make_unique<ParamSlotComponent>(i);
        addChildComponent(*paramSlots_[static_cast<size_t>(i)]);
    }

    startTimerHz(30);
}

PadDeviceSlot::~PadDeviceSlot() {
    stopTimer();
    nameLabel_.removeMouseListener(this);
    deleteButton_.setLookAndFeel(nullptr);
}

void PadDeviceSlot::setGainDb(float db) {
    gainSlider_.setValue(static_cast<double>(db), juce::dontSendNotification);
}

void PadDeviceSlot::timerCallback() {
    if (getMeterLevels) {
        auto [l, r] = getMeterLevels();
        levelMeter_.setLevels(l, r);
    }
}

void PadDeviceSlot::setPlugin(te::Plugin* plugin) {
    plugin_ = plugin;
    livePluginProvider_ = {};
    resetSharedInlineUi();
    if (!plugin) {
        clear();
        return;
    }

    // Check if it's a sampler
    if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin)) {
        setupForSampler(sampler);
    } else {
        setupForExternalPlugin(plugin);
    }

    // Restore collapsed state from plugin's ValueTree
    bool wasCollapsed = plugin->state.getProperty("uiCollapsed", false);
    if (wasCollapsed)
        collapsed_ = true;

    // Update on button state
    onButton_->setToggleState(plugin->isEnabled(), juce::dontSendNotification);
    onButton_->setActive(plugin->isEnabled());

    resized();
}

void PadDeviceSlot::setPlugin(te::Plugin* plugin, const magda::DeviceInfo& device,
                              std::function<te::Plugin::Ptr()> livePlugin) {
    plugin_ = plugin;
    device_ = device;
    livePluginProvider_ = std::move(livePlugin);
    resetSharedInlineUi();
    if (!plugin) {
        clear();
        return;
    }

    if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin)) {
        setupForSampler(sampler);
    } else if (!setupForSharedDeviceUi(plugin, device)) {
        setupForExternalPlugin(plugin);
    }

    bool wasCollapsed = plugin->state.getProperty("uiCollapsed", false);
    if (wasCollapsed)
        collapsed_ = true;

    onButton_->setToggleState(plugin->isEnabled(), juce::dontSendNotification);
    onButton_->setActive(plugin->isEnabled());

    resized();
}

void PadDeviceSlot::setSampler(daw::audio::MagdaSamplerPlugin* sampler) {
    plugin_ = sampler;
    livePluginProvider_ = {};
    resetSharedInlineUi();
    if (!sampler) {
        clear();
        return;
    }
    setupForSampler(sampler);
    onButton_->setToggleState(sampler->isEnabled(), juce::dontSendNotification);
    onButton_->setActive(sampler->isEnabled());
    resized();
}

void PadDeviceSlot::clear() {
    plugin_ = nullptr;
    pluginDeviceId_ = magda::INVALID_DEVICE_ID;
    device_ = {};
    livePluginProvider_ = {};
    resetSharedInlineUi();
    visibleParamCount_ = 0;
    nameLabel_.setText("", juce::dontSendNotification);
    samplerUI_.reset();
    for (auto& slot : paramSlots_)
        if (slot)
            slot->setVisible(false);
    uiButton_->setVisible(false);
}

int PadDeviceSlot::getPreferredWidth() const {
    if (collapsed_)
        return COLLAPSED_WIDTH;
    return preferredWidth_;
}

void PadDeviceSlot::setCollapsed(bool collapsed) {
    if (collapsed_ != collapsed) {
        collapsed_ = collapsed;
        // Persist to plugin's ValueTree so it survives save/reload
        if (plugin_)
            plugin_->state.setProperty("uiCollapsed", collapsed, nullptr);
        resized();
        repaint();
        if (onLayoutChanged)
            onLayoutChanged();
    }
}

void PadDeviceSlot::resetSharedInlineUi() {
    usingSharedInlineUi_ = false;
    compiledPanel_.reset();
    faustCustomView_.reset();
    faustUI_.reset();
    customUI_.reset();
}

void PadDeviceSlot::setupForSampler(daw::audio::MagdaSamplerPlugin* sampler) {
    resetSharedInlineUi();
    preferredWidth_ = SAMPLER_SLOT_WIDTH;
    uiButton_->setVisible(false);

    nameLabel_.setText("Sampler", juce::dontSendNotification);

    // Create SamplerUI if needed
    if (!samplerUI_) {
        samplerUI_ = std::make_unique<SamplerUI>();
        addAndMakeVisible(*samplerUI_);
    }

    // Wire SamplerUI callbacks
    samplerUI_->onParameterChanged = [sampler](int paramIndex, float value) {
        auto params = sampler->getAutomatableParameters();
        if (paramIndex >= 0 && paramIndex < params.size()) {
            params[paramIndex]->setParameter(value, juce::sendNotification);
            // Sync CachedValue for persistence (param and CachedValue are independent)
            sampler->syncCachedValueFromParam(paramIndex);
        }
    };

    samplerUI_->onLoopEnabledChanged = [sampler](bool enabled) {
        sampler->loopEnabledAtomic.store(enabled, std::memory_order_relaxed);
        sampler->loopEnabledValue = enabled;
    };

    samplerUI_->onRootNoteChanged = [sampler](int note) { sampler->setRootNote(note); };

    samplerUI_->getPlaybackPosition = [sampler]() -> double {
        return sampler->getPlaybackPosition();
    };

    samplerUI_->onFileDropped = [this](const juce::File& file) {
        if (onSampleDropped)
            onSampleDropped(file);
    };

    samplerUI_->onLoadSampleRequested = [this]() {
        if (onLoadSampleRequested)
            onLoadSampleRequested();
    };

    // Update parameters
    juce::String sampleName;
    auto file = sampler->getSampleFile();
    if (file.existsAsFile())
        sampleName = file.getFileNameWithoutExtension();

    samplerUI_->updateParameters(
        sampler->attackValue.get(), sampler->decayValue.get(), sampler->sustainValue.get(),
        sampler->releaseValue.get(), sampler->pitchValue.get(), sampler->fineValue.get(),
        sampler->levelValue.get(), sampler->sampleStartValue.get(), sampler->sampleEndValue.get(),
        sampler->loopEnabledValue.get(), sampler->loopStartValue.get(), sampler->loopEndValue.get(),
        sampler->velAmountValue.get(), sampleName, sampler->getRootNote());

    samplerUI_->setWaveformData(sampler->getWaveform(), sampler->getSampleRate(),
                                sampler->getSampleLengthSeconds());

    samplerUI_->setVisible(true);

    // Hide param slots — sampler uses LinkableTextSliders in SamplerUI instead
    for (auto& slot : paramSlots_)
        if (slot)
            slot->setVisible(false);
}

bool PadDeviceSlot::setupForSharedDeviceUi(te::Plugin* plugin, const magda::DeviceInfo& device) {
    if (plugin == nullptr || device.pluginId.isEmpty() ||
        device.format != magda::PluginFormat::Internal)
        return false;

    traits_ = makeDeviceSlotTraits(device.pluginId);
    customUI_ = std::make_unique<DeviceCustomUIManager>();

    if (samplerUI_)
        samplerUI_->setVisible(false);
    for (auto& slot : paramSlots_)
        if (slot)
            slot->setVisible(false);
    visibleParamCount_ = 0;

    nameLabel_.setText(device.name.isNotEmpty() ? device.name : plugin->getName(),
                       juce::dontSendNotification);
    uiButton_->setVisible(false);

    DeviceSlotInlineUiCallbacks callbacks;
    callbacks.onParameterChanged = [this](int paramIndex, float value) {
        if (plugin_ == nullptr)
            return;
        auto params = plugin_->getAutomatableParameters();
        if (paramIndex >= 0 && paramIndex < params.size())
            params[paramIndex]->setParameter(value, juce::sendNotificationSync);
        if (auto* info = device_.findParameterByIndex(paramIndex))
            info->currentValue = value;
    };
    callbacks.onLayoutChanged = [this]() {
        resized();
        repaint();
        if (onLayoutChanged)
            onLayoutChanged();
    };
    callbacks.getNodePath = [this]() { return devicePath_; };
    callbacks.getLivePlugin = [this]() -> te::Plugin::Ptr {
        if (livePluginProvider_) {
            if (auto plugin = livePluginProvider_())
                return plugin;
        }
        return plugin_ != nullptr ? te::Plugin::Ptr(plugin_) : te::Plugin::Ptr();
    };

    const auto createdKind = createDeviceSlotInlineUi(device_, traits_, devicePath_, *this,
                                                      {.compiledPanel = compiledPanel_,
                                                       .faustUI = faustUI_,
                                                       .faustCustomView = faustCustomView_,
                                                       .customUI = *customUI_},
                                                      std::move(callbacks));

    usingSharedInlineUi_ = createdKind == DeviceSlotInlineUiKind::Compiled ||
                           createdKind == DeviceSlotInlineUiKind::Faust ||
                           (customUI_ != nullptr && customUI_->hasAnyUI());
    if (!usingSharedInlineUi_) {
        resetSharedInlineUi();
        return false;
    }

    preferredWidth_ = SLOT_WIDTH;
    if (traits_.compiledPresentation != nullptr &&
        traits_.compiledPresentation->preferredSlotWidth > 0)
        preferredWidth_ = traits_.compiledPresentation->preferredSlotWidth;
    else if (customUI_ != nullptr) {
        const auto customWidth = customUI_->getPreferredContentWidth();
        if (customWidth > 0)
            preferredWidth_ = juce::jmax(preferredWidth_, customWidth);
    }

    updateDeviceSlotInlineUi(device_, compiledPanel_.get(), *customUI_);
    readAndPushDeviceSlotInlineUiModMatrix(device_.id, *customUI_);
    return true;
}

void PadDeviceSlot::setupForExternalPlugin(te::Plugin* plugin) {
    resetSharedInlineUi();
    // Hide SamplerUI
    if (samplerUI_)
        samplerUI_->setVisible(false);

    nameLabel_.setText(plugin->getName(), juce::dontSendNotification);

    // Show UI button for external plugins
    uiButton_->setVisible(true);
    uiButton_->onClick = [this, plugin]() {
        bool isOpen = uiButton_->getToggleState();
        uiButton_->setActive(isOpen);
        if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin)) {
            if (ext->windowState) {
                if (isOpen)
                    ext->windowState->showWindowExplicitly();
                else
                    ext->windowState->hideWindowTemporarily();
            }
        } else {
            if (isOpen)
                plugin->showWindowExplicitly();
        }
    };

    // Populate param slots
    auto params = plugin->getAutomatableParameters();
    visibleParamCount_ = params.size();
    for (int i = 0; i < PLUGIN_PARAM_SLOTS; ++i) {
        auto& slot = paramSlots_[static_cast<size_t>(i)];
        if (i < params.size()) {
            auto* param = params[i];
            slot->setParamIndex(i);
            slot->setParamName(param->getParameterName());
            slot->setParamValue(param->getCurrentNormalisedValue());
            slot->onValueChanged = [param](double value) {
                param->setParameter(static_cast<float>(value), juce::sendNotificationSync);
            };
            slot->setVisible(true);
        } else {
            slot->setVisible(false);
        }
    }

    // 8 columns × 4 rows, matching DeviceSlotComponent layout
    constexpr int paramsPerRow = 8;
    constexpr int PARAM_CELL_WIDTH = 48;
    preferredWidth_ = PARAM_CELL_WIDTH * paramsPerRow;
}

void PadDeviceSlot::setLinkContext(magda::DeviceId deviceId, const magda::ChainNodePath& devicePath,
                                   const magda::ChainNodePath& linkOwnerPath,
                                   const magda::MacroArray* macros, const magda::ModArray* mods,
                                   const magda::MacroArray* trackMacros,
                                   const magda::ModArray* trackMods, int selectedModIndex,
                                   int selectedMacroIndex) {
    pluginDeviceId_ = deviceId;
    devicePath_ = devicePath;
    linkOwnerPath_ = linkOwnerPath;
    if (customUI_ != nullptr)
        customUI_->setDevicePath(devicePath_);
    if (faustUI_ != nullptr)
        faustUI_->setDevicePath(devicePath_);

    // Wire external plugin ParamSlotComponents
    for (int i = 0; i < PLUGIN_PARAM_SLOTS; ++i) {
        auto& slot = paramSlots_[static_cast<size_t>(i)];
        slot->setDeviceId(deviceId);
        slot->setDevicePath(devicePath);
        slot->setLinkOwnerPath(linkOwnerPath);
        slot->setAvailableMacros(macros);
        slot->setAvailableMods(mods);
        slot->setAvailableTrackMacros(trackMacros);
        slot->setAvailableTrackMods(trackMods);
        slot->setSelectedModIndex(selectedModIndex);
        slot->setSelectedMacroIndex(selectedMacroIndex);
        slot->refreshLinkModeState();
    }

    // Wire sampler LinkableTextSliders
    if (samplerUI_) {
        auto sliders = samplerUI_->getLinkableSliders();
        DBG("[PadDeviceLink] sampler context deviceId="
            << deviceId << " target=" << linkPathString(devicePath)
            << " owner=" << linkPathString(linkOwnerPath) << " sliders=" << sliders.size()
            << " selectedMod=" << selectedModIndex << " selectedMacro=" << selectedMacroIndex
            << " macros=" << yesNo(macros != nullptr));
        for (int i = 0; i < static_cast<int>(sliders.size()); ++i) {
            auto* slider = sliders[static_cast<size_t>(i)];
            int paramIdx = slider->getParamIndex() >= 0 ? slider->getParamIndex() : i;
            DBG("[PadDeviceLink]   slider slot=" << i << " param=" << paramIdx);
            slider->setLinkContext(deviceId, paramIdx, devicePath);
            slider->setLinkOwnerPath(linkOwnerPath);
            slider->setAvailableMacros(macros);
            slider->setAvailableMods(mods);
            slider->setAvailableTrackMacros(trackMacros);
            slider->setAvailableTrackMods(trackMods);
            slider->setSelectedModIndex(selectedModIndex);
            slider->setSelectedMacroIndex(selectedMacroIndex);
            slider->refreshLinkModeState();
        }
    }

    if (customUI_ != nullptr) {
        auto sliders = customUI_->getLinkableSliders();
        for (int i = 0; i < static_cast<int>(sliders.size()); ++i) {
            auto* slider = sliders[static_cast<size_t>(i)];
            if (slider == nullptr)
                continue;
            int paramIdx = slider->getParamIndex() >= 0 ? slider->getParamIndex() : i;
            slider->setLinkContext(deviceId, paramIdx, devicePath);
            if (const auto* paramInfo = device_.findParameterByIndex(paramIdx))
                slider->setParameterInfo(*paramInfo);
            slider->setLinkOwnerPath(linkOwnerPath);
            slider->setAvailableMacros(macros);
            slider->setAvailableMods(mods);
            slider->setAvailableTrackMacros(trackMacros);
            slider->setAvailableTrackMods(trackMods);
            slider->setSelectedModIndex(selectedModIndex);
            slider->setSelectedMacroIndex(selectedMacroIndex);
            slider->refreshLinkModeState();
        }
    }
}

std::vector<LinkableTextSlider*> PadDeviceSlot::getLinkableSliders() {
    if (samplerUI_)
        return samplerUI_->getLinkableSliders();
    if (customUI_ != nullptr)
        return customUI_->getLinkableSliders();
    return {};
}

void PadDeviceSlot::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    float cornerRadius = collapsed_ ? 4.0f : 0.0f;

    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    g.fillRoundedRectangle(bounds, cornerRadius);

    // Selection border (matches NodeComponent style)
    if (selected_) {
        g.setColour(juce::Colour(0xff888888));
        g.drawRoundedRectangle(bounds.reduced(1.0f), 4.0f, 2.0f);
    }

    // Draw rotated name when collapsed
    if (collapsed_) {
        // Text area = nameLabel_ bounds (below buttons)
        auto textArea = nameLabel_.getBounds();
        if (textArea.getHeight() > 20) {
            g.setColour(DarkTheme::getTextColour());
            g.setFont(FontManager::getInstance().getUIFont(9.0f));

            g.saveState();
            g.addTransform(juce::AffineTransform::rotation(
                -juce::MathConstants<float>::halfPi, static_cast<float>(textArea.getCentreX()),
                static_cast<float>(textArea.getCentreY())));
            g.drawText(nameLabel_.getText(), textArea.getCentreX() - textArea.getHeight() / 2,
                       textArea.getCentreY() - textArea.getWidth() / 2, textArea.getHeight(),
                       textArea.getWidth(), juce::Justification::centred, true);
            g.restoreState();
        }
    }
}

void PadDeviceSlot::mouseDown(const juce::MouseEvent& e) {
    // Click name label or collapsed body
    if (e.originalComponent == &nameLabel_ || (collapsed_ && e.originalComponent == this)) {
        bool wasSelected = selected_;

        // Always select (fires inspector update)
        if (onClicked)
            onClicked();
        selected_ = true;

        // If already selected, toggle collapse
        if (wasSelected)
            setCollapsed(!collapsed_);

        repaint();
        return;
    }
    juce::Component::mouseDown(e);
}

void PadDeviceSlot::resized() {
    auto area = getLocalBounds().reduced(2, 1);

    if (collapsed_) {
        // Collapsed: stack buttons vertically at top, rotated name below
        int btnSize = juce::jmin(20, area.getWidth());
        int btnGap = 2;

        area.removeFromTop(4);  // Push buttons down from top edge
        auto btnRow = area.removeFromTop(btnSize);
        deleteButton_.setBounds(btnRow.withSizeKeepingCentre(btnSize, btnSize));
        area.removeFromTop(btnGap);

        btnRow = area.removeFromTop(btnSize);
        onButton_->setBounds(btnRow.withSizeKeepingCentre(btnSize, btnSize));
        area.removeFromTop(btnGap);

        if (uiButton_->isVisible()) {
            btnRow = area.removeFromTop(btnSize);
            uiButton_->setBounds(btnRow.withSizeKeepingCentre(btnSize, btnSize));
            area.removeFromTop(btnGap);
        }

        // Rotated name label area — visible but transparent for click handling
        // (paint() draws the rotated text)
        nameLabel_.setBounds(area);
        nameLabel_.setVisible(true);
        nameLabel_.setColour(juce::Label::textColourId, juce::Colours::transparentBlack);

        // Hide content
        if (samplerUI_)
            samplerUI_->setVisible(false);
        if (compiledPanel_)
            compiledPanel_->component().setVisible(false);
        if (faustUI_)
            faustUI_->setVisible(false);
        if (faustCustomView_)
            faustCustomView_->setVisible(false);
        if (customUI_ != nullptr)
            if (auto* active = customUI_->getActiveUI())
                active->setVisible(false);
        for (auto& slot : paramSlots_)
            if (slot)
                slot->setVisible(false);
        gainSlider_.setVisible(false);
        levelMeter_.setVisible(false);
        return;
    }

    gainSlider_.setVisible(true);
    levelMeter_.setVisible(true);

    // Expanded: restore visibility and text colour
    nameLabel_.setVisible(true);
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    if (plugin_) {
        bool isSampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin_) != nullptr;
        if (samplerUI_)
            samplerUI_->setVisible(isSampler);
        if (compiledPanel_)
            compiledPanel_->component().setVisible(usingSharedInlineUi_);
        if (faustUI_)
            faustUI_->setVisible(usingSharedInlineUi_);
        if (faustCustomView_)
            faustCustomView_->setVisible(usingSharedInlineUi_);
        if (customUI_ != nullptr)
            if (auto* active = customUI_->getActiveUI())
                active->setVisible(usingSharedInlineUi_);
        if (!isSampler && !usingSharedInlineUi_) {
            // Restore param slot visibility for external plugins
            for (int i = 0; i < PLUGIN_PARAM_SLOTS; ++i) {
                if (paramSlots_[static_cast<size_t>(i)])
                    paramSlots_[static_cast<size_t>(i)]->setVisible(i < visibleParamCount_);
            }
        }
    }

    // Meter strip on the right edge (full height minus header)
    auto meterArea = area;
    meterArea.removeFromTop(HEADER_HEIGHT + 2);
    levelMeter_.setBounds(meterArea.removeFromRight(METER_WIDTH).reduced(1, 2));
    area.removeFromRight(METER_WIDTH);

    // Header
    auto headerRow = area.removeFromTop(HEADER_HEIGHT);
    int btnSize = HEADER_HEIGHT;

    layoutDeviceSlotHeaderRight(headerRow, btnSize, 2,
                                /*delete*/ &deleteButton_,
                                /*power*/ onButton_.get(),
                                /*multiOut*/ nullptr,
                                /*sc*/ nullptr,
                                /*slider*/ &gainSlider_, GAIN_SLIDER_WIDTH,
                                /*ui*/ uiButton_.get());

    nameLabel_.setBounds(headerRow);

    area.removeFromTop(2);

    // Content
    if (samplerUI_ && samplerUI_->isVisible()) {
        samplerUI_->setBounds(area);
    } else if (usingSharedInlineUi_) {
        auto contentArea = area.reduced(2, 0);
        if (compiledPanel_) {
            compiledPanel_->component().setBounds(contentArea);
        } else if (faustUI_) {
            if (faustCustomView_) {
                const auto customHeight =
                    juce::jmin(faustCustomView_->getPreferredHeight(), contentArea.getHeight() / 2);
                faustCustomView_->setBounds(contentArea.removeFromTop(customHeight));
            }
            faustUI_->setBounds(contentArea);
        } else if (customUI_ != nullptr) {
            if (auto* active = customUI_->getActiveUI())
                active->setBounds(contentArea);
        }
    } else if (paramSlots_[0] && paramSlots_[0]->isVisible()) {
        auto contentArea = area.reduced(2, 0);
        constexpr int paramCols = 8;
        constexpr int paramRows = 4;
        int cellWidth = contentArea.getWidth() / paramCols;
        int cellHeight = contentArea.getHeight() / paramRows;

        auto labelFont = FontManager::getInstance().getUIFont(
            DebugSettings::getInstance().getParamLabelFontSize());
        auto valueFont = FontManager::getInstance().getUIFont(
            DebugSettings::getInstance().getParamValueFontSize());

        for (int i = 0; i < PLUGIN_PARAM_SLOTS; ++i) {
            if (!paramSlots_[static_cast<size_t>(i)]->isVisible())
                continue;
            int row = i / paramCols;
            int col = i % paramCols;
            int x = contentArea.getX() + col * cellWidth;
            int y = contentArea.getY() + row * cellHeight;
            paramSlots_[static_cast<size_t>(i)]->setFonts(labelFont, valueFont);
            paramSlots_[static_cast<size_t>(i)]->setBounds(x, y, cellWidth - 2, cellHeight);
        }
    }
}

}  // namespace magda::daw::ui
