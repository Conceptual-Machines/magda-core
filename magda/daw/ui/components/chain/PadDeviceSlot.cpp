#include "PadDeviceSlot.hpp"

#include <BinaryData.h>
#include <tracktion_engine/tracktion_engine.h>

#include "audio/MagdaSamplerPlugin.hpp"
#include "ui/debug/DebugSettings.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace te = tracktion::engine;

namespace magda::daw::ui {

PadDeviceSlot::PadDeviceSlot() {
    nameLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    nameLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(nameLabel_);

    deleteButton_.setButtonText(juce::CharPointer_UTF8("\xc3\x97"));  // multiplication sign
    deleteButton_.setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));
    deleteButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    deleteButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    deleteButton_.onClick = [this]() {
        if (onDeleteClicked)
            onDeleteClicked();
    };
    addAndMakeVisible(deleteButton_);

    // UI button to open plugin native window
    uiButton_ = std::make_unique<magda::SvgButton>("UI", BinaryData::open_in_new_svg,
                                                   BinaryData::open_in_new_svgSize);
    uiButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    uiButton_->setHoverColor(DarkTheme::getTextColour());
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

    // Create param slots
    for (int i = 0; i < PLUGIN_PARAM_SLOTS; ++i) {
        paramSlots_[static_cast<size_t>(i)] = std::make_unique<ParamSlotComponent>(i);
        addChildComponent(*paramSlots_[static_cast<size_t>(i)]);
    }
}

PadDeviceSlot::~PadDeviceSlot() {
    deleteButton_.setLookAndFeel(nullptr);
}

void PadDeviceSlot::setPlugin(te::Plugin* plugin) {
    plugin_ = plugin;
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

    // Update on button state
    onButton_->setToggleState(plugin->isEnabled(), juce::dontSendNotification);
    onButton_->setActive(plugin->isEnabled());

    resized();
}

void PadDeviceSlot::setSampler(daw::audio::MagdaSamplerPlugin* sampler) {
    plugin_ = sampler;
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
    nameLabel_.setText("", juce::dontSendNotification);
    samplerUI_.reset();
    for (auto& slot : paramSlots_)
        if (slot)
            slot->setVisible(false);
    uiButton_->setVisible(false);
}

int PadDeviceSlot::getPreferredWidth() const {
    return SLOT_WIDTH;
}

void PadDeviceSlot::setupForSampler(daw::audio::MagdaSamplerPlugin* sampler) {
    // Hide param slots
    for (auto& slot : paramSlots_)
        if (slot)
            slot->setVisible(false);
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
        if (paramIndex >= 0 && paramIndex < params.size())
            params[paramIndex]->setParameter(value, juce::sendNotification);
    };

    samplerUI_->onLoopEnabledChanged = [sampler](bool enabled) {
        sampler->loopEnabledAtomic.store(enabled, std::memory_order_relaxed);
        sampler->loopEnabledValue = enabled;
    };

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
        sampler->levelValue.get(), sampler->sampleStartValue.get(), sampler->loopEnabledValue.get(),
        sampler->loopStartValue.get(), sampler->loopEndValue.get(), sampler->velAmountValue.get(),
        sampleName);

    samplerUI_->setWaveformData(sampler->getWaveform(), sampler->getSampleRate(),
                                sampler->getSampleLengthSeconds());

    samplerUI_->setVisible(true);
}

void PadDeviceSlot::setupForExternalPlugin(te::Plugin* plugin) {
    // Hide SamplerUI
    if (samplerUI_)
        samplerUI_->setVisible(false);

    nameLabel_.setText(plugin->getName(), juce::dontSendNotification);

    // Show UI button for external plugins
    uiButton_->setVisible(true);
    uiButton_->onClick = [plugin]() {
        if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin)) {
            if (ext->windowState)
                ext->windowState->showWindowExplicitly();
        } else {
            plugin->showWindowExplicitly();
        }
    };

    // Populate param slots
    auto params = plugin->getAutomatableParameters();
    for (int i = 0; i < PLUGIN_PARAM_SLOTS; ++i) {
        auto& slot = paramSlots_[static_cast<size_t>(i)];
        if (i < params.size()) {
            auto* param = params[i];
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
}

void PadDeviceSlot::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    g.fillRoundedRectangle(bounds.toFloat(), 3.0f);

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRoundedRectangle(bounds.toFloat(), 3.0f, 0.5f);
}

void PadDeviceSlot::resized() {
    auto area = getLocalBounds().reduced(2);

    // Header
    auto headerRow = area.removeFromTop(HEADER_HEIGHT);
    int btnSize = HEADER_HEIGHT;

    deleteButton_.setBounds(headerRow.removeFromRight(btnSize));
    headerRow.removeFromRight(2);

    onButton_->setBounds(headerRow.removeFromRight(btnSize));
    headerRow.removeFromRight(2);

    if (uiButton_->isVisible()) {
        uiButton_->setBounds(headerRow.removeFromRight(btnSize));
        headerRow.removeFromRight(2);
    }

    nameLabel_.setBounds(headerRow);

    area.removeFromTop(2);

    // Content
    if (samplerUI_ && samplerUI_->isVisible()) {
        samplerUI_->setBounds(area);
    } else if (paramSlots_[0] && paramSlots_[0]->isVisible()) {
        auto contentArea = area.reduced(2, 0);
        constexpr int paramCols = 4;
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
