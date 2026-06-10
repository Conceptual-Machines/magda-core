#include "NodeComponent.hpp"

#include <BinaryData.h>

#include "../../utils/SelectionPolicy.hpp"
#include "ai/AIPanelComponent.hpp"
#include "core/AutomationInfo.hpp"
#include "core/LinkModeManager.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackManager.hpp"
#include "core/controllers/ControllerActivation.hpp"
#include "modulation/MacroEditorPanel.hpp"
#include "modulation/MacroPanelComponent.hpp"
#include "modulation/ModsPanelComponent.hpp"
#include "modulation/ModulatorEditorPanel.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

namespace {
juce::String encodeStepTypes(const magda::ChainNodePath& path) {
    juce::StringArray values;
    for (const auto& step : path.steps)
        values.add(juce::String(static_cast<int>(step.type)));
    return values.joinIntoString(",");
}

juce::String encodeStepIds(const magda::ChainNodePath& path) {
    juce::StringArray values;
    for (const auto& step : path.steps)
        values.add(juce::String(step.id));
    return values.joinIntoString(",");
}

void writePathToDragInfo(juce::DynamicObject& obj, const magda::ChainNodePath& path,
                         const juce::String& suffix = {}) {
    obj.setProperty("trackId" + suffix, path.trackId);
    obj.setProperty("topLevelDeviceId" + suffix, path.topLevelDeviceId);
    obj.setProperty("isTrackLevel" + suffix, path.isTrackLevel);
    obj.setProperty("stepTypes" + suffix, encodeStepTypes(path));
    obj.setProperty("stepIds" + suffix, encodeStepIds(path));
}

juce::Image createChainNodeDragImage(const juce::String& label, int itemCount) {
    constexpr int width = 188;
    constexpr int height = 42;
    juce::Image image(juce::Image::ARGB, width, height, true);
    juce::Graphics g(image);

    auto bounds = image.getBounds().toFloat().reduced(1.0f);
    const auto accent = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);
    const auto bg = DarkTheme::getColour(DarkTheme::SURFACE).withAlpha(0.92f);

    g.setColour(bg);
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(accent.withAlpha(0.95f));
    g.drawRoundedRectangle(bounds, 6.0f, 1.5f);
    g.fillRoundedRectangle(bounds.removeFromLeft(5.0f), 3.0f);

    auto textArea = image.getBounds().reduced(12, 6);
    textArea.removeFromLeft(6);
    g.setColour(juce::Colours::white.withAlpha(0.95f));
    g.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
    g.drawFittedText(label.isNotEmpty() ? label : "Chain Item", textArea.removeFromTop(17),
                     juce::Justification::centredLeft, 1);

    const auto detail = itemCount == 1 ? "1 item" : juce::String(itemCount) + " items";
    g.setColour(juce::Colours::white.withAlpha(0.68f));
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawFittedText(detail, textArea, juce::Justification::centredLeft, 1);

    return image;
}
}  // namespace

struct NodeComponent::PanelFadeTimer : private juce::Timer {
    void fadeIn(const std::vector<juce::Component*>& components, int durationMs,
                std::function<void(float)> onProgress = nullptr) {
        stopTimer();
        targets_.clear();
        onComplete_ = nullptr;
        onProgress_ = std::move(onProgress);
        durationMs_ = juce::jmax(1, durationMs);
        startMs_ = juce::Time::getMillisecondCounterHiRes();
        startAlpha_ = 0.0f;
        targetAlpha_ = 1.0f;
        notifyProgress(startAlpha_);

        for (auto* component : components) {
            if (component == nullptr)
                continue;
            component->setAlpha(startAlpha_);
            component->setVisible(true);
            targets_.push_back(component);
        }

        if (targets_.empty()) {
            notifyProgress(targetAlpha_);
            onProgress_ = nullptr;
            return;
        }

        startTimerHz(60);
    }

    void fadeOut(const std::vector<juce::Component*>& components, int durationMs,
                 std::function<void()> onComplete,
                 std::function<void(float)> onProgress = nullptr) {
        stopTimer();
        targets_.clear();
        onComplete_ = std::move(onComplete);
        onProgress_ = std::move(onProgress);
        durationMs_ = juce::jmax(1, durationMs);
        startMs_ = juce::Time::getMillisecondCounterHiRes();
        startAlpha_ = 1.0f;
        targetAlpha_ = 0.0f;
        notifyProgress(startAlpha_);

        for (auto* component : components) {
            if (component == nullptr)
                continue;
            component->setAlpha(startAlpha_);
            component->setVisible(true);
            targets_.push_back(component);
        }

        if (targets_.empty()) {
            finish();
            return;
        }

        startTimerHz(60);
    }

    void cancel() {
        stopTimer();
        for (auto& target : targets_) {
            if (auto* component = target.getComponent())
                component->setAlpha(1.0f);
        }
        targets_.clear();
        onComplete_ = nullptr;
        notifyProgress(1.0f);
        onProgress_ = nullptr;
    }

  private:
    void notifyProgress(float alpha) {
        if (onProgress_)
            onProgress_(alpha);
    }

    void timerCallback() override {
        const auto elapsed = juce::Time::getMillisecondCounterHiRes() - startMs_;
        const auto progress = juce::jlimit(0.0, 1.0, elapsed / static_cast<double>(durationMs_));
        const auto alpha =
            startAlpha_ + (targetAlpha_ - startAlpha_) * static_cast<float>(progress);

        for (auto& target : targets_) {
            if (auto* component = target.getComponent())
                component->setAlpha(alpha);
        }
        notifyProgress(alpha);

        if (progress >= 1.0)
            finish();
    }

    void finish() {
        stopTimer();
        for (auto& target : targets_) {
            if (auto* component = target.getComponent())
                component->setAlpha(targetAlpha_);
        }
        notifyProgress(targetAlpha_);
        targets_.clear();
        auto onComplete = std::move(onComplete_);
        onComplete_ = nullptr;
        onProgress_ = nullptr;
        if (onComplete)
            onComplete();
    }

    std::vector<juce::Component::SafePointer<juce::Component>> targets_;
    std::function<void()> onComplete_;
    std::function<void(float)> onProgress_;
    double startMs_ = 0.0;
    int durationMs_ = 1;
    float startAlpha_ = 0.0f;
    float targetAlpha_ = 1.0f;
};

NodeComponent::NodeComponent() {
    paramPanelFadeTimer_ = std::make_unique<PanelFadeTimer>();
    modPanelFadeTimer_ = std::make_unique<PanelFadeTimer>();

    // Register as SelectionManager listener for centralized selection
    magda::SelectionManager::getInstance().addListener(this);
    // Listen for binding/controller changes so the header dot reflects the
    // live automap state. Both registries can change asynchronously
    // (controller plug/unplug, profile reload, MIDI Learn).
    magda::BindingRegistry::getInstance().addListener(this);
    magda::ControllerRegistry::getInstance().addListener(this);
    // === HEADER ===

    // Bypass button (power icon)
    bypassButton_ = std::make_unique<magda::SvgButton>("Power", BinaryData::power_on_svg,
                                                       BinaryData::power_on_svgSize);
    bypassButton_->setClickingTogglesState(true);
    bypassButton_->setNormalColor(DarkTheme::getColour(DarkTheme::STATUS_ERROR));
    bypassButton_->setActiveColor(juce::Colours::white);
    bypassButton_->setActiveBackgroundColor(
        DarkTheme::getColour(DarkTheme::ACCENT_GREEN).darker(0.3f));
    bypassButton_->setActive(true);  // Default: not bypassed = active
    bypassButton_->onClick = [this]() {
        bool bypassed = !bypassButton_->getToggleState();  // Toggle OFF = bypassed
        bypassButton_->setActive(!bypassed);
        if (onBypassChanged) {
            onBypassChanged(bypassed);
        }
    };
    addAndMakeVisible(*bypassButton_);

    // Name label - clicks pass through for selection
    nameLabel_.setFont(FontManager::getInstance().getUIFontBold(10.0f));
    nameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    nameLabel_.setJustificationType(juce::Justification::centredLeft);
    nameLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(nameLabel_);

    // Delete button (reddish-purple background)
    deleteButton_.setButtonText(juce::String::fromUTF8("\xc3\x97"));  // × symbol
    deleteButton_.setColour(
        juce::TextButton::buttonColourId,
        DarkTheme::getColour(DarkTheme::ACCENT_PURPLE)
            .interpolatedWith(DarkTheme::getColour(DarkTheme::STATUS_ERROR), 0.5f)
            .darker(0.2f));
    deleteButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    deleteButton_.onClick = [this]() {
        if (onDeleteClicked) {
            onDeleteClicked();
        }
    };
    deleteButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addAndMakeVisible(deleteButton_);

    // === MOD PANEL CONTROLS ===
    for (int i = 0; i < 3; ++i) {
        modSlotButtons_[i] = std::make_unique<juce::TextButton>("+");
        modSlotButtons_[i]->setColour(juce::TextButton::buttonColourId,
                                      DarkTheme::getColour(DarkTheme::SURFACE));
        modSlotButtons_[i]->setColour(juce::TextButton::textColourOffId,
                                      DarkTheme::getSecondaryTextColour());
        modSlotButtons_[i]->onClick = [this, i]() {
            juce::PopupMenu menu;
            menu.addItem(1, "LFO");
            menu.addItem(2, "Bezier LFO");
            menu.addItem(3, "ADSR");
            menu.addItem(4, "Envelope Follower");
            menu.showMenuAsync(juce::PopupMenu::Options(), [this, i](int result) {
                if (result > 0) {
                    juce::StringArray types = {"", "LFO", "BEZ", "ADSR", "ENV"};
                    modSlotButtons_[i]->setButtonText(types[result]);
                }
            });
        };
        addChildComponent(*modSlotButtons_[i]);
    }

    // === PARAM PANEL CONTROLS ===
    for (int i = 0; i < 4; ++i) {
        auto knob = std::make_unique<juce::Slider>();
        knob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        knob->setRange(0.0, 1.0, 0.01);
        knob->setValue(0.5);
        knob->setColour(juce::Slider::rotarySliderFillColourId,
                        DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
        knob->setColour(juce::Slider::rotarySliderOutlineColourId,
                        DarkTheme::getColour(DarkTheme::SURFACE));
        addChildComponent(*knob);
        paramKnobs_.push_back(std::move(knob));
    }
}

NodeComponent::~NodeComponent() {
    magda::SelectionManager::getInstance().removeListener(this);
    magda::BindingRegistry::getInstance().removeListener(this);
    magda::ControllerRegistry::getInstance().removeListener(this);
}

void NodeComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // When collapsed, draw a narrow vertical strip with rotated name
    // BUT still draw side panels if visible
    if (collapsed_) {
        // === LEFT SIDE PANELS (even when collapsed): [Macros][MacroEditor][Mods][ModEditor] ===
        if (isParamPanelLaidOut()) {
            auto paramArea = bounds.removeFromLeft(getParamPanelWidth());
            g.saveState();
            g.setOpacity(paramPanelAlpha_);
            g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
            g.fillRect(paramArea);
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.drawRect(paramArea);
            paintParamPanel(g, paramArea);
            g.restoreState();
        }

        // Macro editor panel - after macros, before mods
        int extraRightWidthCollapsed = getExtraRightPanelWidth();
        if (extraRightWidthCollapsed > 0) {
            auto extraRightArea = bounds.removeFromLeft(extraRightWidthCollapsed);
            g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
            g.fillRect(extraRightArea);
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.drawRect(extraRightArea);
            paintExtraRightPanel(g, extraRightArea);
        }

        if (isModPanelLaidOut()) {
            auto modArea = bounds.removeFromLeft(getModPanelWidth());
            g.saveState();
            g.setOpacity(modPanelAlpha_);
            g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
            g.fillRect(modArea);
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.drawRect(modArea);
            paintModPanel(g, modArea);
            g.restoreState();
        }

        // Mod editor panel - after mods, before main content
        int extraWidthCollapsed = getExtraLeftPanelWidth();
        if (extraWidthCollapsed > 0) {
            auto extraArea = bounds.removeFromLeft(extraWidthCollapsed);
            g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
            g.fillRect(extraArea);
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.drawRect(extraArea);
            paintExtraLeftPanel(g, extraArea);
        }

        // AI panel - after mod editor, before main content
        if (aiPanelVisible_) {
            auto aiArea = bounds.removeFromLeft(getAIPanelWidth());
            g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
            g.fillRect(aiArea);
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.drawRect(aiArea);
            paintAIPanel(g, aiArea);
        }

        // === RIGHT SIDE PANEL (even when collapsed) ===
        if (gainPanelVisible_) {
            auto gainArea = bounds.removeFromRight(getGainPanelWidth());
            g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
            g.fillRect(gainArea);
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.drawRect(gainArea);
            paintGainPanel(g, gainArea);
        }

        // === COLLAPSED MAIN STRIP (remaining bounds) ===
        // Background
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.03f));
        g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

        // Border
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(bounds.toFloat(), 4.0f, 1.0f);

        // Draw name vertically (rotated 90 degrees) in the text area below buttons
        g.saveState();
        g.setColour(DarkTheme::getTextColour());
        g.setFont(FontManager::getInstance().getUIFontBold(10.0f));

        auto center = collapsedTextArea_.getCentre().toFloat();
        g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi,
                                                       center.x, center.y));
        // Swapped width/height due to rotation
        juce::Rectangle<int> textBounds(
            static_cast<int>(center.x - collapsedTextArea_.getHeight() / 2),
            static_cast<int>(center.y - collapsedTextArea_.getWidth() / 2),
            collapsedTextArea_.getHeight(), collapsedTextArea_.getWidth());
        g.drawText(getCollapsedName(), textBounds, juce::Justification::centred);
        g.restoreState();

        // Dim/selection drawn in paintOverChildren
        return;
    }

    // === LEFT SIDE PANELS: [Macros][MacroEditor][Mods][ModEditor] (squared corners) ===
    if (isParamPanelLaidOut()) {
        auto paramArea = bounds.removeFromLeft(getParamPanelWidth());
        g.saveState();
        g.setOpacity(paramPanelAlpha_);
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
        g.fillRect(paramArea);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRect(paramArea);
        paintParamPanel(g, paramArea);
        g.restoreState();
    }

    // Macro editor panel - after macros, before mods
    int extraRightWidth = getExtraRightPanelWidth();
    if (extraRightWidth > 0) {
        auto extraRightArea = bounds.removeFromLeft(extraRightWidth);
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
        g.fillRect(extraRightArea);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRect(extraRightArea);
        paintExtraRightPanel(g, extraRightArea);
    }

    if (isModPanelLaidOut()) {
        auto modArea = bounds.removeFromLeft(getModPanelWidth());
        g.saveState();
        g.setOpacity(modPanelAlpha_);
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
        g.fillRect(modArea);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRect(modArea);
        paintModPanel(g, modArea);
        g.restoreState();
    }

    // Mod editor panel - after mods, before main content
    int extraWidth = getExtraLeftPanelWidth();
    if (extraWidth > 0) {
        auto extraArea = bounds.removeFromLeft(extraWidth);
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
        g.fillRect(extraArea);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRect(extraArea);
        paintExtraLeftPanel(g, extraArea);
    }

    // AI panel — sits between the mod editor and the main content
    if (aiPanelVisible_) {
        auto aiArea = bounds.removeFromLeft(getAIPanelWidth());
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
        g.fillRect(aiArea);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRect(aiArea);
        paintAIPanel(g, aiArea);
    }

    // === RIGHT SIDE PANEL: [Gain] (squared corners) ===
    if (gainPanelVisible_) {
        auto gainArea = bounds.removeFromRight(getGainPanelWidth());
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.02f));
        g.fillRect(gainArea);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRect(gainArea);
        paintGainPanel(g, gainArea);
    }

    // === MAIN NODE AREA (remaining bounds) ===
    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.03f));
    g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRoundedRectangle(bounds.toFloat(), 4.0f, 1.0f);

    // Header separator (only if header visible)
    int headerHeight = getHeaderHeight();
    if (headerHeight > 0) {
        g.drawHorizontalLine(headerHeight, static_cast<float>(bounds.getX()),
                             static_cast<float>(bounds.getRight()));
    }

    // Calculate content area (below header)
    auto contentArea = bounds;
    contentArea.removeFromTop(headerHeight);

    // Let subclass paint main content
    paintContent(g, contentArea);

    // Dim/selection drawn in paintOverChildren so they appear above side panels
}

void NodeComponent::paintOverChildren(juce::Graphics& g) {
    // Dim if bypassed or frozen (over everything including side panels)
    if (!bypassButton_->getToggleState() || frozen_) {  // Toggle OFF = bypassed
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    }

    // Controller-binding indicator dots, drawn next to the node name. Two
    // dots side-by-side when both apply: automap (green) then pinned
    // (orange). Anchor comes from getControllerIndicatorAnchor() so
    // subclasses with a custom logo (Drum Grid's "MDG2000") can place the
    // dot relative to their own visible text rather than the empty
    // nameLabel_. A negative-x anchor suppresses dot painting.
    if (hasAutomapBindings_ || hasPinnedBindings_) {
        auto anchor = getControllerIndicatorAnchor();
        if (anchor.x >= 0.0f) {
            constexpr float dotSize = 6.0f;
            constexpr float gapBetweenDots = 5.0f;

            float x = anchor.x;
            float y = anchor.y - dotSize * 0.5f;

            if (hasAutomapBindings_) {
                g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(0.95f));
                g.fillEllipse(x, y, dotSize, dotSize);
                x += dotSize + gapBetweenDots;
            }
            if (hasPinnedBindings_) {
                g.setColour(juce::Colour(0xFFFF6B35).withAlpha(0.9f));
                g.fillEllipse(x, y, dotSize, dotSize);
            }
        }
    }

    // Selection border (over everything including side panels)
    if (selected_) {
        g.setColour(juce::Colour(0xff888888));  // Grey
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f, 2.0f);
    }
}

void NodeComponent::resized() {
    auto bounds = getLocalBounds();

    // When collapsed (narrow width), arrange key icons vertically
    // BUT still layout side panels if visible
    if (collapsed_) {
        // === LEFT SIDE PANELS (even when collapsed): [Macros][MacroEditor][Mods][ModEditor] ===
        if (isParamPanelLaidOut()) {
            auto paramArea = bounds.removeFromLeft(getParamPanelWidth());
            resizedParamPanel(paramArea);
        } else {
            // Hide param knobs when panel is not visible
            for (auto& knob : paramKnobs_) {
                knob->setVisible(false);
            }
            // Hide the real macro panel if it exists
            if (macroPanel_)
                macroPanel_->setVisible(false);
        }

        // Macro editor panel - after macros, before mods
        int extraRightWidthCollapsed = getExtraRightPanelWidth();
        if (extraRightWidthCollapsed > 0) {
            auto extraRightArea = bounds.removeFromLeft(extraRightWidthCollapsed);
            resizedExtraRightPanel(extraRightArea);
        }

        if (isModPanelLaidOut()) {
            auto modArea = bounds.removeFromLeft(getModPanelWidth());
            resizedModPanel(modArea);
        } else {
            // Hide mod slot buttons when panel is not visible
            for (auto& btn : modSlotButtons_) {
                if (btn)
                    btn->setVisible(false);
            }
            // Hide the real mods panel if it exists
            if (modsPanel_)
                modsPanel_->setVisible(false);
        }

        // Mod editor panel - after mods, before main content
        int extraWidthCollapsed = getExtraLeftPanelWidth();
        if (extraWidthCollapsed > 0) {
            auto extraArea = bounds.removeFromLeft(extraWidthCollapsed);
            resizedExtraLeftPanel(extraArea);
        }

        // AI panel - after mod editor, before main content
        if (aiPanelVisible_) {
            auto aiArea = bounds.removeFromLeft(getAIPanelWidth());
            resizedAIPanel(aiArea);
        } else if (aiPanel_) {
            aiPanel_->setVisible(false);
        }

        // === RIGHT SIDE PANEL (even when collapsed) ===
        if (gainPanelVisible_) {
            auto gainArea = bounds.removeFromRight(getGainPanelWidth());
            resizedGainPanel(gainArea);
        }

        // === COLLAPSED MAIN STRIP (remaining bounds) ===
        nameLabel_.setVisible(false);

        // Reserve meter strip on the right (subclasses override getCollapsedMeterWidth)
        auto area = bounds.reduced(4);
        int collapsedMeter = getCollapsedMeterWidth();
        collapsedMeterArea_ = {};
        if (collapsedMeter > 0) {
            collapsedMeterArea_ = area.removeFromRight(collapsedMeter).reduced(0, 2);
            area.removeFromRight(2);
        }

        // Arrange buttons vertically at top of collapsed strip
        int buttonSize = juce::jmin(BUTTON_SIZE, area.getWidth() - 4);

        // Delete button at top (always visible)
        deleteButton_.setBounds(
            area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
        deleteButton_.setVisible(true);
        area.removeFromTop(4);

        // Bypass button below delete (only if it was visible - devices use their own)
        if (bypassButton_->isVisible()) {
            bypassButton_->setBounds(
                area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
            area.removeFromTop(4);
        }

        // Let subclass add extra collapsed buttons
        resizedCollapsed(area);

        // Store remaining area for rotated name text
        collapsedTextArea_ = area;

        // Call resizedContent with empty area so subclasses can hide their content
        resizedContent(juce::Rectangle<int>());
        return;
    }

    // === LEFT SIDE PANELS: [Macros][MacroEditor][Mods][ModEditor] ===
    if (isParamPanelLaidOut()) {
        auto paramArea = bounds.removeFromLeft(getParamPanelWidth());
        resizedParamPanel(paramArea);
    } else {
        // Hide param knobs when panel is not visible
        for (auto& knob : paramKnobs_) {
            knob->setVisible(false);
        }
        // Hide the real macro panel if it exists
        if (macroPanel_)
            macroPanel_->setVisible(false);
    }

    // Macro editor panel - after macros, before mods
    int extraRightWidth = getExtraRightPanelWidth();
    if (extraRightWidth > 0) {
        auto extraRightArea = bounds.removeFromLeft(extraRightWidth);
        resizedExtraRightPanel(extraRightArea);
    }

    if (isModPanelLaidOut()) {
        auto modArea = bounds.removeFromLeft(getModPanelWidth());
        resizedModPanel(modArea);
    } else {
        // Hide mod slot buttons when panel is not visible
        for (auto& btn : modSlotButtons_) {
            if (btn)
                btn->setVisible(false);
        }
        // Hide the real mods panel if it exists
        if (modsPanel_)
            modsPanel_->setVisible(false);
    }

    // Mod editor panel - after mods, before main content
    int extraWidth = getExtraLeftPanelWidth();
    if (extraWidth > 0) {
        auto extraArea = bounds.removeFromLeft(extraWidth);
        resizedExtraLeftPanel(extraArea);
    }

    // AI panel — between mod editor and main content
    if (aiPanelVisible_) {
        auto aiArea = bounds.removeFromLeft(getAIPanelWidth());
        resizedAIPanel(aiArea);
    } else if (aiPanel_) {
        // Without this, the panel keeps painting at its last bounds after the
        // AI button is toggled off — the side strip is gone, but the output
        // text / input box / footer linger over the device's main content.
        aiPanel_->setVisible(false);
    }

    // === RIGHT SIDE PANEL: [Gain] ===
    if (gainPanelVisible_) {
        auto gainArea = bounds.removeFromRight(getGainPanelWidth());
        resizedGainPanel(gainArea);
    }

    // === MAIN NODE AREA (remaining bounds) ===

    // Reserve meter strip on the right edge (subclasses override getMeterWidth())
    int meterWidth = getMeterWidth();
    if (meterWidth > 0)
        bounds.removeFromRight(meterWidth);

    // === HEADER: [B] Name ... [X] === (only if header visible)
    int headerHeight = getHeaderHeight();
    if (headerHeight > 0) {
        auto headerArea = bounds.removeFromTop(headerHeight).reduced(3, 2);

        // Delete button on far right (if visible)
        if (deleteButton_.isVisible()) {
            deleteButton_.setBounds(headerArea.removeFromRight(BUTTON_SIZE));
            headerArea.removeFromRight(4);
        }

        // Power slot — defaults to base's bypassButton_, but subclasses can
        // substitute a custom-styled button via getHeaderPowerButton().
        if (auto* power = getHeaderPowerButton(); power != nullptr && power->isVisible()) {
            power->setBounds(headerArea.removeFromRight(BUTTON_SIZE));
            headerArea.removeFromRight(4);
        }

        // Preset slot — base class reserves the position so the right-edge
        // icon order is locked to [preset][power][delete] and a subclass
        // can't accidentally tuck a button between them in
        // resizedHeaderExtra.
        if (auto* preset = getHeaderPresetButton(); preset != nullptr && preset->isVisible()) {
            preset->setBounds(headerArea.removeFromRight(BUTTON_SIZE));
            headerArea.removeFromRight(4);
        }

        // Let subclass add extra header buttons
        resizedHeaderExtra(headerArea);

        nameLabel_.setBounds(headerArea);
        nameLabel_.setVisible(true);
    } else {
        // Hide header controls
        bypassButton_->setVisible(false);
        deleteButton_.setVisible(false);
        nameLabel_.setVisible(false);
    }

    // === CONTENT (remaining area) ===
    // Reduce by 2 horizontally, 1 vertically to keep border visible
    auto contentArea = bounds.reduced(2, 1);
    resizedContent(contentArea);
}

void NodeComponent::setNodeName(const juce::String& name) {
    nameLabel_.setText(name, juce::dontSendNotification);
}

void NodeComponent::setNodeNameFont(const juce::Font& font) {
    nameLabel_.setFont(font);
}

juce::String NodeComponent::getNodeName() const {
    return nameLabel_.getText();
}

void NodeComponent::setBypassed(bool bypassed) {
    bypassButton_->setToggleState(!bypassed, juce::dontSendNotification);  // Active = not bypassed
    bypassButton_->setActive(!bypassed);
    repaint();  // Redraw bypass overlay in paintOverChildren
}

bool NodeComponent::isBypassed() const {
    return !bypassButton_->getToggleState();  // Toggle OFF = bypassed
}

void NodeComponent::setFrozen(bool frozen) {
    if (frozen_ == frozen)
        return;
    frozen_ = frozen;
    // Disable all child components so params can't be edited
    for (auto* child : getChildren()) {
        child->setEnabled(!frozen);
    }
    repaint();
}

void NodeComponent::setModPanelVisible(bool visible) {
    if (modPanelVisible_ != visible) {
        const bool opening = visible;
        if (opening) {
            cancelModPanelContentFade();
            retainModPanelForFadeOut_ = false;
            modPanelAlpha_ = 0.0f;
        } else {
            retainModPanelForFadeOut_ = true;
            modPanelAlpha_ = 1.0f;
        }
        modPanelVisible_ = visible;

        // When hiding the mod panel, also hide the modulator editor
        if (!visible && modulatorEditorVisible_) {
            hideModulatorEditor();
        }

        resized();
        repaint();
        auto safeThis = juce::Component::SafePointer<NodeComponent>(this);
        if (safeThis != nullptr && opening)
            safeThis->fadeInModPanelContent();
        if (safeThis != nullptr && !opening)
            safeThis->fadeOutModPanelContent();
        if (onModPanelToggled) {
            onModPanelToggled(modPanelVisible_);
        }
        if (onLayoutChanged) {
            onLayoutChanged();
        }
    }
}

void NodeComponent::setParamPanelVisible(bool visible) {
    if (paramPanelVisible_ != visible) {
        const bool opening = visible;
        DBG("NodeComponent::setParamPanelVisible - changing from "
            << (paramPanelVisible_ ? "visible" : "hidden") << " to "
            << (visible ? "visible" : "hidden"));
        if (opening) {
            cancelParamPanelContentFade();
            retainParamPanelForFadeOut_ = false;
            paramPanelAlpha_ = 0.0f;
        } else {
            retainParamPanelForFadeOut_ = true;
            paramPanelAlpha_ = 1.0f;
        }
        paramPanelVisible_ = visible;

        // When hiding the macro panel, also hide the macro editor
        if (!visible && macroEditorVisible_) {
            hideMacroEditor();
        }

        resized();
        repaint();
        auto safeThis = juce::Component::SafePointer<NodeComponent>(this);
        if (safeThis != nullptr && opening) {
            safeThis->fadeInParamPanelContent();
        }
        if (safeThis != nullptr && !opening) {
            safeThis->fadeOutParamPanelContent();
        }
        if (onParamPanelToggled) {
            onParamPanelToggled(paramPanelVisible_);
        }
        if (onLayoutChanged) {
            onLayoutChanged();
        }
    }
}

void NodeComponent::setGainPanelVisible(bool visible) {
    if (gainPanelVisible_ != visible) {
        gainPanelVisible_ = visible;
        if (onGainPanelToggled) {
            onGainPanelToggled(gainPanelVisible_);
        }
        resized();
        repaint();
        if (onLayoutChanged) {
            onLayoutChanged();
        }
    }
}

void NodeComponent::setAIPanelVisible(bool visible) {
    if (aiPanelVisible_ != visible) {
        aiPanelVisible_ = visible;
        if (onAIPanelToggled) {
            onAIPanelToggled(aiPanelVisible_);
        }
        resized();
        repaint();
        if (onLayoutChanged) {
            onLayoutChanged();
        }
    }
}

void NodeComponent::setBypassButtonVisible(bool visible) {
    bypassButton_->setVisible(visible);
}

void NodeComponent::setDeleteButtonVisible(bool visible) {
    deleteButton_.setVisible(visible);
}

void NodeComponent::paintContent(juce::Graphics& /*g*/, juce::Rectangle<int> /*contentArea*/) {
    // Default: nothing - subclasses override
}

void NodeComponent::resizedContent(juce::Rectangle<int> /*contentArea*/) {
    // Default: nothing - subclasses override
}

void NodeComponent::resizedHeaderExtra(juce::Rectangle<int>& /*headerArea*/) {
    // Default: nothing - subclasses override to add extra header buttons
}

int NodeComponent::getLeftPanelsWidth() const {
    int width = 0;
    if (isModPanelLaidOut())
        width += getModPanelWidth();
    width += getExtraLeftPanelWidth();  // Extra left panel (e.g., mod editor)
    if (isParamPanelLaidOut())
        width += getParamPanelWidth();
    width += getExtraRightPanelWidth();  // Extra "right" panel (e.g., macro editor) - still left of
                                         // main content
    if (aiPanelVisible_)
        width += getAIPanelWidth();
    return width;
}

int NodeComponent::getRightPanelsWidth() const {
    int width = 0;
    if (gainPanelVisible_)
        width += getGainPanelWidth();
    return width;
}

int NodeComponent::getTotalWidth(int baseContentWidth) const {
    return getLeftPanelsWidth() + baseContentWidth + getRightPanelsWidth() + getMeterWidth();
}

int NodeComponent::getExtraLeftPanelWidth() const {
    return getModulatorEditorWidth();
}

int NodeComponent::getExtraRightPanelWidth() const {
    return getMacroEditorWidth();
}

void NodeComponent::paintModPanel(juce::Graphics& g, juce::Rectangle<int> panelArea) {
    // If we have a real mods panel, just draw the header
    if (modsPanel_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        g.setFont(FontManager::getInstance().getUIFontBold(9.0f));
        g.drawText("MODS", panelArea.removeFromTop(16), juce::Justification::centred);
        return;
    }
    // Default: draw labeled placeholder (vertical side panel)
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    g.setFont(FontManager::getInstance().getUIFont(8.0f));
    g.drawText("MOD", panelArea.removeFromTop(16), juce::Justification::centred);
}

void NodeComponent::paintExtraLeftPanel(juce::Graphics& g, juce::Rectangle<int> panelArea) {
    // Draw modulator editor panel header if visible
    if (modulatorEditorVisible_ && modulatorEditorPanel_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).darker(0.2f));
        g.setFont(FontManager::getInstance().getUIFontBold(9.0f));
        g.drawText("MOD EDIT", panelArea.removeFromTop(16), juce::Justification::centred);
    }
}

void NodeComponent::paintParamPanel(juce::Graphics& g, juce::Rectangle<int> panelArea) {
    // If we have a real macros panel, just draw the header
    if (macroPanel_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
        g.setFont(FontManager::getInstance().getUIFontBold(9.0f));
        g.drawText("MACROS", panelArea.removeFromTop(16), juce::Justification::centred);
        return;
    }
    // Default: draw labeled placeholder (vertical side panel)
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    g.setFont(FontManager::getInstance().getUIFont(8.0f));
    g.drawText("PRM", panelArea.removeFromTop(16), juce::Justification::centred);
}

void NodeComponent::paintGainPanel(juce::Graphics& g, juce::Rectangle<int> panelArea) {
    // Draw a vertical meter/slider representation
    auto meterArea = panelArea.reduced(4, 8);

    // Meter background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
    g.fillRoundedRectangle(meterArea.toFloat(), 2.0f);

    // Mock meter fill (would be driven by actual audio level)
    float meterLevel = 0.6f;
    int fillHeight = static_cast<int>(meterLevel * meterArea.getHeight());
    auto fillArea = meterArea.removeFromBottom(fillHeight);

    // Gradient from green to yellow to red
    juce::ColourGradient gradient(
        juce::Colour(0xff2ecc71), 0.0f, static_cast<float>(meterArea.getBottom()),
        juce::Colour(0xffe74c3c), 0.0f, static_cast<float>(meterArea.getY()), false);
    gradient.addColour(0.7, juce::Colour(0xfff39c12));
    g.setGradientFill(gradient);
    g.fillRect(fillArea);

    // Border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRoundedRectangle(panelArea.reduced(4, 8).toFloat(), 2.0f, 1.0f);
}

void NodeComponent::resizedModPanel(juce::Rectangle<int> panelArea) {
    panelArea.removeFromTop(16);  // Skip header

    // If we have a real mods panel, use it
    if (modsPanel_) {
        modsPanel_->setBounds(panelArea);
        modsPanel_->setVisible(true);
        updateModsPanel();
        // Hide placeholder buttons
        for (auto& btn : modSlotButtons_) {
            if (btn)
                btn->setVisible(false);
        }
        return;
    }

    // Default: placeholder mod slot buttons
    panelArea = panelArea.reduced(2);
    int slotHeight = (panelArea.getHeight() - 4) / 3;
    for (int i = 0; i < 3; ++i) {
        modSlotButtons_[i]->setBounds(panelArea.removeFromTop(slotHeight).reduced(0, 1));
        modSlotButtons_[i]->setVisible(true);
    }
}

void NodeComponent::resizedExtraLeftPanel(juce::Rectangle<int> panelArea) {
    // Layout modulator editor panel if visible
    if (modulatorEditorVisible_ && modulatorEditorPanel_) {
        panelArea.removeFromTop(16);  // Skip header
        modulatorEditorPanel_->setBounds(panelArea);
        modulatorEditorPanel_->setVisible(true);
    } else if (modulatorEditorPanel_) {
        modulatorEditorPanel_->setVisible(false);
    }
}

void NodeComponent::resizedParamPanel(juce::Rectangle<int> panelArea) {
    panelArea.removeFromTop(16);  // Skip header

    // If we have a real macros panel, use it
    if (macroPanel_) {
        macroPanel_->setBounds(panelArea);
        macroPanel_->setVisible(true);
        updateMacroPanel();
        // Hide placeholder knobs
        for (auto& knob : paramKnobs_) {
            knob->setVisible(false);
        }
        return;
    }

    // Default: placeholder param knobs
    panelArea = panelArea.reduced(2);
    int knobSize = (panelArea.getWidth() - 2) / 2;
    int row = 0, col = 0;
    for (auto& knob : paramKnobs_) {
        int x = panelArea.getX() + col * (knobSize + 2);
        int y = panelArea.getY() + row * (knobSize + 2);
        knob->setBounds(x, y, knobSize, knobSize);
        knob->setVisible(true);
        col++;
        if (col >= 2) {
            col = 0;
            row++;
        }
    }
}

void NodeComponent::resizedGainPanel(juce::Rectangle<int> /*panelArea*/) {
    // Default: nothing - gain meter drawn in paintGainPanel
}

void NodeComponent::paintAIPanel(juce::Graphics& g, juce::Rectangle<int> panelArea) {
    // Header label — the AIPanelComponent (when mounted) draws the input/
    // output below this strip; resizedAIPanel positions it skipping the 16px
    // header band.
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    g.setFont(FontManager::getInstance().getUIFontBold(9.0f));
    g.drawText("AI", panelArea.removeFromTop(16), juce::Justification::centred);
}

void NodeComponent::resizedAIPanel(juce::Rectangle<int> panelArea) {
    panelArea.removeFromTop(16);  // skip header
    if (aiPanel_) {
        aiPanel_->setBounds(panelArea);
        aiPanel_->setVisible(true);
    }
}

void NodeComponent::paintExtraRightPanel(juce::Graphics& g, juce::Rectangle<int> panelArea) {
    // Draw macro editor panel header if visible
    if (macroEditorVisible_ && macroEditorPanel_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_PURPLE).darker(0.2f));
        g.setFont(FontManager::getInstance().getUIFontBold(9.0f));
        g.drawText("MACRO EDIT", panelArea.removeFromTop(16), juce::Justification::centred);
    }
}

void NodeComponent::resizedExtraRightPanel(juce::Rectangle<int> panelArea) {
    // Layout macro editor panel if visible
    if (macroEditorVisible_ && macroEditorPanel_) {
        panelArea.removeFromTop(16);  // Skip header
        macroEditorPanel_->setBounds(panelArea);
        macroEditorPanel_->setVisible(true);
    } else if (macroEditorPanel_) {
        macroEditorPanel_->setVisible(false);
    }
}

void NodeComponent::resizedCollapsed(juce::Rectangle<int>& /*area*/) {
    // Default: nothing - subclasses can add extra buttons
}

void NodeComponent::setSelected(bool selected) {
    if (selected_ != selected) {
        selected_ = selected;
        repaint();
    }
}

void NodeComponent::setCollapsed(bool collapsed) {
    if (collapsed_ != collapsed) {
        collapsed_ = collapsed;
        resized();
        repaint();
        if (onCollapsedChanged) {
            onCollapsedChanged(collapsed_);
        }
        if (onLayoutChanged) {
            onLayoutChanged();
        }
    }
}

void NodeComponent::setNodePath(const magda::ChainNodePath& path) {
    nodePath_ = path;

    // Update mods/macros panels with parent path for drag-and-drop
    if (modsPanel_) {
        modsPanel_->setParentPath(path);
    }
    if (macroPanel_) {
        macroPanel_->setParentPath(path);
    }

    // Path change → re-evaluate controller dots; the previous path's
    // resolver match no longer applies.
    refreshControllerIndicators();
}

juce::Point<float> NodeComponent::getControllerIndicatorAnchor() const {
    // Default: position the dot just to the right of nameLabel_'s rendered
    // text, vertically centred on the label. Subclasses with a custom
    // logo (Drum Grid, etc.) override this. A negative x suppresses
    // painting (used when there's no visible name to anchor to).
    if (!nameLabel_.isVisible() || nameLabel_.getText().isEmpty())
        return {-1.0f, 0.0f};

    auto labelBounds = nameLabel_.getBounds();
    // GlyphArrangement is JUCE's recommended path for measuring text
    // since juce::Font::getStringWidthFloat is deprecated.
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(nameLabel_.getFont(), nameLabel_.getText(), 0.0f, 0.0f);
    const float textWidth = glyphs.getBoundingBox(0, -1, true).getWidth();

    constexpr float gapAfterText = 12.0f;
    return {static_cast<float>(labelBounds.getX()) + textWidth + gapAfterText,
            static_cast<float>(labelBounds.getCentreY())};
}

void NodeComponent::refreshControllerIndicators() {
    if (!nodePath_.isValid()) {
        if (hasPinnedBindings_ || hasAutomapBindings_) {
            hasPinnedBindings_ = false;
            hasAutomapBindings_ = false;
            repaint();
        }
        return;
    }

    // pinned (orange): any user-mapped binding (Static or Alias) — covers
    // Learn'd plugin params AND Learn'd macros / mod-rates on this node.
    // automap (green): resolver bindings — i.e. profile-level defaults
    // currently resolving to this node (focus-dependent for focused.macro).
    // Both are gated on the owning controller being the active surface AND
    // connected, so the dots reflect live control state rather than mere
    // config presence (see magda::controllers::ControllerActivation).
    bool pinned = magda::controllers::isDeviceUserMapLive(nodePath_);
    bool automap = magda::controllers::isDeviceAutomapLive(nodePath_);
    if (pinned != hasPinnedBindings_ || automap != hasAutomapBindings_) {
        hasPinnedBindings_ = pinned;
        hasAutomapBindings_ = automap;
        repaint();
    }
}

void NodeComponent::selectionTypeChanged(magda::SelectionType newType) {
    // If selection type changed away from ChainNode/Device, deselect this node.
    // Both ChainNode and Device represent a selected device in the chain.
    if (newType != magda::SelectionType::ChainNode &&
        newType != magda::SelectionType::MultiChainNode &&
        newType != magda::SelectionType::Device) {
        setSelected(false);
    }
}

void NodeComponent::chainNodeSelectionChanged(const magda::ChainNodePath& /*path*/) {
    // Update our selection state based on whether we match the selected path
    auto& selection = magda::SelectionManager::getInstance();
    bool shouldBeSelected = nodePath_.isValid() && selection.isChainNodeSelected(nodePath_);
    setSelected(shouldBeSelected);
    // The focused.macro resolver depends on the live focus — when focus
    // shifts to/from this node (or any sibling), recheck the automap dot.
    refreshControllerIndicators();
}

void NodeComponent::chainNodeReselected(const magda::ChainNodePath& /*path*/) {
    // Not used - we handle collapse toggle directly in mouseUp
}

void NodeComponent::paramSelectionChanged(const magda::ParamSelection& /*selection*/) {}

void NodeComponent::mouseDown(const juce::MouseEvent& e) {
    // Only handle left clicks for selection
    if (e.mods.isLeftButtonDown()) {
        mouseDownForSelection_ = true;

        // Capture drag start position in parent coordinates
        if (draggable_) {
            if (auto* parent = getParentComponent()) {
                dragStartPos_ = e.getEventRelativeTo(parent).getPosition();
            }
            dragStartBounds_ = getBounds().getPosition();
            isDragging_ = false;
        }
    }
}

void NodeComponent::mouseMove(const juce::MouseEvent& e) {
    // Alt = copy-on-drag affordance (mirrors clips): show the copying cursor
    // when hovering a device with Alt held.
    setMouseCursor(e.mods.isAltDown() ? juce::MouseCursor::CopyingCursor
                                      : juce::MouseCursor::NormalCursor);
}

void NodeComponent::mouseDrag(const juce::MouseEvent& e) {
    if (!mouseDownForSelection_ || !draggable_)
        return;

    setMouseCursor(e.mods.isAltDown() ? juce::MouseCursor::CopyingCursor
                                      : juce::MouseCursor::NormalCursor);

    auto* parent = getParentComponent();
    if (!parent)
        return;

    auto currentPos = e.getEventRelativeTo(parent).getPosition();
    int deltaX = std::abs(currentPos.x - dragStartPos_.x);
    int deltaY = std::abs(currentPos.y - dragStartPos_.y);

    // Check threshold before starting drag
    if (!isDragging_ && (deltaX > DRAG_THRESHOLD || deltaY > DRAG_THRESHOLD)) {
        isDragging_ = true;
        if (nodePath_.isValid()) {
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this)) {
                auto& selection = magda::SelectionManager::getInstance();
                auto paths = selection.isChainNodeSelected(nodePath_)
                                 ? selection.getSelectedChainNodes()
                                 : std::vector<magda::ChainNodePath>{nodePath_};
                if (paths.empty())
                    paths.push_back(nodePath_);

                auto* dragInfo = new juce::DynamicObject();
                dragInfo->setProperty("type", paths.size() > 1 ? "chainElements" : "chainElement");
                dragInfo->setProperty("pathCount", static_cast<int>(paths.size()));
                writePathToDragInfo(*dragInfo, paths.front());
                for (int i = 0; i < static_cast<int>(paths.size()); ++i)
                    writePathToDragInfo(*dragInfo, paths[static_cast<size_t>(i)], juce::String(i));

                const auto label =
                    paths.size() > 1 ? juce::String(paths.size()) + " Chain Items" : getNodeName();
                auto dragImage = createChainNodeDragImage(label, static_cast<int>(paths.size()));
                container->startDragging(juce::var(dragInfo), this, juce::ScaledImage(dragImage),
                                         true);
            }
        }
        if (onDragStart)
            onDragStart(this, e);
    }

    if (isDragging_ && onDragMove) {
        onDragMove(this, e);
    }
}

void NodeComponent::rangeSelectFromAnchor() {
    auto& selection = magda::SelectionManager::getInstance();
    const auto& anchor = selection.getAnchorChainNode();

    auto* parent = getParentComponent();
    if (!anchor.isValid() || parent == nullptr) {
        selection.selectChainNode(nodePath_);
        return;
    }

    // Siblings of a chain level are the NodeComponents under the same parent,
    // in child (= chain) order
    std::vector<magda::ChainNodePath> siblingPaths;
    int anchorIdx = -1, clickedIdx = -1;
    for (int i = 0; i < parent->getNumChildComponents(); ++i) {
        auto* sibling = dynamic_cast<NodeComponent*>(parent->getChildComponent(i));
        if (sibling == nullptr || !sibling->nodePath_.isValid())
            continue;
        if (sibling->nodePath_ == anchor)
            anchorIdx = static_cast<int>(siblingPaths.size());
        if (sibling == this)
            clickedIdx = static_cast<int>(siblingPaths.size());
        siblingPaths.push_back(sibling->nodePath_);
    }

    if (anchorIdx < 0 || clickedIdx < 0) {
        // Anchor lives in another chain (or is gone): fall back to replace
        selection.selectChainNode(nodePath_);
        return;
    }

    const int lo = std::min(anchorIdx, clickedIdx);
    const int hi = std::max(anchorIdx, clickedIdx);
    std::vector<magda::ChainNodePath> range(siblingPaths.begin() + lo,
                                            siblingPaths.begin() + hi + 1);
    selection.selectChainNodes(range);
}

void NodeComponent::mouseUp(const juce::MouseEvent& e) {
    // If we were dragging, commit the drag and skip selection
    if (isDragging_) {
        if (onDragEnd)
            onDragEnd(this, e);
        isDragging_ = false;
        mouseDownForSelection_ = false;
        return;
    }

    // While a macro/mod is in link mode, clicks bouncing up from a non-link-
    // target child (a tab strip, the device meter, the empty space between
    // params) shouldn't change selection or toggle the device's collapsed
    // state — that interrupts the linking gesture and visibly collapses the
    // device the user is trying to link into. Bail out of the selection /
    // collapse path; the actual link target widget (ParamSlot,
    // LinkableTextSlider) consumes its own click separately.
    auto& linkMgr = magda::LinkModeManager::getInstance();
    if (linkMgr.getMacroInLinkMode().isValid() || linkMgr.getModInLinkMode().isValid()) {
        mouseDownForSelection_ = false;
        return;
    }

    // Complete selection on mouse up (click-and-release) - only if not dragging
    if (mouseDownForSelection_ && !e.mods.isPopupMenu()) {
        mouseDownForSelection_ = false;

        // Check if mouse is still within bounds (not a drag-away)
        if (getLocalBounds().contains(e.getPosition())) {
            if (nodePath_.isValid()) {
                // Capture state BEFORE calling selectChainNode
                // (callbacks may change these values synchronously)
                bool wasAlreadySelected = selected_;
                bool wasCollapsed = collapsed_;

                // selectChainNode fans out to every SelectionManagerListener —
                // some listener paths can trigger rebuildNodeComponents, which
                // would delete *this* while we're still inside mouseUp. Guard
                // with a SafePointer and bail if we got destroyed mid-dispatch.
                juce::Component::SafePointer<NodeComponent> safeThis(this);
                auto& selection = magda::SelectionManager::getInstance();
                const bool toggle = magda::isToggleSelectClick(e.mods) ||
                                    (e.mods.isCtrlDown() && !e.mods.isShiftDown());
                const bool range = magda::isRangeSelectClick(e.mods);
                if (range)
                    rangeSelectFromAnchor();
                else if (toggle)
                    selection.toggleChainNodeSelection(nodePath_);
                else
                    selection.selectChainNode(nodePath_);
                if (safeThis == nullptr)
                    return;

                // If was already selected, toggle collapse — but only when collapsed
                // (to expand) or when the click is on the header bar (to collapse)
                if (!toggle && !range && wasAlreadySelected &&
                    (wasCollapsed || e.getPosition().y < getHeaderHeight())) {
                    setCollapsed(!wasCollapsed);
                }
            }

            // Also call legacy callback for backward compatibility
            if (onSelected) {
                onSelected();
            }
        }
    }

    isDragging_ = false;
}

void NodeComponent::mouseWheelMove(const juce::MouseEvent& e,
                                   const juce::MouseWheelDetails& wheel) {
    // Cmd/Ctrl + scroll wheel = zoom (forward to parent chain panel)
    if (e.mods.isCommandDown() && onZoomDelta) {
        float delta = wheel.deltaY > 0 ? 0.1f : -0.1f;
        onZoomDelta(delta);
    } else {
        // Let parent handle normal scrolling
        Component::mouseWheelMove(e, wheel);
    }
}

// === Mods/Macros Panel Support ===

void NodeComponent::initializeModsMacrosPanels() {
    // Create mods panel
    modsPanel_ = std::make_unique<ModsPanelComponent>();
    modsPanel_->onModTargetChanged = [this](int modIndex, magda::ControlTarget target) {
        onModTargetChangedInternal(modIndex, target);
    };
    modsPanel_->onModLinkRemoved = [this](int modIndex, magda::ControlTarget target) {
        onModLinkRemovedInternal(modIndex, target);
        updateModsPanel();
        updateModulatorEditor();
    };
    modsPanel_->onModAllLinksCleared = [this](int modIndex) {
        onModAllLinksClearedInternal(modIndex);
        updateModsPanel();
        updateModulatorEditor();
    };
    modsPanel_->onModNameChanged = [this](int modIndex, juce::String name) {
        onModNameChangedInternal(modIndex, name);
    };
    modsPanel_->onModClicked = [this](int modIndex) {
        onModClickedInternal(modIndex);
        // Toggle modulator editor - if clicking same mod, hide; otherwise show
        if (modulatorEditorVisible_ && selectedModIndex_ == modIndex) {
            hideModulatorEditor();
        } else {
            showModulatorEditor(modIndex);
        }
    };
    modsPanel_->onAddModRequested = [this](int slotIndex, magda::ModType type,
                                           magda::LFOWaveform waveform) {
        onAddModRequestedInternal(slotIndex, type, waveform);
    };
    modsPanel_->onModRemoveRequested = [this](int modIndex) {
        onModRemoveRequestedInternal(modIndex);
    };
    modsPanel_->onModEnableToggled = [this](int modIndex, bool enabled) {
        onModEnableToggledInternal(modIndex, enabled);
    };
    modsPanel_->onAddPageRequested = [this](int itemsToAdd) { onModPageAddRequested(itemsToAdd); };
    modsPanel_->onRemovePageRequested = [this](int itemsToRemove) {
        onModPageRemoveRequested(itemsToRemove);
    };
    modsPanel_->onPanelClicked = [this]() {
        magda::SelectionManager::getInstance().selectModsPanel(nodePath_);
    };
    addChildComponent(*modsPanel_);

    // Create macro panel
    macroPanel_ = std::make_unique<MacroPanelComponent>();
    macroPanel_->onMacroValueChanged = [this](int macroIndex, float value) {
        onMacroValueChangedInternal(macroIndex, value);
    };
    macroPanel_->onMacroTargetChanged = [this](int macroIndex, magda::ControlTarget target) {
        onMacroTargetChangedInternal(macroIndex, target);
    };
    macroPanel_->onMacroNameChanged = [this](int macroIndex, juce::String name) {
        onMacroNameChangedInternal(macroIndex, name);
    };
    macroPanel_->onMacroLinkRemoved = [this](int macroIndex, magda::ControlTarget target) {
        onMacroLinkRemovedInternal(macroIndex, target);
        updateMacroPanel();
        updateMacroEditor();
    };
    macroPanel_->onMacroAllLinksCleared = [this](int macroIndex) {
        onMacroAllLinksClearedInternal(macroIndex);
    };
    macroPanel_->onMacroClicked = [this](int macroIndex) {
        onMacroClickedInternal(macroIndex);
        // Toggle macro editor - if clicking same macro, hide; otherwise show
        if (macroEditorVisible_ && selectedMacroIndex_ == macroIndex) {
            hideMacroEditor();
        } else {
            showMacroEditor(macroIndex);
        }
    };
    macroPanel_->onAddPageRequested = [this](int itemsToAdd) {
        onMacroPageAddRequested(itemsToAdd);
    };
    macroPanel_->onRemovePageRequested = [this](int itemsToRemove) {
        onMacroPageRemoveRequested(itemsToRemove);
    };
    macroPanel_->onPanelClicked = [this]() {
        magda::SelectionManager::getInstance().selectMacrosPanel(nodePath_);
    };
    addChildComponent(*macroPanel_);

    // Create modulator editor panel
    modulatorEditorPanel_ = std::make_unique<ModulatorEditorPanel>();
    modulatorEditorPanel_->onNameChanged = [this](juce::String name) {
        if (selectedModIndex_ >= 0) {
            onModNameChangedInternal(selectedModIndex_, name);
        }
    };
    modulatorEditorPanel_->onRateChanged = [this](float rate) {
        if (selectedModIndex_ >= 0) {
            onModRateChangedInternal(selectedModIndex_, rate);
        }
    };
    modulatorEditorPanel_->onWaveformChanged = [this](magda::LFOWaveform waveform) {
        if (selectedModIndex_ >= 0) {
            onModWaveformChangedInternal(selectedModIndex_, waveform);
        }
    };
    modulatorEditorPanel_->onTempoSyncChanged = [this](bool tempoSync) {
        if (selectedModIndex_ >= 0) {
            onModTempoSyncChangedInternal(selectedModIndex_, tempoSync);
        }
    };
    modulatorEditorPanel_->onSyncDivisionChanged = [this](magda::SyncDivision division) {
        if (selectedModIndex_ >= 0) {
            onModSyncDivisionChangedInternal(selectedModIndex_, division);
        }
    };
    modulatorEditorPanel_->onTriggerModeChanged = [this](magda::LFOTriggerMode mode) {
        if (selectedModIndex_ >= 0) {
            onModTriggerModeChangedInternal(selectedModIndex_, mode);
        }
    };
    modulatorEditorPanel_->onAudioAttackChanged = [this](float ms) {
        if (selectedModIndex_ >= 0) {
            onModAudioAttackChangedInternal(selectedModIndex_, ms);
        }
    };
    modulatorEditorPanel_->onAudioReleaseChanged = [this](float ms) {
        if (selectedModIndex_ >= 0) {
            onModAudioReleaseChangedInternal(selectedModIndex_, ms);
        }
    };
    modulatorEditorPanel_->onCurveChanged = [this]() {
        // Force repaint of waveform displays for immediate curve editor sync
        if (modsPanel_) {
            modsPanel_->repaintWaveforms();
        }
        // Notify audio thread so CurveSnapshot is updated in real time
        if (selectedModIndex_ >= 0) {
            onModCurveChangedInternal(selectedModIndex_);
        }
    };
    modulatorEditorPanel_->onAdvancedClicked = [this]() {
        // Try device first, then rack
        auto* device = magda::TrackManager::getInstance().getDeviceInChainByPath(nodePath_);
        auto* rack = device ? nullptr : magda::TrackManager::getInstance().getRackByPath(nodePath_);
        if (!device && !rack)
            return;

        const auto& sidechain = device ? device->sidechain : rack->sidechain;
        const auto& mods = device ? device->mods : rack->mods;

        // Pick sidechain type from the selected modulator's trigger mode.
        // Advanced is only enabled in MIDI/Audio modes, so the fallback is fine.
        const bool isAudioMode =
            (selectedModIndex_ >= 0 && selectedModIndex_ < (int)mods.size() &&
             mods[(size_t)selectedModIndex_].triggerMode == magda::LFOTriggerMode::Audio);
        const auto sidechainType =
            isAudioMode ? magda::SidechainConfig::Type::Audio : magda::SidechainConfig::Type::MIDI;

        juce::PopupMenu menu;

        bool hasSidechain =
            sidechain.type == sidechainType && sidechain.sourceTrackId != magda::INVALID_TRACK_ID;

        menu.addSectionHeader(isAudioMode ? "Audio Trigger Source" : "MIDI Trigger Source");
        menu.addItem(1, "Self", true, !hasSidechain);
        menu.addSeparator();

        struct TrackEntry {
            magda::TrackId id;
            juce::String name;
        };
        auto trackEntries = std::make_shared<std::vector<TrackEntry>>();
        int itemId = 10;
        for (const auto& track : magda::TrackManager::getInstance().getTracks()) {
            if (track.id == nodePath_.trackId)
                continue;
            bool isCurrent = hasSidechain && sidechain.sourceTrackId == track.id;
            menu.addItem(itemId, track.name, true, isCurrent);
            trackEntries->push_back({track.id, track.name});
            itemId++;
        }

        auto safeThis = juce::Component::SafePointer(this);
        auto isDeviceTarget = device != nullptr;
        auto deviceId = device ? device->id : magda::INVALID_DEVICE_ID;
        auto rackPath = nodePath_;
        menu.showMenuAsync(juce::PopupMenu::Options(), [safeThis, isDeviceTarget, deviceId,
                                                        rackPath, trackEntries,
                                                        sidechainType](int result) {
            if (!safeThis || result == 0)
                return;
            if (result == 1) {
                if (isDeviceTarget)
                    magda::TrackManager::getInstance().clearSidechain(deviceId);
                else
                    magda::TrackManager::getInstance().clearRackSidechain(rackPath);
            } else {
                int index = result - 10;
                if (index >= 0 && index < (int)trackEntries->size()) {
                    if (isDeviceTarget) {
                        magda::TrackManager::getInstance().setSidechainSource(
                            deviceId, (*trackEntries)[index].id, sidechainType);
                    } else {
                        magda::TrackManager::getInstance().setRackSidechainSource(
                            rackPath, (*trackEntries)[index].id, sidechainType);
                    }
                }
            }
        });
    };
    // Mod matrix: param name resolver
    modulatorEditorPanel_->setParamNameResolver([this](magda::DeviceId deviceId,
                                                       int paramIndex) -> juce::String {
        auto* device = magda::TrackManager::getInstance().getDeviceInChainByPath(nodePath_);
        if (!device) {
            // Try rack: look up device by ID across all chains
            auto* rack = magda::TrackManager::getInstance().getRackByPath(nodePath_);
            if (rack) {
                for (auto& chain : rack->chains) {
                    for (auto& element : chain.elements) {
                        if (magda::isDevice(element) && magda::getDevice(element).id == deviceId) {
                            device = &magda::getDevice(element);
                            break;
                        }
                    }
                    if (device)
                        break;
                }
            }
        }
        if (device && device->id == deviceId) {
            if (const auto* info = device->findParameterByIndex(paramIndex))
                return info->name;
        }
        return "P" + juce::String(paramIndex);
    });

    // Mod matrix: delete link
    modulatorEditorPanel_->onModLinkDeleted = [this](int modIndex, magda::ControlTarget target) {
        auto* device = magda::TrackManager::getInstance().getDeviceInChainByPath(nodePath_);
        if (device) {
            magda::TrackManager::getInstance().removeModLink(nodePath_, modIndex, target);
        } else {
            // Rack mod
            magda::TrackManager::getInstance().removeModLink(nodePath_, modIndex, target);
        }
        updateModulatorEditor();
    };

    // Mod matrix: toggle bipolar
    modulatorEditorPanel_->onModLinkBipolarChanged =
        [this](int modIndex, magda::ControlTarget target, bool bipolar) {
            auto* device = magda::TrackManager::getInstance().getDeviceInChainByPath(nodePath_);
            if (device) {
                magda::TrackManager::getInstance().setModLinkBipolar(nodePath_, modIndex, target,
                                                                     bipolar);
            } else {
                magda::TrackManager::getInstance().setModLinkBipolar(nodePath_, modIndex, target,
                                                                     bipolar);
            }
            updateModulatorEditor();
        };

    // Mod matrix: enable/disable link without losing its amount
    modulatorEditorPanel_->onModLinkEnabledChanged = [this](int modIndex,
                                                            magda::ControlTarget target,
                                                            bool enabled) {
        magda::TrackManager::getInstance().setModLinkEnabled(nodePath_, modIndex, target, enabled);
        updateModulatorEditor();
    };

    // Mod matrix: change link amount
    modulatorEditorPanel_->onModLinkAmountChanged =
        [this](int modIndex, magda::ControlTarget target, float amount) {
            auto* device = magda::TrackManager::getInstance().getDeviceInChainByPath(nodePath_);
            if (device) {
                magda::TrackManager::getInstance().setModLinkAmount(nodePath_, modIndex, target,
                                                                    amount);
            } else {
                magda::TrackManager::getInstance().setModLinkAmount(nodePath_, modIndex, target,
                                                                    amount);
            }
        };

    addChildComponent(*modulatorEditorPanel_);

    // Create macro editor panel
    macroEditorPanel_ = std::make_unique<MacroEditorPanel>();
    macroEditorPanel_->onNameChanged = [this](juce::String name) {
        if (selectedMacroIndex_ >= 0) {
            onMacroNameChangedInternal(selectedMacroIndex_, name);
        }
    };
    macroEditorPanel_->onValueChanged = [this](float value) {
        if (selectedMacroIndex_ >= 0) {
            onMacroValueChangedInternal(selectedMacroIndex_, value);
        }
    };
    macroEditorPanel_->onLinkAmountChanged = [this](magda::ControlTarget target, float amount) {
        if (selectedMacroIndex_ >= 0) {
            onMacroLinkAmountChangedInternal(selectedMacroIndex_, target, amount);
        }
    };
    macroEditorPanel_->onLinkRemoved = [this](magda::ControlTarget target) {
        if (selectedMacroIndex_ >= 0) {
            onMacroLinkRemovedInternal(selectedMacroIndex_, target);
            updateMacroEditor();
        }
    };
    macroEditorPanel_->onLinkBipolarToggled = [this](magda::ControlTarget target, bool bipolar) {
        if (selectedMacroIndex_ >= 0) {
            onMacroLinkBipolarChangedInternal(selectedMacroIndex_, target, bipolar);
            updateMacroEditor();
        }
    };
    macroEditorPanel_->setParamNameResolver(
        [this](magda::DeviceId deviceId, int paramIndex) -> juce::String {
            auto paramNamesMap = getDeviceParamNames();
            auto it = paramNamesMap.find(deviceId);
            if (it != paramNamesMap.end() && paramIndex >= 0 &&
                paramIndex < static_cast<int>(it->second.size())) {
                return it->second[static_cast<size_t>(paramIndex)];
            }
            return "P" + juce::String(paramIndex);
        });
    macroEditorPanel_->setModNameResolver(
        [this](magda::ModId modId, int modParamIndex) -> juce::String {
            // Same scope as the macro — pull the mod list from this node and
            // format "<modName> Rate" / "<modName> Depth" for the link row.
            const auto* mods = getModsData();
            if (!mods)
                return {};
            for (const auto& m : *mods) {
                if (m.id == modId) {
                    return magda::getModParameterDisplayName(m, modParamIndex);
                }
            }
            return {};
        });
    addChildComponent(*macroEditorPanel_);

    // AI panel — created lazily; bound to a device path / pluginId by
    // DeviceSlotComponent (or whichever subclass mounts on a real device).
    aiPanel_ = std::make_unique<AIPanelComponent>();
    addChildComponent(*aiPanel_);
}

void NodeComponent::updateModsPanel() {
    if (!modsPanel_)
        return;

    const auto* mods = getModsData();
    if (mods) {
        modsPanel_->setMods(*mods);
    }

    auto devices = getAvailableDevices();
    modsPanel_->setAvailableDevices(devices);
    modsPanel_->setDeviceParamNames(getDeviceParamNames());

    // Same-scope modifiers — each knob's "Link to Modulator" submenu
    // can target another mod's rate. Skip the knob's own ModId is done
    // inside the knob (it knows its own currentMod_.id).
    std::vector<std::pair<magda::ModId, juce::String>> modList;
    if (mods) {
        modList.reserve(mods->size());
        for (const auto& m : *mods)
            if (m.enabled)
                modList.emplace_back(m.id, magda::getModDisplayName(m));
    }
    modsPanel_->setAvailableModifiers(modList);
}

void NodeComponent::updateMacroValueDisplay(int macroIndex, float value) {
    if (macroPanel_)
        macroPanel_->updateMacroValueDisplay(macroIndex, value);
}

void NodeComponent::fadeInParamPanelContent() {
    auto safeThis = juce::Component::SafePointer<NodeComponent>(this);
    auto repaintPanel = [safeThis](float alpha) {
        if (safeThis == nullptr)
            return;
        safeThis->paramPanelAlpha_ = alpha;
        safeThis->repaint();
    };

    if (macroPanel_) {
        paramPanelFadeTimer_->fadeIn({macroPanel_.get()}, SIDE_PANEL_FADE_IN_MS, repaintPanel);
        return;
    }

    std::vector<juce::Component*> targets;
    targets.reserve(paramKnobs_.size());
    for (auto& knob : paramKnobs_)
        targets.push_back(knob.get());
    paramPanelFadeTimer_->fadeIn(targets, SIDE_PANEL_FADE_IN_MS, repaintPanel);
}

void NodeComponent::cancelParamPanelContentFade() {
    paramPanelFadeTimer_->cancel();
}

void NodeComponent::fadeOutParamPanelContent() {
    std::vector<juce::Component*> targets;
    if (macroPanel_) {
        targets.push_back(macroPanel_.get());
    } else {
        targets.reserve(paramKnobs_.size());
        for (auto& knob : paramKnobs_)
            targets.push_back(knob.get());
    }

    auto safeThis = juce::Component::SafePointer<NodeComponent>(this);
    paramPanelFadeTimer_->fadeOut(
        targets, SIDE_PANEL_FADE_IN_MS,
        [safeThis]() {
            if (safeThis == nullptr)
                return;
            safeThis->retainParamPanelForFadeOut_ = false;
            safeThis->paramPanelAlpha_ = 1.0f;
            safeThis->resized();
            safeThis->repaint();
            if (safeThis->onLayoutChanged)
                safeThis->onLayoutChanged();
        },
        [safeThis](float alpha) {
            if (safeThis == nullptr)
                return;
            safeThis->paramPanelAlpha_ = alpha;
            safeThis->repaint();
        });
}

void NodeComponent::fadeInModPanelContent() {
    auto safeThis = juce::Component::SafePointer<NodeComponent>(this);
    auto repaintPanel = [safeThis](float alpha) {
        if (safeThis == nullptr)
            return;
        safeThis->modPanelAlpha_ = alpha;
        safeThis->repaint();
    };

    if (modsPanel_) {
        modPanelFadeTimer_->fadeIn({modsPanel_.get()}, SIDE_PANEL_FADE_IN_MS, repaintPanel);
        return;
    }

    std::vector<juce::Component*> targets;
    targets.reserve(3);
    for (auto& button : modSlotButtons_)
        targets.push_back(button.get());
    modPanelFadeTimer_->fadeIn(targets, SIDE_PANEL_FADE_IN_MS, repaintPanel);
}

void NodeComponent::cancelModPanelContentFade() {
    modPanelFadeTimer_->cancel();
}

void NodeComponent::fadeOutModPanelContent() {
    std::vector<juce::Component*> targets;
    if (modsPanel_) {
        targets.push_back(modsPanel_.get());
    } else {
        targets.reserve(3);
        for (auto& button : modSlotButtons_)
            targets.push_back(button.get());
    }

    auto safeThis = juce::Component::SafePointer<NodeComponent>(this);
    modPanelFadeTimer_->fadeOut(
        targets, SIDE_PANEL_FADE_IN_MS,
        [safeThis]() {
            if (safeThis == nullptr)
                return;
            safeThis->retainModPanelForFadeOut_ = false;
            safeThis->modPanelAlpha_ = 1.0f;
            safeThis->resized();
            safeThis->repaint();
            if (safeThis->onLayoutChanged)
                safeThis->onLayoutChanged();
        },
        [safeThis](float alpha) {
            if (safeThis == nullptr)
                return;
            safeThis->modPanelAlpha_ = alpha;
            safeThis->repaint();
        });
}

void NodeComponent::refreshPanels() {
    if (isParamPanelLaidOut())
        updateMacroPanel();
    if (isModPanelLaidOut())
        updateModsPanel();
    if (modulatorEditorVisible_)
        updateModulatorEditor();
    if (macroEditorVisible_)
        updateMacroEditor();
}

void NodeComponent::updateMacroPanel() {
    if (!macroPanel_)
        return;

    const auto* macros = getMacrosData();
    if (macros) {
        macroPanel_->setMacros(*macros);
    }

    auto devices = getAvailableDevices();
    macroPanel_->setAvailableDevices(devices);
    macroPanel_->setDeviceParamNames(getDeviceParamNames());

    // Same-scope modifiers — let the macro link picker offer "Modulators →
    // <mod> → Rate" entries that resolve to the LFO's rate / rateType param.
    std::vector<std::pair<magda::ModId, juce::String>> mods;
    if (const auto* modsData = getModsData()) {
        mods.reserve(modsData->size());
        for (const auto& m : *modsData)
            if (m.enabled)
                mods.emplace_back(m.id, magda::getModDisplayName(m));
    }
    macroPanel_->setAvailableModifiers(mods);
}

// === Modulator Editor Panel ===

void NodeComponent::showModulatorEditor(int modIndex) {
    selectedModIndex_ = modIndex;
    modulatorEditorVisible_ = true;

    updateModulatorEditor();

    // Trigger relayout
    resized();
    repaint();
    if (onLayoutChanged)
        onLayoutChanged();
}

void NodeComponent::hideModulatorEditor() {
    selectedModIndex_ = -1;
    modulatorEditorVisible_ = false;

    if (modulatorEditorPanel_) {
        modulatorEditorPanel_->setVisible(false);
        modulatorEditorPanel_->setSelectedModIndex(-1);
    }

    // Trigger relayout
    resized();
    repaint();
    if (onLayoutChanged)
        onLayoutChanged();
}

void NodeComponent::updateModulatorEditor() {
    if (!modulatorEditorPanel_ || selectedModIndex_ < 0)
        return;

    const auto* mods = getModsData();
    if (!mods)
        return;

    if (selectedModIndex_ < static_cast<int>(mods->size())) {
        // Pass pointer to live mod for animated waveform display,
        // plus a getter lambda that safely re-fetches the pointer each time
        // (guards against vector reallocation invalidating raw pointers).
        int modIdx = selectedModIndex_;
        auto getter = [this, modIdx]() -> const magda::ModInfo* {
            const auto* m = getModsData();
            if (m && modIdx < static_cast<int>(m->size()))
                return &(*m)[modIdx];
            return nullptr;
        };
        modulatorEditorPanel_->setOwnerPath(nodePath_.trackId, nodePath_);
        modulatorEditorPanel_->setModInfo((*mods)[selectedModIndex_], &(*mods)[selectedModIndex_],
                                          std::move(getter));
        modulatorEditorPanel_->setSelectedModIndex(selectedModIndex_);
    }
}

// === Macro Editor Panel ===

void NodeComponent::showMacroEditor(int macroIndex) {
    selectedMacroIndex_ = macroIndex;
    macroEditorVisible_ = true;

    updateMacroEditor();

    // Trigger relayout
    resized();
    repaint();
    if (onLayoutChanged)
        onLayoutChanged();
}

void NodeComponent::hideMacroEditor() {
    selectedMacroIndex_ = -1;
    macroEditorVisible_ = false;

    if (macroEditorPanel_) {
        macroEditorPanel_->setVisible(false);
        macroEditorPanel_->setSelectedMacroIndex(-1);
    }

    // Trigger relayout
    resized();
    repaint();
    if (onLayoutChanged)
        onLayoutChanged();
}

void NodeComponent::updateMacroEditor() {
    if (!macroEditorPanel_ || selectedMacroIndex_ < 0)
        return;

    const auto* macros = getMacrosData();
    if (!macros)
        return;

    if (selectedMacroIndex_ < static_cast<int>(macros->size())) {
        macroEditorPanel_->setMacroInfo((*macros)[selectedMacroIndex_]);
        macroEditorPanel_->setSelectedMacroIndex(selectedMacroIndex_);
    }
}

int NodeComponent::getModulatorEditorWidth() const {
    return modulatorEditorVisible_ ? ModulatorEditorPanel::PREFERRED_WIDTH : 0;
}

int NodeComponent::getMacroEditorWidth() const {
    return macroEditorVisible_ ? MacroEditorPanel::PREFERRED_WIDTH : 0;
}

}  // namespace magda::daw::ui
