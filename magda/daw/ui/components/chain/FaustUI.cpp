#include "FaustUI.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include "FaustCodeEditorWindow.hpp"
#include "audio/FaustPlugin.hpp"
#include "audio/FaustResources.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace te = tracktion::engine;

FaustUI::FaustUI() {
    nameLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    nameLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(nameLabel_);

    errorLabel_.setFont(FontManager::getInstance().getMonoFont(10.0f));
    errorLabel_.setColour(juce::Label::textColourId, juce::Colours::red);
    errorLabel_.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(errorLabel_);

    loadButton_.onClick = [this] { showLoadMenu(); };
    addAndMakeVisible(loadButton_);

    editButton_.onClick = [this] { showCodeEditor(); };
    addAndMakeVisible(editButton_);

    startTimerHz(30);
}

FaustUI::~FaustUI() {
    stopTimer();
}

void FaustUI::setPlugin(magda::daw::audio::FaustPlugin* plugin) {
    plugin_ = plugin;
    rebuildFromPlugin();
}

void FaustUI::rebuildFromPlugin() {
    slots_.clear();
    if (!plugin_) {
        DBG("[FaustUI] rebuildFromPlugin: plugin_ is null");
        return;
    }

    nameLabel_.setText(plugin_->state.getProperty("dspName", juce::String()).toString(),
                       juce::dontSendNotification);

    auto params = plugin_->getAutomatableParameters();
    DBG("[FaustUI] rebuildFromPlugin: found " << params.size() << " params, bounds=("
        << getWidth() << "x" << getHeight() << ")");
    for (auto p : params) {
        auto slot = std::make_unique<ParamSlot>();
        slot->param = p;

        slot->label.setText(p->getParameterName(), juce::dontSendNotification);
        slot->label.setFont(FontManager::getInstance().getUIFont(9.0f));
        slot->label.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
        slot->label.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(slot->label);

        const auto& range = p->valueRange;
        slot->slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slot->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 14);
        slot->slider.setRange(range.start, range.end,
                              range.interval > 0.0f ? range.interval : 0.0001);
        slot->slider.setValue(p->getCurrentValue(), juce::dontSendNotification);

        auto* paramPtr = p;
        slot->slider.onValueChange = [paramPtr, s = slot.get()]() {
            paramPtr->setParameter(static_cast<float>(s->slider.getValue()),
                                   juce::sendNotificationSync);
        };
        addAndMakeVisible(slot->slider);

        slots_.push_back(std::move(slot));
    }

    resized();
}

bool FaustUI::tryLoad(const juce::String& name, const juce::String& source) {
    if (!plugin_)
        return false;
    juce::String err;
    if (!plugin_->loadDspSource(name, source, err)) {
        errorLabel_.setText(err, juce::dontSendNotification);
        return false;
    }
    errorLabel_.setText({}, juce::dontSendNotification);
    rebuildFromPlugin();
    return true;
}

void FaustUI::showLoadMenu() {
    if (!plugin_)
        return;

    juce::PopupMenu menu;
    auto starters = magda::daw::audio::getBundledStarterDsps();
    int id = 1;
    for (const auto& s : starters)
        menu.addItem(id++, s.name);
    menu.addSeparator();
    const int fromFileId = id;
    menu.addItem(fromFileId, "From file...");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&loadButton_),
        [this, starters, fromFileId](int result) {
            if (result <= 0)
                return;
            if (result == fromFileId) {
                loadFromFile();
                return;
            }
            const int idx = result - 1;
            if (idx < 0 || idx >= static_cast<int>(starters.size()))
                return;
            const auto& s = starters[static_cast<size_t>(idx)];
            tryLoad(s.name, s.source);
        });
}

void FaustUI::loadFromFile() {
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Choose a .dsp file",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.dsp");
    fileChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (!file.existsAsFile() || !plugin_)
                return;
            tryLoad(file.getFileNameWithoutExtension(), file.loadFileAsString());
        });
}

void FaustUI::showCodeEditor() {
    if (!plugin_)
        return;
    if (editorWindow_) {
        editorWindow_->setVisible(true);
        editorWindow_->toFront(true);
        return;
    }
    const auto title =
        "Faust DSP — " + plugin_->state.getProperty("dspName", juce::String()).toString();
    const auto source = plugin_->state.getProperty("dspSource", juce::String()).toString();
    editorWindow_ = std::make_unique<FaustCodeEditorWindow>(
        title, source,
        [this](const juce::String& src, juce::String& err) -> bool {
            if (!plugin_)
                return false;
            const auto editedName =
                plugin_->state.getProperty("dspName", juce::String("Custom")).toString();
            if (!plugin_->loadDspSource(editedName, src, err))
                return false;
            errorLabel_.setText({}, juce::dontSendNotification);
            rebuildFromPlugin();
            return true;
        });
}

void FaustUI::timerCallback() {
    // Pull external changes (automation playback, project load) back into the
    // sliders. dontSendNotification avoids re-firing the onValueChange writer.
    for (auto& s : slots_) {
        if (!s->param || s->slider.isMouseButtonDown())
            continue;
        const float v = s->param->getCurrentValue();
        if (std::abs(v - static_cast<float>(s->slider.getValue())) > 1e-6f)
            s->slider.setValue(v, juce::dontSendNotification);
    }
}

void FaustUI::paint(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds().reduced(1));
}

void FaustUI::resized() {
    auto area = getLocalBounds().reduced(6);

    auto header = area.removeFromTop(20);
    editButton_.setBounds(header.removeFromRight(50));
    header.removeFromRight(4);
    loadButton_.setBounds(header.removeFromRight(60));
    nameLabel_.setBounds(header.reduced(4, 0));

    if (errorLabel_.getText().isNotEmpty())
        errorLabel_.setBounds(area.removeFromBottom(28));

    if (slots_.empty())
        return;

    const int labelHeight = 14;
    const int sliderHeight = 18;
    const int n = static_cast<int>(slots_.size());
    const int colWidth = area.getWidth() / n;

    for (int i = 0; i < n; ++i) {
        auto col = area.removeFromLeft(colWidth).reduced(2, 0);
        slots_[i]->label.setBounds(col.removeFromTop(labelHeight));
        slots_[i]->slider.setBounds(col.removeFromTop(sliderHeight + 16));
    }
}

}  // namespace magda::daw::ui
