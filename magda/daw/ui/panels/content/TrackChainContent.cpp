#include "TrackChainContent.hpp"

#include <BinaryData.h>

#include <cmath>

#include "../../debug/DebugSettings.hpp"
#include "../../dialogs/ChainTreeDialog.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/MixerMetrics.hpp"
#include "../../themes/SmallButtonLookAndFeel.hpp"
#include "core/DeviceInfo.hpp"
#include "core/SelectionManager.hpp"
#include "ui/components/chain/NodeComponent.hpp"
#include "ui/components/chain/RackComponent.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

//==============================================================================
// GainMeterComponent - Vertical gain slider with peak meter background
//==============================================================================
class GainMeterComponent : public juce::Component,
                           public juce::Label::Listener,
                           private juce::Timer {
  public:
    GainMeterComponent() {
        // Editable label for dB value
        dbLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
        dbLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
        dbLabel_.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        dbLabel_.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
        dbLabel_.setColour(juce::Label::outlineWhenEditingColourId,
                           DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        dbLabel_.setColour(juce::Label::backgroundWhenEditingColourId,
                           DarkTheme::getColour(DarkTheme::BACKGROUND));
        dbLabel_.setJustificationType(juce::Justification::centred);
        dbLabel_.setEditable(false, true, false);  // Single-click to edit
        dbLabel_.addListener(this);
        addAndMakeVisible(dbLabel_);

        updateLabel();

        // Start timer for mock meter animation
        startTimerHz(30);
    }

    ~GainMeterComponent() override {
        stopTimer();
    }

    void setGainDb(double db, juce::NotificationType notification = juce::sendNotification) {
        db = juce::jlimit(-60.0, 6.0, db);
        if (std::abs(gainDb_ - db) > 0.01) {
            gainDb_ = db;
            updateLabel();
            repaint();
            if (notification != juce::dontSendNotification && onGainChanged) {
                onGainChanged(gainDb_);
            }
        }
    }

    double getGainDb() const {
        return gainDb_;
    }

    // Mock meter level (0-1) - in real implementation this would come from audio processing
    void setMeterLevel(float level) {
        meterLevel_ = juce::jlimit(0.0f, 1.0f, level);
        repaint();
    }

    std::function<void(double)> onGainChanged;

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();
        auto meterArea = bounds.removeFromTop(bounds.getHeight() - 14).reduced(2);

        // Background
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
        g.fillRoundedRectangle(meterArea.toFloat(), 2.0f);

        // Meter fill (from bottom up)
        float fillHeight = meterLevel_ * meterArea.getHeight();
        auto fillArea = meterArea.removeFromBottom(static_cast<int>(fillHeight));

        // Gradient from green (low) to yellow to red (high)
        juce::ColourGradient gradient(
            juce::Colour(0xff2ecc71), 0.0f, static_cast<float>(meterArea.getBottom()),
            juce::Colour(0xffe74c3c), 0.0f, static_cast<float>(meterArea.getY()), false);
        gradient.addColour(0.7, juce::Colour(0xfff39c12));  // Yellow at 70%
        g.setGradientFill(gradient);
        g.fillRect(fillArea);

        // Gain position indicator (horizontal line)
        float gainNormalized = static_cast<float>((gainDb_ + 60.0) / 66.0);  // -60 to +6 dB
        int gainY =
            meterArea.getY() + static_cast<int>((1.0f - gainNormalized) * meterArea.getHeight());
        g.setColour(DarkTheme::getTextColour());
        g.drawHorizontalLine(gainY, static_cast<float>(meterArea.getX()),
                             static_cast<float>(meterArea.getRight()));

        // Small triangles on sides to show gain position
        juce::Path triangle;
        triangle.addTriangle(static_cast<float>(meterArea.getX()), static_cast<float>(gainY - 3),
                             static_cast<float>(meterArea.getX()), static_cast<float>(gainY + 3),
                             static_cast<float>(meterArea.getX() + 4), static_cast<float>(gainY));
        g.fillPath(triangle);

        triangle.clear();
        triangle.addTriangle(
            static_cast<float>(meterArea.getRight()), static_cast<float>(gainY - 3),
            static_cast<float>(meterArea.getRight()), static_cast<float>(gainY + 3),
            static_cast<float>(meterArea.getRight() - 4), static_cast<float>(gainY));
        g.fillPath(triangle);

        // Border
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        auto fullMeterArea = getLocalBounds().removeFromTop(getHeight() - 14).reduced(2);
        g.drawRoundedRectangle(fullMeterArea.toFloat(), 2.0f, 1.0f);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        dbLabel_.setBounds(bounds.removeFromBottom(14));
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isLeftButtonDown()) {
            dragging_ = true;
            setGainFromY(e.y);
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (dragging_) {
            setGainFromY(e.y);
        }
    }

    void mouseUp(const juce::MouseEvent&) override {
        dragging_ = false;
    }

    void mouseDoubleClick(const juce::MouseEvent&) override {
        // Reset to unity (0 dB)
        setGainDb(0.0);
    }

    // Label::Listener
    void labelTextChanged(juce::Label* label) override {
        if (label == &dbLabel_) {
            auto text = dbLabel_.getText().trim();
            // Remove "dB" suffix if present
            if (text.endsWithIgnoreCase("db")) {
                text = text.dropLastCharacters(2).trim();
            }
            double newDb = text.getDoubleValue();
            setGainDb(newDb);
        }
    }

  private:
    double gainDb_ = 0.0;
    float meterLevel_ = 0.0f;
    float peakLevel_ = 0.0f;
    bool dragging_ = false;
    juce::Label dbLabel_;

    void updateLabel() {
        if (gainDb_ <= -60.0) {
            dbLabel_.setText("-inf", juce::dontSendNotification);
        } else {
            dbLabel_.setText(juce::String(gainDb_, 1), juce::dontSendNotification);
        }
    }

    void setGainFromY(int y) {
        auto meterArea = getLocalBounds().removeFromTop(getHeight() - 14).reduced(2);
        float normalized = 1.0f - static_cast<float>(y - meterArea.getY()) / meterArea.getHeight();
        normalized = juce::jlimit(0.0f, 1.0f, normalized);
        double db = -60.0 + normalized * 66.0;  // -60 to +6 dB range
        setGainDb(db);
    }

    void timerCallback() override {
        // Mock meter animation - simulate audio activity
        // In real implementation, this would receive actual audio levels
        float targetLevel = static_cast<float>((gainDb_ + 60.0) / 66.0) * 0.8f;
        targetLevel += (juce::Random::getSystemRandom().nextFloat() - 0.5f) * 0.1f;
        meterLevel_ = meterLevel_ * 0.9f + targetLevel * 0.1f;
        meterLevel_ = juce::jlimit(0.0f, 1.0f, meterLevel_);
        repaint();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GainMeterComponent)
};

//==============================================================================
// DeviceButtonLookAndFeel - Small buttons with minimal rounding for device slots
//==============================================================================
class DeviceButtonLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& bgColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        // Minimal corner radius (2% of smaller dimension)
        float cornerRadius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.02f;

        auto baseColour = bgColour;
        if (shouldDrawButtonAsDown)
            baseColour = baseColour.darker(0.2f);
        else if (shouldDrawButtonAsHighlighted)
            baseColour = baseColour.brighter(0.1f);

        g.setColour(baseColour);
        g.fillRoundedRectangle(bounds, cornerRadius);

        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool /*isMouseOver*/,
                        bool /*isButtonDown*/) override {
        auto font =
            FontManager::getInstance().getUIFont(DebugSettings::getInstance().getButtonFontSize());
        g.setFont(font);
        g.setColour(button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                              : juce::TextButton::textColourOffId));
        g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
    }
};

//==============================================================================
// DeviceSlotComponent - Interactive device display (inherits from NodeComponent)
// Header layout: [M] [Name...] [gain slider] [UI] [bypass] [X]
// No footer - all controls in header
//==============================================================================
class TrackChainContent::DeviceSlotComponent : public NodeComponent {
  public:
    static constexpr int BASE_WIDTH = 200;
    static constexpr int NUM_PARAMS = 16;
    static constexpr int PARAMS_PER_ROW = 4;

    DeviceSlotComponent(TrackChainContent& owner, magda::TrackId trackId,
                        const magda::DeviceInfo& device)
        : owner_(owner),
          trackId_(trackId),
          device_(device),
          gainSlider_(TextSlider::Format::Decibels) {
        setNodeName(device.name);
        setBypassed(device.bypassed);

        // Restore panel visibility from device state
        modPanelVisible_ = device.modPanelOpen;

        // Hide built-in bypass button - we'll add our own in the header
        setBypassButtonVisible(false);

        // Set up NodeComponent callbacks
        onDeleteClicked = [this]() {
            magda::TrackManager::getInstance().removeDeviceFromTrack(trackId_, device_.id);
        };

        onModPanelToggled = [this](bool visible) {
            if (auto* dev = magda::TrackManager::getInstance().getDevice(trackId_, device_.id)) {
                dev->modPanelOpen = visible;
            }
            owner_.resized();
            owner_.repaint();
        };

        onLayoutChanged = [this]() {
            owner_.resized();
            owner_.repaint();
        };

        // Mod button (toggle mod panel) - sine wave icon
        modButton_ = std::make_unique<magda::SvgButton>("Mod", BinaryData::sinewavebright_svg,
                                                        BinaryData::sinewavebright_svgSize);
        modButton_->setClickingTogglesState(true);
        modButton_->setToggleState(modPanelVisible_, juce::dontSendNotification);
        modButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
        modButton_->setActiveColor(juce::Colours::white);
        modButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        modButton_->setActive(modPanelVisible_);
        modButton_->onClick = [this]() {
            modButton_->setActive(modButton_->getToggleState());
            modPanelVisible_ = modButton_->getToggleState();
            // Side panel shows alongside collapsed strip - no need to expand
            resized();
            repaint();
            if (onModPanelToggled)
                onModPanelToggled(modPanelVisible_);
        };
        addAndMakeVisible(*modButton_);

        // Note: No macro button on devices - params are shown inline

        // Gain text slider in header
        gainSlider_.setRange(-60.0, 12.0, 0.1);
        gainSlider_.setValue(device_.gainDb, juce::dontSendNotification);
        gainSlider_.onValueChanged = [this](double value) {
            if (auto* dev = magda::TrackManager::getInstance().getDevice(trackId_, device_.id)) {
                dev->gainDb = static_cast<float>(value);
            }
        };
        addAndMakeVisible(gainSlider_);

        // UI button (open plugin window) - open in new icon
        uiButton_ = std::make_unique<magda::SvgButton>("UI", BinaryData::open_in_new_svg,
                                                       BinaryData::open_in_new_svgSize);
        uiButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
        uiButton_->onClick = [this]() { DBG("Open plugin UI for: " << device_.name); };
        addAndMakeVisible(*uiButton_);

        // Bypass/On button (power icon)
        onButton_ = std::make_unique<magda::SvgButton>("Power", BinaryData::power_on_svg,
                                                       BinaryData::power_on_svgSize);
        onButton_->setClickingTogglesState(true);
        onButton_->setToggleState(!device.bypassed,
                                  juce::dontSendNotification);  // On = not bypassed
        onButton_->setNormalColor(DarkTheme::getColour(DarkTheme::STATUS_ERROR));
        onButton_->setActiveColor(juce::Colours::white);
        onButton_->setActiveBackgroundColor(
            DarkTheme::getColour(DarkTheme::ACCENT_GREEN).darker(0.3f));
        onButton_->setActive(!device.bypassed);
        onButton_->onClick = [this]() {
            bool active = onButton_->getToggleState();
            onButton_->setActive(active);
            setBypassed(!active);  // Active = not bypassed
            magda::TrackManager::getInstance().setDeviceBypassed(trackId_, device_.id, !active);
        };
        addAndMakeVisible(*onButton_);

        // Create inline param sliders with labels (mock params)
        // clang-format off
        static const char* mockParamNames[NUM_PARAMS] = {
            "Cutoff", "Resonance", "Drive", "Mix",
            "Attack", "Decay", "Sustain", "Release",
            "LFO Rate", "LFO Depth", "Feedback", "Width",
            "Low", "Mid", "High", "Output"
        };
        // clang-format on

        for (int i = 0; i < NUM_PARAMS; ++i) {
            paramLabels_[i] = std::make_unique<juce::Label>();
            paramLabels_[i]->setText(mockParamNames[i], juce::dontSendNotification);
            paramLabels_[i]->setFont(FontManager::getInstance().getUIFont(
                DebugSettings::getInstance().getParamLabelFontSize()));
            paramLabels_[i]->setColour(juce::Label::textColourId,
                                       DarkTheme::getSecondaryTextColour());
            paramLabels_[i]->setJustificationType(juce::Justification::centredLeft);
            paramLabels_[i]->setInterceptsMouseClicks(false, false);  // Pass through for selection
            addAndMakeVisible(*paramLabels_[i]);

            paramSliders_[i] = std::make_unique<TextSlider>(TextSlider::Format::Decimal);
            paramSliders_[i]->setRange(0.0, 1.0, 0.01);
            paramSliders_[i]->setValue(0.5, juce::dontSendNotification);
            addAndMakeVisible(*paramSliders_[i]);
        }
    }

    ~DeviceSlotComponent() override = default;

    int getExpandedWidth() const {
        if (collapsed_) {
            // When collapsed, still add side panel widths
            return getLeftPanelsWidth() + COLLAPSED_WIDTH + getRightPanelsWidth();
        }
        return getTotalWidth(DebugSettings::getInstance().getDeviceSlotWidth());
    }

    bool isModPanelVisible() const {
        return modPanelVisible_;
    }

    magda::DeviceId getDeviceId() const {
        return device_.id;
    }

  protected:
    // No footer for devices
    int getFooterHeight() const override {
        return 0;
    }

    void paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea) override {
        // Manufacturer label at top
        auto labelArea = contentArea.removeFromTop(12);
        auto textColour = isBypassed() ? DarkTheme::getSecondaryTextColour().withAlpha(0.5f)
                                       : DarkTheme::getSecondaryTextColour();
        g.setColour(textColour);
        g.setFont(FontManager::getInstance().getUIFont(8.0f));
        g.drawText(device_.manufacturer, labelArea.reduced(2, 0), juce::Justification::centredLeft);
    }

    void resizedContent(juce::Rectangle<int> contentArea) override {
        // When collapsed, hide content controls only (buttons handled by resizedCollapsed)
        if (collapsed_) {
            for (int i = 0; i < NUM_PARAMS; ++i) {
                paramLabels_[i]->setVisible(false);
                paramSliders_[i]->setVisible(false);
            }
            gainSlider_.setVisible(false);
            return;
        }

        // Show header controls when expanded
        modButton_->setVisible(true);
        uiButton_->setVisible(true);
        onButton_->setVisible(true);
        gainSlider_.setVisible(true);

        // Skip manufacturer label area
        contentArea.removeFromTop(12);
        contentArea = contentArea.reduced(2, 0);

        // Update param label fonts from debug settings
        auto labelFont = FontManager::getInstance().getUIFont(
            DebugSettings::getInstance().getParamLabelFontSize());
        auto valueFont = FontManager::getInstance().getUIFont(
            DebugSettings::getInstance().getParamValueFontSize());
        for (int i = 0; i < NUM_PARAMS; ++i) {
            paramLabels_[i]->setFont(labelFont);
            paramSliders_[i]->setFont(valueFont);
            paramLabels_[i]->setVisible(true);
            paramSliders_[i]->setVisible(true);
        }

        // Layout params in a 4-column grid, scaled to fit available space
        int numRows = (NUM_PARAMS + PARAMS_PER_ROW - 1) / PARAMS_PER_ROW;
        int cellWidth = contentArea.getWidth() / PARAMS_PER_ROW;
        int cellHeight = contentArea.getHeight() / numRows;
        int labelHeight = juce::jmin(10, cellHeight / 3);
        int sliderHeight = cellHeight - labelHeight - 2;

        for (int i = 0; i < NUM_PARAMS; ++i) {
            int row = i / PARAMS_PER_ROW;
            int col = i % PARAMS_PER_ROW;
            int x = contentArea.getX() + col * cellWidth;
            int y = contentArea.getY() + row * cellHeight;

            paramLabels_[i]->setBounds(x, y, cellWidth - 2, labelHeight);
            paramSliders_[i]->setBounds(x, y + labelHeight, cellWidth - 2, sliderHeight);
        }
    }

    void resizedCollapsed(juce::Rectangle<int>& area) override {
        // Add device-specific buttons vertically when collapsed
        // Order: X (from base), ON, UI, Mod
        int buttonSize = juce::jmin(16, area.getWidth() - 4);

        // On/power button (right after X)
        onButton_->setBounds(
            area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
        onButton_->setVisible(true);
        area.removeFromTop(4);

        // UI button
        uiButton_->setBounds(
            area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
        uiButton_->setVisible(true);
        area.removeFromTop(4);

        // Mod button
        modButton_->setBounds(
            area.removeFromTop(buttonSize).withSizeKeepingCentre(buttonSize, buttonSize));
        modButton_->setVisible(true);
    }

    void resizedHeaderExtra(juce::Rectangle<int>& headerArea) override {
        // Header layout: [M] [Name...] [gain slider] [UI] [on] [X]
        // Note: delete (X) is handled by NodeComponent on the right

        // Mod button on the left (before name)
        modButton_->setBounds(headerArea.removeFromLeft(BUTTON_SIZE));
        headerArea.removeFromLeft(4);

        // Power button on the right (before delete which is handled by parent)
        onButton_->setBounds(headerArea.removeFromRight(BUTTON_SIZE));
        headerArea.removeFromRight(4);

        // UI button
        uiButton_->setBounds(headerArea.removeFromRight(BUTTON_SIZE));
        headerArea.removeFromRight(4);

        // Gain slider takes some space on the right
        gainSlider_.setBounds(headerArea.removeFromRight(50));
        headerArea.removeFromRight(4);

        // Remaining space is for the name label (handled by NodeComponent)
    }

    int getModPanelWidth() const override {
        return 60;
    }

  private:
    TrackChainContent& owner_;
    magda::TrackId trackId_;
    magda::DeviceInfo device_;
    std::unique_ptr<magda::SvgButton> modButton_;
    TextSlider gainSlider_;
    std::unique_ptr<magda::SvgButton> uiButton_;
    std::unique_ptr<magda::SvgButton> onButton_;

    std::unique_ptr<juce::Label> paramLabels_[NUM_PARAMS];
    std::unique_ptr<TextSlider> paramSliders_[NUM_PARAMS];
};

//==============================================================================
// ChainContainer - Container for track chain that paints arrows between elements
//==============================================================================
class TrackChainContent::ChainContainer : public juce::Component {
  public:
    explicit ChainContainer(TrackChainContent& owner) : owner_(owner) {}

    void setNodeComponents(const std::vector<std::unique_ptr<NodeComponent>>* nodes) {
        nodeComponents_ = nodes;
    }

    void mouseDown(const juce::MouseEvent& /*e*/) override {
        // Clicking empty area deselects all devices
        owner_.clearDeviceSelection();
    }

    void paint(juce::Graphics& g) override {
        // Draw arrows between elements
        int arrowY = getHeight() / 2;
        g.setColour(DarkTheme::getSecondaryTextColour());

        // Draw arrows after each node (except the last one)
        if (nodeComponents_) {
            for (size_t i = 0; i + 1 < nodeComponents_->size(); ++i) {
                int x = (*nodeComponents_)[i]->getRight();
                drawArrow(g, x, arrowY);
            }
        }

        // Draw insertion indicator during drag
        if (owner_.dragInsertIndex_ >= 0) {
            int indicatorX = owner_.calculateIndicatorX(owner_.dragInsertIndex_);
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
            g.fillRect(indicatorX - 2, 0, 4, getHeight());
        }

        // Draw ghost image during drag
        if (owner_.dragGhostImage_.isValid()) {
            g.setOpacity(0.6f);
            int ghostX = owner_.dragMousePos_.x - owner_.dragGhostImage_.getWidth() / 2;
            int ghostY = owner_.dragMousePos_.y - owner_.dragGhostImage_.getHeight() / 2;
            g.drawImageAt(owner_.dragGhostImage_, ghostX, ghostY);
            g.setOpacity(1.0f);
        }
    }

  private:
    void drawArrow(juce::Graphics& g, int x, int y) {
        int arrowStart = x + 4;
        int arrowEnd = x + 16;
        g.drawLine(static_cast<float>(arrowStart), static_cast<float>(y),
                   static_cast<float>(arrowEnd), static_cast<float>(y), 1.5f);
        // Arrow head
        g.drawLine(static_cast<float>(arrowEnd - 4), static_cast<float>(y - 3),
                   static_cast<float>(arrowEnd), static_cast<float>(y), 1.5f);
        g.drawLine(static_cast<float>(arrowEnd - 4), static_cast<float>(y + 3),
                   static_cast<float>(arrowEnd), static_cast<float>(y), 1.5f);
    }

    TrackChainContent& owner_;
    const std::vector<std::unique_ptr<NodeComponent>>* nodeComponents_ = nullptr;
};

// dB conversion helpers
namespace {
constexpr float MIN_DB = -60.0f;

float gainToDb(float gain) {
    if (gain <= 0.0f)
        return MIN_DB;
    return 20.0f * std::log10(gain);
}

float dbToGain(float db) {
    if (db <= MIN_DB)
        return 0.0f;
    return std::pow(10.0f, db / 20.0f);
}
}  // namespace

TrackChainContent::TrackChainContent() : chainContainer_(std::make_unique<ChainContainer>(*this)) {
    setName("Track Chain");

    // Listen for debug settings changes
    DebugSettings::getInstance().addListener([this]() {
        // Force all node components to update their fonts
        for (auto& node : nodeComponents_) {
            node->resized();
            node->repaint();
        }
        resized();
        repaint();
    });

    // Viewport for horizontal scrolling of chain content
    chainViewport_.setViewedComponent(chainContainer_.get(), false);
    chainViewport_.setScrollBarsShown(false, true);  // Horizontal only
    addAndMakeVisible(chainViewport_);

    // No selection label
    noSelectionLabel_.setText("Select a track to view its signal chain",
                              juce::dontSendNotification);
    noSelectionLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    noSelectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    noSelectionLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(noSelectionLabel_);

    // === HEADER BAR CONTROLS - LEFT SIDE (action buttons) ===

    // Global mods toggle button (sine wave icon - same as rack/device mod buttons)
    globalModsButton_ = std::make_unique<magda::SvgButton>("Mod", BinaryData::sinewavebright_svg,
                                                           BinaryData::sinewavebright_svgSize);
    globalModsButton_->setClickingTogglesState(true);
    globalModsButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    globalModsButton_->setActiveColor(juce::Colours::white);
    globalModsButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    globalModsButton_->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    globalModsButton_->onClick = [this]() {
        globalModsButton_->setActive(globalModsButton_->getToggleState());
        globalModsVisible_ = globalModsButton_->getToggleState();
        resized();
        repaint();
    };
    addChildComponent(*globalModsButton_);

    // Link button (parameter linking)
    linkButton_ = std::make_unique<magda::SvgButton>("Link", BinaryData::link_bright_svg,
                                                     BinaryData::link_bright_svgSize);
    linkButton_->setClickingTogglesState(true);
    linkButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    linkButton_->setActiveColor(juce::Colours::white);
    linkButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    linkButton_->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    linkButton_->onClick = [this]() {
        linkButton_->setActive(linkButton_->getToggleState());
        // TODO: Toggle parameter linking mode
        DBG("Link mode: " << (linkButton_->getToggleState() ? "ON" : "OFF"));
    };
    addChildComponent(*linkButton_);

    // Add rack button (rack icon with blue fill, grey border)
    addRackButton_ =
        std::make_unique<magda::SvgButton>("Rack", BinaryData::rack_svg, BinaryData::rack_svgSize);
    addRackButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));  // Match SVG fill color
    addRackButton_->setNormalColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    addRackButton_->setHoverColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).brighter(0.2f));
    addRackButton_->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    addRackButton_->onClick = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::TrackManager::getInstance().addRackToTrack(selectedTrackId_);
        }
    };
    addChildComponent(*addRackButton_);

    // Tree view button (show chain tree dialog)
    treeViewButton_ =
        std::make_unique<magda::SvgButton>("Tree", BinaryData::tree_svg, BinaryData::tree_svgSize);
    treeViewButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    treeViewButton_->setHoverColor(DarkTheme::getTextColour());
    treeViewButton_->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    treeViewButton_->onClick = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::ChainTreeDialog::show(selectedTrackId_);
        }
    };
    addChildComponent(*treeViewButton_);

    // === HEADER BAR CONTROLS - RIGHT SIDE (track info) ===

    // Track name label - clicks pass through for track selection
    trackNameLabel_.setFont(FontManager::getInstance().getUIFontBold(11.0f));
    trackNameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    trackNameLabel_.setJustificationType(juce::Justification::centredRight);
    trackNameLabel_.setInterceptsMouseClicks(false, false);
    addChildComponent(trackNameLabel_);

    // Mute button
    muteButton_.setButtonText("M");
    muteButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    muteButton_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::STATUS_WARNING));
    muteButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    muteButton_.setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    muteButton_.setClickingTogglesState(true);
    muteButton_.onClick = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::TrackManager::getInstance().setTrackMuted(selectedTrackId_,
                                                             muteButton_.getToggleState());
        }
    };
    muteButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addChildComponent(muteButton_);

    // Solo button
    soloButton_.setButtonText("S");
    soloButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    soloButton_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    soloButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    soloButton_.setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    soloButton_.setClickingTogglesState(true);
    soloButton_.onClick = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::TrackManager::getInstance().setTrackSoloed(selectedTrackId_,
                                                              soloButton_.getToggleState());
        }
    };
    soloButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    addChildComponent(soloButton_);

    // Volume text slider (dB format)
    volumeSlider_.setRange(-60.0, 6.0, 0.1);
    volumeSlider_.setValue(0.0, juce::dontSendNotification);  // Unity gain (0 dB)
    volumeSlider_.onValueChanged = [this](double db) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            float gain = dbToGain(static_cast<float>(db));
            magda::TrackManager::getInstance().setTrackVolume(selectedTrackId_, gain);
        }
    };
    addChildComponent(volumeSlider_);

    // Pan text slider
    panSlider_.setRange(-1.0, 1.0, 0.01);
    panSlider_.setValue(0.0, juce::dontSendNotification);  // Center
    panSlider_.onValueChanged = [this](double pan) {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::TrackManager::getInstance().setTrackPan(selectedTrackId_,
                                                           static_cast<float>(pan));
        }
    };
    addChildComponent(panSlider_);

    // Chain bypass button (power icon - same as device bypass buttons)
    chainBypassButton_ = std::make_unique<magda::SvgButton>("Power", BinaryData::power_on_svg,
                                                            BinaryData::power_on_svgSize);
    chainBypassButton_->setClickingTogglesState(true);
    chainBypassButton_->setToggleState(true,
                                       juce::dontSendNotification);  // Start active (not bypassed)
    chainBypassButton_->setNormalColor(DarkTheme::getColour(DarkTheme::STATUS_ERROR));
    chainBypassButton_->setActiveColor(juce::Colours::white);
    chainBypassButton_->setActiveBackgroundColor(
        DarkTheme::getColour(DarkTheme::ACCENT_GREEN).darker(0.3f));
    chainBypassButton_->setActive(true);  // Start active
    chainBypassButton_->onClick = [this]() {
        bool active = chainBypassButton_->getToggleState();
        chainBypassButton_->setActive(active);
        // TODO: Actually bypass all devices in the track chain
        DBG("Track chain bypass: " << (active ? "ACTIVE" : "BYPASSED"));
        repaint();
    };
    addChildComponent(*chainBypassButton_);

    // Register as listeners
    magda::TrackManager::getInstance().addListener(this);
    magda::SelectionManager::getInstance().addListener(this);

    // Check if there's already a selected track
    selectedTrackId_ = magda::TrackManager::getInstance().getSelectedTrack();
    updateFromSelectedTrack();
}

TrackChainContent::~TrackChainContent() {
    magda::TrackManager::getInstance().removeListener(this);
    magda::SelectionManager::getInstance().removeListener(this);
}

void TrackChainContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());

    if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
        auto bounds = getLocalBounds();

        // Draw header background - use accent color only when track itself is selected
        // (not when a chain node is selected)
        auto headerArea = bounds.removeFromTop(HEADER_HEIGHT);
        bool trackIsSelected = magda::SelectionManager::getInstance().getSelectionType() ==
                               magda::SelectionType::Track;
        g.setColour(trackIsSelected ? DarkTheme::getColour(DarkTheme::ACCENT_CYAN).withAlpha(0.08f)
                                    : DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRect(headerArea);

        // Header bottom border
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawHorizontalLine(HEADER_HEIGHT - 1, 0.0f, static_cast<float>(getWidth()));

        // Draw global mods panel on left if visible
        if (globalModsVisible_) {
            auto modsArea = bounds.removeFromLeft(MODS_PANEL_WIDTH);
            g.setColour(DarkTheme::getColour(DarkTheme::SURFACE).darker(0.1f));
            g.fillRect(modsArea);

            // Panel border
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.drawVerticalLine(modsArea.getRight() - 1, static_cast<float>(modsArea.getY()),
                               static_cast<float>(modsArea.getBottom()));

            // Panel header
            auto modsPanelHeader = modsArea.removeFromTop(24).reduced(8, 4);
            g.setColour(DarkTheme::getTextColour());
            g.setFont(FontManager::getInstance().getUIFontBold(10.0f));
            g.drawText("MODULATORS", modsPanelHeader, juce::Justification::centredLeft);

            // Placeholder content
            auto modsContent = modsArea.reduced(8);
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(FontManager::getInstance().getUIFont(9.0f));

            int y = modsContent.getY();
            g.drawText("+ Add LFO", modsContent.getX(), y, modsContent.getWidth(), 20,
                       juce::Justification::centredLeft);
            y += 24;
            g.drawText("+ Add Envelope", modsContent.getX(), y, modsContent.getWidth(), 20,
                       juce::Justification::centredLeft);
            y += 24;
            g.drawText("+ Add Random", modsContent.getX(), y, modsContent.getWidth(), 20,
                       juce::Justification::centredLeft);
        }

        // Arrows between chain elements are drawn by ChainContainer::paint
        // which scrolls correctly with the viewport
    }
}

void TrackChainContent::mouseDown(const juce::MouseEvent& e) {
    // Click on header area selects the track
    if (selectedTrackId_ != magda::INVALID_TRACK_ID && e.y < HEADER_HEIGHT) {
        magda::SelectionManager::getInstance().selectTrack(selectedTrackId_);
    }
}

void TrackChainContent::resized() {
    auto bounds = getLocalBounds();

    if (selectedTrackId_ == magda::INVALID_TRACK_ID) {
        noSelectionLabel_.setBounds(bounds);
        showHeader(false);
    } else {
        noSelectionLabel_.setVisible(false);

        // === HEADER BAR LAYOUT ===
        // Layout: MOD RACK+ RACK-MB+ ... Name | gain | ON
        auto headerArea = bounds.removeFromTop(HEADER_HEIGHT).reduced(8, 4);

        // LEFT SIDE - Action buttons
        globalModsButton_->setBounds(headerArea.removeFromLeft(20));
        headerArea.removeFromLeft(2);
        linkButton_->setBounds(headerArea.removeFromLeft(20));
        headerArea.removeFromLeft(8);
        addRackButton_->setBounds(headerArea.removeFromLeft(20));
        headerArea.removeFromLeft(4);
        treeViewButton_->setBounds(headerArea.removeFromLeft(20));
        headerArea.removeFromLeft(16);

        // RIGHT SIDE - Track info (from right to left)
        chainBypassButton_->setBounds(headerArea.removeFromRight(17));
        headerArea.removeFromRight(4);
        panSlider_.setBounds(headerArea.removeFromRight(40));
        headerArea.removeFromRight(4);
        volumeSlider_.setBounds(headerArea.removeFromRight(50));
        headerArea.removeFromRight(4);
        soloButton_.setBounds(headerArea.removeFromRight(18));
        headerArea.removeFromRight(2);
        muteButton_.setBounds(headerArea.removeFromRight(18));
        headerArea.removeFromRight(8);
        trackNameLabel_.setBounds(headerArea);  // Name takes remaining space

        showHeader(true);

        // === MODS PANEL (left side, if visible) ===
        if (globalModsVisible_) {
            bounds.removeFromLeft(MODS_PANEL_WIDTH);
        }

        // === CONTENT AREA LAYOUT ===
        // Everything flows horizontally: [Device] → [Device] → [Rack] → [Rack] → ...
        // ChainPanel is displayed within the rack when a chain is selected
        auto contentArea = bounds.reduced(8);

        // Viewport fills the content area
        chainViewport_.setBounds(contentArea);

        // Layout chain content inside the container
        layoutChainContent();
    }
}

void TrackChainContent::layoutChainContent() {
    auto viewportBounds = chainViewport_.getLocalBounds();
    int chainHeight = viewportBounds.getHeight();
    int availableWidth = viewportBounds.getWidth();

    // Calculate total content width
    int totalWidth = calculateTotalContentWidth();

    // Account for scrollbar if needed
    if (totalWidth > availableWidth) {
        chainHeight -= 8;  // Space for scrollbar
    }

    // Set container size
    chainContainer_->setSize(juce::jmax(totalWidth, availableWidth), chainHeight);
    chainContainer_->setNodeComponents(&nodeComponents_);

    // Layout all node components horizontally
    int x = 0;
    for (auto& node : nodeComponents_) {
        // Check if it's a RackComponent to set available width
        if (auto* rack = dynamic_cast<RackComponent*>(node.get())) {
            int remainingWidth = juce::jmax(300, availableWidth - x - ARROW_WIDTH - SLOT_SPACING);
            rack->setAvailableWidth(remainingWidth);
        }

        int nodeWidth = node->getPreferredWidth();
        node->setBounds(x, 0, nodeWidth, chainHeight);
        x += nodeWidth + ARROW_WIDTH + SLOT_SPACING;
    }
}

int TrackChainContent::calculateTotalContentWidth() const {
    int totalWidth = 0;

    // Add width for all node components
    for (const auto& node : nodeComponents_) {
        totalWidth += node->getPreferredWidth() + ARROW_WIDTH + SLOT_SPACING;
    }

    return totalWidth;
}

void TrackChainContent::onActivated() {
    selectedTrackId_ = magda::TrackManager::getInstance().getSelectedTrack();
    updateFromSelectedTrack();
}

void TrackChainContent::onDeactivated() {
    // Nothing to do
}

void TrackChainContent::tracksChanged() {
    if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
        const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
        if (!track) {
            selectedTrackId_ = magda::INVALID_TRACK_ID;
            updateFromSelectedTrack();
        }
    }
}

void TrackChainContent::trackPropertyChanged(int trackId) {
    if (static_cast<magda::TrackId>(trackId) == selectedTrackId_) {
        updateFromSelectedTrack();
    }
}

void TrackChainContent::trackSelectionChanged(magda::TrackId trackId) {
    selectedTrackId_ = trackId;
    updateFromSelectedTrack();
}

void TrackChainContent::trackDevicesChanged(magda::TrackId trackId) {
    if (trackId == selectedTrackId_) {
        rebuildNodeComponents();
    }
}

void TrackChainContent::selectionTypeChanged(magda::SelectionType /*newType*/) {
    // Repaint header when selection type changes (Track vs ChainNode)
    // to update the header background color
    repaint();
}

void TrackChainContent::updateFromSelectedTrack() {
    if (selectedTrackId_ == magda::INVALID_TRACK_ID) {
        showHeader(false);
        noSelectionLabel_.setVisible(true);
        nodeComponents_.clear();
    } else {
        const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
        if (track) {
            trackNameLabel_.setText(track->name, juce::dontSendNotification);

            // Update mute/solo state
            muteButton_.setToggleState(track->muted, juce::dontSendNotification);
            soloButton_.setToggleState(track->soloed, juce::dontSendNotification);

            // Convert linear gain to dB for volume slider
            float db = gainToDb(track->volume);
            volumeSlider_.setValue(db, juce::dontSendNotification);

            // Update pan slider
            panSlider_.setValue(track->pan, juce::dontSendNotification);

            // Reset chain bypass button state (active = not bypassed)
            chainBypassButton_->setToggleState(true, juce::dontSendNotification);
            chainBypassButton_->setActive(true);

            showHeader(true);
            noSelectionLabel_.setVisible(false);
            rebuildNodeComponents();
        } else {
            showHeader(false);
            noSelectionLabel_.setVisible(true);
            nodeComponents_.clear();
        }
    }

    resized();
    repaint();
}

void TrackChainContent::showHeader(bool show) {
    // Left side - action buttons
    globalModsButton_->setVisible(show);
    linkButton_->setVisible(show);
    addRackButton_->setVisible(show);
    treeViewButton_->setVisible(show);
    // Right side - track info
    trackNameLabel_.setVisible(show);
    muteButton_.setVisible(show);
    soloButton_.setVisible(show);
    volumeSlider_.setVisible(show);
    panSlider_.setVisible(show);
    chainBypassButton_->setVisible(show);
}

void TrackChainContent::rebuildNodeComponents() {
    // Clear existing components
    unfocusAllComponents();
    nodeComponents_.clear();

    if (selectedTrackId_ == magda::INVALID_TRACK_ID) {
        return;
    }

    const auto& elements = magda::TrackManager::getInstance().getChainElements(selectedTrackId_);

    // Create a component for each chain element
    for (size_t i = 0; i < elements.size(); ++i) {
        const auto& element = elements[i];

        if (magda::isDevice(element)) {
            // Create device slot component
            const auto& device = magda::getDevice(element);
            auto slot = std::make_unique<DeviceSlotComponent>(*this, selectedTrackId_, device);
            slot->setNodePath(magda::ChainNodePath::topLevelDevice(selectedTrackId_, device.id));

            // Wire up drag-to-reorder callbacks
            slot->onDragStart = [this](NodeComponent* node, const juce::MouseEvent&) {
                draggedNode_ = node;
                dragOriginalIndex_ = findNodeIndex(node);
                dragInsertIndex_ = dragOriginalIndex_;
                // Capture ghost image and make original semi-transparent
                dragGhostImage_ = node->createComponentSnapshot(node->getLocalBounds());
                node->setAlpha(0.4f);
            };

            slot->onDragMove = [this](NodeComponent*, const juce::MouseEvent& e) {
                auto pos = e.getEventRelativeTo(chainContainer_.get()).getPosition();
                dragInsertIndex_ = calculateInsertIndex(pos.x);
                dragMousePos_ = pos;
                chainContainer_->repaint();
            };

            slot->onDragEnd = [this](NodeComponent* node, const juce::MouseEvent&) {
                // Restore alpha and clear ghost
                node->setAlpha(1.0f);
                dragGhostImage_ = juce::Image();

                int nodeCount = static_cast<int>(nodeComponents_.size());
                if (dragOriginalIndex_ >= 0 && dragInsertIndex_ >= 0 &&
                    dragOriginalIndex_ != dragInsertIndex_) {
                    // Convert insert position to target index
                    int targetIndex = dragInsertIndex_;
                    if (dragInsertIndex_ > dragOriginalIndex_) {
                        targetIndex = dragInsertIndex_ - 1;
                    }
                    targetIndex = juce::jlimit(0, nodeCount - 1, targetIndex);
                    if (targetIndex != dragOriginalIndex_) {
                        magda::TrackManager::getInstance().moveNode(
                            selectedTrackId_, dragOriginalIndex_, targetIndex);
                    }
                }
                draggedNode_ = nullptr;
                dragOriginalIndex_ = -1;
                dragInsertIndex_ = -1;
                chainContainer_->repaint();
            };

            chainContainer_->addAndMakeVisible(*slot);
            nodeComponents_.push_back(std::move(slot));

        } else if (magda::isRack(element)) {
            // Create rack component
            const auto& rack = magda::getRack(element);
            auto rackComp = std::make_unique<RackComponent>(selectedTrackId_, rack);
            rackComp->setNodePath(magda::ChainNodePath::rack(selectedTrackId_, rack.id));

            // Wire up callbacks
            rackComp->onSelected = [this]() { selectedDeviceId_ = magda::INVALID_DEVICE_ID; };
            rackComp->onLayoutChanged = [this]() {
                resized();
                repaint();
            };
            rackComp->onChainSelected = [this](magda::TrackId trackId, magda::RackId rId,
                                               magda::ChainId chainId) {
                onChainSelected(trackId, rId, chainId);
            };
            rackComp->onDeviceSelected = [this](magda::DeviceId deviceId) {
                if (deviceId != magda::INVALID_DEVICE_ID) {
                    selectedDeviceId_ = magda::INVALID_DEVICE_ID;
                    magda::SelectionManager::getInstance().selectDevice(
                        selectedTrackId_, selectedRackId_, selectedChainId_, deviceId);
                } else {
                    magda::SelectionManager::getInstance().clearDeviceSelection();
                }
            };

            // Wire up drag-to-reorder callbacks
            rackComp->onDragStart = [this](NodeComponent* node, const juce::MouseEvent&) {
                draggedNode_ = node;
                dragOriginalIndex_ = findNodeIndex(node);
                dragInsertIndex_ = dragOriginalIndex_;
                // Capture ghost image and make original semi-transparent
                dragGhostImage_ = node->createComponentSnapshot(node->getLocalBounds());
                node->setAlpha(0.4f);
            };

            rackComp->onDragMove = [this](NodeComponent*, const juce::MouseEvent& e) {
                auto pos = e.getEventRelativeTo(chainContainer_.get()).getPosition();
                dragInsertIndex_ = calculateInsertIndex(pos.x);
                dragMousePos_ = pos;
                chainContainer_->repaint();
            };

            rackComp->onDragEnd = [this](NodeComponent* node, const juce::MouseEvent&) {
                // Restore alpha and clear ghost
                node->setAlpha(1.0f);
                dragGhostImage_ = juce::Image();

                int nodeCount = static_cast<int>(nodeComponents_.size());
                if (dragOriginalIndex_ >= 0 && dragInsertIndex_ >= 0 &&
                    dragOriginalIndex_ != dragInsertIndex_) {
                    int targetIndex = dragInsertIndex_;
                    if (dragInsertIndex_ > dragOriginalIndex_) {
                        targetIndex = dragInsertIndex_ - 1;
                    }
                    targetIndex = juce::jlimit(0, nodeCount - 1, targetIndex);
                    if (targetIndex != dragOriginalIndex_) {
                        magda::TrackManager::getInstance().moveNode(
                            selectedTrackId_, dragOriginalIndex_, targetIndex);
                    }
                }
                draggedNode_ = nullptr;
                dragOriginalIndex_ = -1;
                dragInsertIndex_ = -1;
                chainContainer_->repaint();
            };

            chainContainer_->addAndMakeVisible(*rackComp);
            nodeComponents_.push_back(std::move(rackComp));
        }
    }

    // Restore selection state from SelectionManager
    const auto& selectedPath = magda::SelectionManager::getInstance().getSelectedChainNode();
    if (selectedPath.isValid() && selectedPath.trackId == selectedTrackId_) {
        for (auto& node : nodeComponents_) {
            if (node->getNodePath() == selectedPath) {
                node->setSelected(true);
                break;
            }
        }
    }

    resized();
    repaint();
}

void TrackChainContent::onChainSelected(magda::TrackId trackId, magda::RackId rackId,
                                        magda::ChainId chainId) {
    // Store selection locally
    selectedRackId_ = rackId;
    selectedChainId_ = chainId;
    (void)trackId;  // Already tracked via selectedTrackId_

    // Notify TrackManager of chain selection (for plugin browser)
    magda::TrackManager::getInstance().setSelectedChain(selectedTrackId_, rackId, chainId);

    // Clear selection in other racks (hide their chain panels)
    for (auto& node : nodeComponents_) {
        if (auto* rack = dynamic_cast<RackComponent*>(node.get())) {
            if (rack->getRackId() != rackId) {
                rack->clearChainSelection();
                rack->hideChainPanel();
            }
        }
    }

    // Relayout since rack widths may have changed
    resized();
    repaint();
}

bool TrackChainContent::hasSelectedTrack() const {
    return selectedTrackId_ != magda::INVALID_TRACK_ID;
}

bool TrackChainContent::hasSelectedChain() const {
    return selectedTrackId_ != magda::INVALID_TRACK_ID &&
           selectedRackId_ != magda::INVALID_RACK_ID && selectedChainId_ != magda::INVALID_CHAIN_ID;
}

void TrackChainContent::addDeviceToSelectedTrack(const magda::DeviceInfo& device) {
    if (!hasSelectedTrack()) {
        return;
    }
    magda::TrackManager::getInstance().addDeviceToTrack(selectedTrackId_, device);
}

void TrackChainContent::addDeviceToSelectedChain(const magda::DeviceInfo& device) {
    if (!hasSelectedChain()) {
        return;
    }
    magda::TrackManager::getInstance().addDeviceToChain(selectedTrackId_, selectedRackId_,
                                                        selectedChainId_, device);
}

void TrackChainContent::clearDeviceSelection() {
    DBG("TrackChainContent::clearDeviceSelection");
    selectedDeviceId_ = magda::INVALID_DEVICE_ID;

    // Clear selection on all node components
    for (auto& node : nodeComponents_) {
        node->setSelected(false);
        // Also clear device selection in rack components (but keep chain panel open)
        if (auto* rack = dynamic_cast<RackComponent*>(node.get())) {
            rack->clearDeviceSelection();
        }
    }
    // Notify SelectionManager - this will update inspector
    magda::SelectionManager::getInstance().clearDeviceSelection();
}

void TrackChainContent::onDeviceSlotSelected(magda::DeviceId deviceId) {
    DBG("TrackChainContent::onDeviceSlotSelected deviceId=" + juce::String(deviceId));
    selectedDeviceId_ = deviceId;

    // Update selection state on all node components
    for (auto& node : nodeComponents_) {
        if (auto* slot = dynamic_cast<DeviceSlotComponent*>(node.get())) {
            bool shouldSelect = slot->getDeviceId() == deviceId;
            slot->setSelected(shouldSelect);
        } else if (auto* rack = dynamic_cast<RackComponent*>(node.get())) {
            // Clear device/chain selection in racks (but keep chain panel open)
            rack->clearDeviceSelection();
            rack->clearChainSelection();  // Clear chain row selection border
            rack->setSelected(false);     // Deselect the rack itself too
        }
    }
    // Notify SelectionManager - this will update inspector
    magda::SelectionManager::getInstance().selectDevice(selectedTrackId_, deviceId);
}

int TrackChainContent::findNodeIndex(NodeComponent* node) const {
    for (size_t i = 0; i < nodeComponents_.size(); ++i) {
        if (nodeComponents_[i].get() == node) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int TrackChainContent::calculateInsertIndex(int mouseX) const {
    // Find insert position based on mouse X and node midpoints
    for (size_t i = 0; i < nodeComponents_.size(); ++i) {
        int midX = nodeComponents_[i]->getX() + nodeComponents_[i]->getWidth() / 2;
        if (mouseX < midX) {
            return static_cast<int>(i);
        }
    }
    // After last element
    return static_cast<int>(nodeComponents_.size());
}

int TrackChainContent::calculateIndicatorX(int index) const {
    // Before first element
    if (index == 0) {
        if (!nodeComponents_.empty()) {
            return nodeComponents_[0]->getX() - 4;
        }
        return 8;  // Default padding
    }

    // After previous element
    if (index > 0 && index <= static_cast<int>(nodeComponents_.size())) {
        return nodeComponents_[index - 1]->getRight() + ARROW_WIDTH / 2;
    }

    // Fallback
    return 8;
}

}  // namespace magda::daw::ui
