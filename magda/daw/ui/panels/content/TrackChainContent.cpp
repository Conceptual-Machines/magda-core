#include "TrackChainContent.hpp"

#include <BinaryData.h>

#include <cmath>

#include "../../debug/DebugSettings.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/MixerMetrics.hpp"
#include "../../themes/SmallButtonLookAndFeel.hpp"
#include "core/DeviceInfo.hpp"
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
        modButton_ = std::make_unique<magda::SvgButton>("Mod", BinaryData::sinewave_svg,
                                                        BinaryData::sinewave_svgSize);
        modButton_->setClickingTogglesState(true);
        modButton_->setToggleState(modPanelVisible_, juce::dontSendNotification);
        modButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
        modButton_->setActiveColor(juce::Colours::white);
        modButton_->setActiveBackgroundColor(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        modButton_->setActive(modPanelVisible_);
        modButton_->onClick = [this]() {
            modButton_->setActive(modButton_->getToggleState());
            modPanelVisible_ = modButton_->getToggleState();
            if (onModPanelToggled)
                onModPanelToggled(modPanelVisible_);
        };
        addAndMakeVisible(*modButton_);

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

        // Bypass/On button (power symbol)
        onButton_.setButtonText(juce::String::fromUTF8("\xe2\x8f\xbb"));  // ⏻ power symbol
        // OFF state (not bypassed = active) = darker green background
        onButton_.setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::ACCENT_GREEN).darker(0.3f));
        // ON state (bypassed) = reddish background
        onButton_.setColour(juce::TextButton::buttonOnColourId,
                            DarkTheme::getColour(DarkTheme::STATUS_ERROR));
        onButton_.setColour(juce::TextButton::textColourOffId,
                            DarkTheme::getColour(DarkTheme::BACKGROUND));
        onButton_.setColour(juce::TextButton::textColourOnId,
                            DarkTheme::getColour(DarkTheme::BACKGROUND));
        onButton_.setClickingTogglesState(true);
        onButton_.setToggleState(device.bypassed, juce::dontSendNotification);
        onButton_.onClick = [this]() {
            bool bypassed = onButton_.getToggleState();
            setBypassed(bypassed);
            magda::TrackManager::getInstance().setDeviceBypassed(trackId_, device_.id, bypassed);
        };
        onButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
        addAndMakeVisible(onButton_);

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
            addAndMakeVisible(*paramLabels_[i]);

            paramSliders_[i] = std::make_unique<TextSlider>(TextSlider::Format::Decimal);
            paramSliders_[i]->setRange(0.0, 1.0, 0.01);
            paramSliders_[i]->setValue(0.5, juce::dontSendNotification);
            addAndMakeVisible(*paramSliders_[i]);
        }
    }

    ~DeviceSlotComponent() override {
        onButton_.setLookAndFeel(nullptr);
    }

    int getExpandedWidth() const {
        return getTotalWidth(DebugSettings::getInstance().getDeviceSlotWidth());
    }

    bool isModPanelVisible() const {
        return modPanelVisible_;
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

    void resizedHeaderExtra(juce::Rectangle<int>& headerArea) override {
        // Header layout: [M] [Name...] [gain slider] [UI] [on] [X]
        // Note: delete (X) is handled by NodeComponent on the right

        // Mod button on the left (before name)
        modButton_->setBounds(headerArea.removeFromLeft(BUTTON_SIZE));
        headerArea.removeFromLeft(4);

        // Power button on the right (before delete which is handled by parent)
        onButton_.setBounds(headerArea.removeFromRight(BUTTON_SIZE));
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
    juce::TextButton onButton_;

    std::unique_ptr<juce::Label> paramLabels_[NUM_PARAMS];
    std::unique_ptr<TextSlider> paramSliders_[NUM_PARAMS];
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

TrackChainContent::TrackChainContent() {
    setName("Track Chain");

    // Listen for debug settings changes
    DebugSettings::getInstance().addListener([this]() {
        // Force all device slots to update their fonts
        for (auto& slot : deviceSlots_) {
            slot->resized();
            slot->repaint();
        }
        resized();
        repaint();
    });

    // No selection label
    noSelectionLabel_.setText("Select a track to view its signal chain",
                              juce::dontSendNotification);
    noSelectionLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    noSelectionLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    noSelectionLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(noSelectionLabel_);

    // === HEADER BAR CONTROLS - LEFT SIDE (action buttons) ===

    // Global mods toggle button
    globalModsButton_.setButtonText("MOD");
    globalModsButton_.setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
    globalModsButton_.setColour(juce::TextButton::buttonOnColourId,
                                DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
    globalModsButton_.setColour(juce::TextButton::textColourOffId,
                                DarkTheme::getSecondaryTextColour());
    globalModsButton_.setColour(juce::TextButton::textColourOnId,
                                DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    globalModsButton_.setClickingTogglesState(true);
    globalModsButton_.onClick = [this]() {
        globalModsVisible_ = globalModsButton_.getToggleState();
        resized();
        repaint();
    };
    addChildComponent(globalModsButton_);

    // Add rack button
    addRackButton_.setButtonText("RACK+");
    addRackButton_.setColour(juce::TextButton::buttonColourId,
                             DarkTheme::getColour(DarkTheme::SURFACE));
    addRackButton_.setColour(juce::TextButton::textColourOffId,
                             DarkTheme::getSecondaryTextColour());
    addRackButton_.onClick = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            magda::TrackManager::getInstance().addRackToTrack(selectedTrackId_);
        }
    };
    addChildComponent(addRackButton_);

    // Add multi-band rack button
    addMultibandRackButton_.setButtonText("RACK-MB+");
    addMultibandRackButton_.setColour(juce::TextButton::buttonColourId,
                                      DarkTheme::getColour(DarkTheme::SURFACE));
    addMultibandRackButton_.setColour(juce::TextButton::textColourOffId,
                                      DarkTheme::getSecondaryTextColour());
    addMultibandRackButton_.onClick = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            // TODO: Add multi-band rack with frequency splits
            magda::TrackManager::getInstance().addRackToTrack(selectedTrackId_, "MB Rack");
        }
    };
    addChildComponent(addMultibandRackButton_);

    // === HEADER BAR CONTROLS - RIGHT SIDE (track info) ===

    // Track name label
    trackNameLabel_.setFont(FontManager::getInstance().getUIFontBold(11.0f));
    trackNameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    trackNameLabel_.setJustificationType(juce::Justification::centredRight);
    addChildComponent(trackNameLabel_);

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

    // Chain bypass button (on/off for entire track chain)
    chainBypassButton_.setButtonText("ON");
    chainBypassButton_.setColour(juce::TextButton::buttonColourId,
                                 DarkTheme::getColour(DarkTheme::ACCENT_GREEN).darker(0.3f));
    chainBypassButton_.setColour(juce::TextButton::buttonOnColourId,
                                 DarkTheme::getColour(DarkTheme::SURFACE));
    chainBypassButton_.setColour(juce::TextButton::textColourOffId,
                                 DarkTheme::getColour(DarkTheme::BACKGROUND));
    chainBypassButton_.setColour(juce::TextButton::textColourOnId,
                                 DarkTheme::getSecondaryTextColour());
    chainBypassButton_.setClickingTogglesState(true);
    chainBypassButton_.onClick = [this]() {
        // When toggled ON (getToggleState() == true), chain is BYPASSED
        bool bypassed = chainBypassButton_.getToggleState();
        chainBypassButton_.setButtonText(bypassed ? "OFF" : "ON");
        // TODO: Actually bypass all devices in the track chain
        DBG("Track chain bypass: " << (bypassed ? "BYPASSED" : "ACTIVE"));
        repaint();
    };
    addChildComponent(chainBypassButton_);

    // Register as listener
    magda::TrackManager::getInstance().addListener(this);

    // Check if there's already a selected track
    selectedTrackId_ = magda::TrackManager::getInstance().getSelectedTrack();
    updateFromSelectedTrack();
}

TrackChainContent::~TrackChainContent() {
    magda::TrackManager::getInstance().removeListener(this);
}

void TrackChainContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());

    if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
        auto bounds = getLocalBounds();

        // Draw header background
        auto headerArea = bounds.removeFromTop(HEADER_HEIGHT);
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
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

        // Draw arrows between all chain elements (devices and racks)
        auto contentArea = bounds.reduced(8);
        int chainHeight = contentArea.getHeight();
        int arrowWidth = 20;
        int slotSpacing = 8;
        int arrowY = contentArea.getY() + chainHeight / 2;

        int x = contentArea.getX();

        // Draw arrows after each device slot
        for (size_t i = 0; i < deviceSlots_.size(); ++i) {
            int slotWidth = deviceSlots_[i]->getExpandedWidth();
            x += slotWidth;

            // Draw arrow
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.drawLine(static_cast<float>(x + 2), static_cast<float>(arrowY),
                       static_cast<float>(x + arrowWidth - 2), static_cast<float>(arrowY), 1.5f);
            // Arrow head
            g.drawLine(static_cast<float>(x + arrowWidth - 5), static_cast<float>(arrowY - 4),
                       static_cast<float>(x + arrowWidth - 2), static_cast<float>(arrowY), 1.5f);
            g.drawLine(static_cast<float>(x + arrowWidth - 5), static_cast<float>(arrowY + 4),
                       static_cast<float>(x + arrowWidth - 2), static_cast<float>(arrowY), 1.5f);

            x += arrowWidth + slotSpacing;
        }

        // Draw arrows after each rack (rack includes chain panel when visible)
        for (size_t i = 0; i < rackComponents_.size(); ++i) {
            int rackWidth = rackComponents_[i]->getPreferredWidth();
            x += rackWidth + slotSpacing;

            // Draw arrow after rack
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.drawLine(static_cast<float>(x + 2), static_cast<float>(arrowY),
                       static_cast<float>(x + arrowWidth - 2), static_cast<float>(arrowY), 1.5f);
            // Arrow head
            g.drawLine(static_cast<float>(x + arrowWidth - 5), static_cast<float>(arrowY - 4),
                       static_cast<float>(x + arrowWidth - 2), static_cast<float>(arrowY), 1.5f);
            g.drawLine(static_cast<float>(x + arrowWidth - 5), static_cast<float>(arrowY + 4),
                       static_cast<float>(x + arrowWidth - 2), static_cast<float>(arrowY), 1.5f);

            x += arrowWidth;
        }
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
        globalModsButton_.setBounds(headerArea.removeFromLeft(40));
        headerArea.removeFromLeft(4);
        addRackButton_.setBounds(headerArea.removeFromLeft(55));
        headerArea.removeFromLeft(4);
        addMultibandRackButton_.setBounds(headerArea.removeFromLeft(75));
        headerArea.removeFromLeft(16);

        // RIGHT SIDE - Track info (from right to left)
        chainBypassButton_.setBounds(headerArea.removeFromRight(36));
        headerArea.removeFromRight(8);
        volumeSlider_.setBounds(headerArea.removeFromRight(50));
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
        int chainHeight = contentArea.getHeight();
        int arrowWidth = 20;
        int slotSpacing = 8;

        int x = contentArea.getX();

        // Layout device slots horizontally
        for (auto& slot : deviceSlots_) {
            int slotWidth = slot->getExpandedWidth();
            slot->setBounds(x, contentArea.getY(), slotWidth, chainHeight);
            x += slotWidth + arrowWidth + slotSpacing;
        }

        // Layout rack components horizontally (continuing the chain)
        // Rack width includes chain panel if visible
        for (auto& rack : rackComponents_) {
            int rackWidth = rack->getPreferredWidth();
            rack->setBounds(x, contentArea.getY(), rackWidth, chainHeight);
            x += rackWidth + arrowWidth + slotSpacing;
        }
    }
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
        rebuildDeviceSlots();
        rebuildRackComponents();
    }
}

void TrackChainContent::updateFromSelectedTrack() {
    if (selectedTrackId_ == magda::INVALID_TRACK_ID) {
        showHeader(false);
        noSelectionLabel_.setVisible(true);
        deviceSlots_.clear();
        rackComponents_.clear();
    } else {
        const auto* track = magda::TrackManager::getInstance().getTrack(selectedTrackId_);
        if (track) {
            trackNameLabel_.setText(track->name, juce::dontSendNotification);

            // Convert linear gain to dB for volume slider
            float db = gainToDb(track->volume);
            volumeSlider_.setValue(db, juce::dontSendNotification);

            // Reset chain bypass button state
            chainBypassButton_.setToggleState(false, juce::dontSendNotification);
            chainBypassButton_.setButtonText("ON");

            showHeader(true);
            noSelectionLabel_.setVisible(false);
            rebuildDeviceSlots();
            rebuildRackComponents();
        } else {
            showHeader(false);
            noSelectionLabel_.setVisible(true);
            deviceSlots_.clear();
            rackComponents_.clear();
        }
    }

    resized();
    repaint();
}

void TrackChainContent::showHeader(bool show) {
    // Left side - action buttons
    globalModsButton_.setVisible(show);
    addRackButton_.setVisible(show);
    addMultibandRackButton_.setVisible(show);
    // Right side - track info
    trackNameLabel_.setVisible(show);
    volumeSlider_.setVisible(show);
    chainBypassButton_.setVisible(show);
}

void TrackChainContent::rebuildDeviceSlots() {
    // Remove existing slots
    deviceSlots_.clear();

    if (selectedTrackId_ == magda::INVALID_TRACK_ID) {
        return;
    }

    const auto* devices = magda::TrackManager::getInstance().getDevices(selectedTrackId_);
    if (!devices) {
        return;
    }

    // Create a slot component for each device
    for (const auto& device : *devices) {
        auto slot = std::make_unique<DeviceSlotComponent>(*this, selectedTrackId_, device);
        addAndMakeVisible(*slot);
        deviceSlots_.push_back(std::move(slot));
    }

    resized();
    repaint();
}

void TrackChainContent::rebuildRackComponents() {
    if (selectedTrackId_ == magda::INVALID_TRACK_ID) {
        unfocusAllComponents();
        rackComponents_.clear();
        return;
    }

    const auto* racks = magda::TrackManager::getInstance().getRacks(selectedTrackId_);
    if (!racks) {
        unfocusAllComponents();
        rackComponents_.clear();
        return;
    }

    // Smart rebuild: preserve existing rack components, only add/remove as needed
    std::vector<std::unique_ptr<RackComponent>> newRackComponents;

    for (const auto& rack : *racks) {
        // Check if we already have a component for this rack
        std::unique_ptr<RackComponent> existingRack;
        for (auto it = rackComponents_.begin(); it != rackComponents_.end(); ++it) {
            if ((*it)->getRackId() == rack.id) {
                // Found existing rack - preserve it and update its data
                existingRack = std::move(*it);
                rackComponents_.erase(it);
                existingRack->updateFromRack(rack);
                break;
            }
        }

        if (existingRack) {
            newRackComponents.push_back(std::move(existingRack));
        } else {
            // Create new component for new rack
            auto rackComp = std::make_unique<RackComponent>(selectedTrackId_, rack);
            // Wire up chain selection callback
            rackComp->onChainSelected = [this](magda::TrackId trackId, magda::RackId rackId,
                                               magda::ChainId chainId) {
                onChainSelected(trackId, rackId, chainId);
            };
            addAndMakeVisible(*rackComp);
            newRackComponents.push_back(std::move(rackComp));
        }
    }

    // Unfocus before destroying remaining old racks (racks that were removed)
    if (!rackComponents_.empty()) {
        unfocusAllComponents();
    }

    // Move new components to member variable (old ones are destroyed here)
    rackComponents_ = std::move(newRackComponents);

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
    for (auto& rack : rackComponents_) {
        if (rack->getRackId() != rackId) {
            rack->clearChainSelection();
            rack->hideChainPanel();
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

}  // namespace magda::daw::ui
