#include "TrackChainContent.hpp"

#include <cmath>

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/MixerMetrics.hpp"
#include "../../themes/SmallButtonLookAndFeel.hpp"
#include "core/DeviceInfo.hpp"
#include "ui/components/chain/NodeComponent.hpp"
#include "ui/components/chain/RackComponent.hpp"

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
// DeviceButtonLookAndFeel - Small rounded buttons for device slots
//==============================================================================
class DeviceButtonLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& bgColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        float cornerRadius = 3.0f;

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
        auto font = FontManager::getInstance().getUIFont(9.0f);
        g.setFont(font);
        g.setColour(button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                              : juce::TextButton::textColourOffId));
        g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
    }
};

//==============================================================================
// DeviceSlotComponent - Interactive device display (inherits from NodeComponent)
//==============================================================================
class TrackChainContent::DeviceSlotComponent : public NodeComponent {
  public:
    static constexpr int BASE_WIDTH = 130;

    DeviceSlotComponent(TrackChainContent& owner, magda::TrackId trackId,
                        const magda::DeviceInfo& device)
        : owner_(owner), trackId_(trackId), device_(device) {
        setNodeName(device.name);
        setBypassed(device.bypassed);

        // Restore panel visibility from device state
        modPanelVisible_ = device.modPanelOpen;
        paramPanelVisible_ = device.paramPanelOpen;
        gainPanelVisible_ = device.gainPanelOpen;

        // Set up NodeComponent callbacks
        onBypassChanged = [this](bool bypassed) {
            magda::TrackManager::getInstance().setDeviceBypassed(trackId_, device_.id, bypassed);
        };

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

        onParamPanelToggled = [this](bool visible) {
            if (auto* dev = magda::TrackManager::getInstance().getDevice(trackId_, device_.id)) {
                dev->paramPanelOpen = visible;
            }
            owner_.resized();
            owner_.repaint();
        };

        onGainPanelToggled = [this](bool visible) {
            if (auto* dev = magda::TrackManager::getInstance().getDevice(trackId_, device_.id)) {
                dev->gainPanelOpen = visible;
            }
            owner_.resized();
            owner_.repaint();
        };

        onLayoutChanged = [this]() {
            owner_.resized();
            owner_.repaint();
        };

        // UI button (extra header button)
        uiButton_.setButtonText("U");
        uiButton_.setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));
        uiButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
        uiButton_.onClick = [this]() { DBG("Open plugin UI for: " << device_.name); };
        uiButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
        addAndMakeVisible(uiButton_);

        // Gain meter with text slider
        gainMeter_.setGainDb(device_.gainDb, juce::dontSendNotification);
        gainMeter_.onGainChanged = [this](double db) {
            if (auto* dev = magda::TrackManager::getInstance().getDevice(trackId_, device_.id)) {
                dev->gainDb = static_cast<float>(db);
            }
        };
        addAndMakeVisible(gainMeter_);
    }

    ~DeviceSlotComponent() override {
        uiButton_.setLookAndFeel(nullptr);
    }

    int getExpandedWidth() const {
        return getTotalWidth(BASE_WIDTH);
    }

    bool isGainSliderVisible() const {
        return gainPanelVisible_;
    }
    bool isModPanelVisible() const {
        return modPanelVisible_;
    }
    bool isCollapsed() const {
        return false;
    }  // No longer support collapsed mode

  protected:
    void paintContent(juce::Graphics& g, juce::Rectangle<int> contentArea) override {
        // Device name and manufacturer
        auto textColour = isBypassed() ? DarkTheme::getSecondaryTextColour().withAlpha(0.5f)
                                       : DarkTheme::getTextColour();
        g.setColour(textColour);
        g.setFont(FontManager::getInstance().getUIFontBold(11.0f));
        g.drawText(device_.name, contentArea, juce::Justification::centred);

        auto mfrBounds = contentArea;
        mfrBounds.removeFromTop(16);
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(FontManager::getInstance().getUIFont(9.0f));
        g.drawText(device_.manufacturer + " - " + device_.getFormatString(), mfrBounds,
                   juce::Justification::centred);
    }

    void resizedContent(juce::Rectangle<int> /*contentArea*/) override {
        // Content area doesn't need child layout - just painted text
    }

    void resizedHeaderExtra(juce::Rectangle<int>& headerArea) override {
        // Add UI button after bypass button
        uiButton_.setBounds(headerArea.removeFromLeft(BUTTON_SIZE));
        headerArea.removeFromLeft(4);
    }

    void resizedGainPanel(juce::Rectangle<int> panelArea) override {
        gainMeter_.setBounds(panelArea.reduced(2, 4));
        gainMeter_.setVisible(true);
    }

    int getModPanelWidth() const override {
        return 60;
    }
    int getParamPanelWidth() const override {
        return 80;
    }
    int getGainPanelWidth() const override {
        return 28;
    }

  private:
    TrackChainContent& owner_;
    magda::TrackId trackId_;
    magda::DeviceInfo device_;
    juce::TextButton uiButton_;
    GainMeterComponent gainMeter_;
};

// dB conversion helpers
namespace {
constexpr float MIN_DB = -60.0f;
constexpr float MAX_DB = 6.0f;
constexpr float UNITY_DB = 0.0f;

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

float dbToFaderPos(float db) {
    if (db <= MIN_DB)
        return 0.0f;
    if (db >= MAX_DB)
        return 1.0f;

    if (db < UNITY_DB) {
        return 0.75f * (db - MIN_DB) / (UNITY_DB - MIN_DB);
    } else {
        return 0.75f + 0.25f * (db - UNITY_DB) / (MAX_DB - UNITY_DB);
    }
}

float faderPosToDb(float pos) {
    if (pos <= 0.0f)
        return MIN_DB;
    if (pos >= 1.0f)
        return MAX_DB;

    if (pos < 0.75f) {
        return MIN_DB + (pos / 0.75f) * (UNITY_DB - MIN_DB);
    } else {
        return UNITY_DB + ((pos - 0.75f) / 0.25f) * (MAX_DB - UNITY_DB);
    }
}
}  // namespace

TrackChainContent::TrackChainContent() {
    setName("Track Chain");

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

    // Volume slider - horizontal, using dB scale with unity at 0.75 position
    volumeSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider_.setRange(0.0, 1.0, 0.001);
    volumeSlider_.setValue(0.75);  // Unity gain (0 dB)
    volumeSlider_.setSliderSnapsToMousePosition(false);
    volumeSlider_.setColour(juce::Slider::trackColourId,
                            DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    volumeSlider_.setColour(juce::Slider::backgroundColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));
    volumeSlider_.setColour(juce::Slider::thumbColourId,
                            DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    volumeSlider_.onValueChange = [this]() {
        if (selectedTrackId_ != magda::INVALID_TRACK_ID) {
            float faderPos = static_cast<float>(volumeSlider_.getValue());
            float db = faderPosToDb(faderPos);
            float gain = dbToGain(db);
            magda::TrackManager::getInstance().setTrackVolume(selectedTrackId_, gain);
            // Update volume label
            juce::String dbText;
            if (db <= MIN_DB) {
                dbText = "-inf";
            } else {
                dbText = juce::String(db, 1) + " dB";
            }
            volumeValueLabel_.setText(dbText, juce::dontSendNotification);
        }
    };
    addChildComponent(volumeSlider_);

    // Volume value label
    volumeValueLabel_.setText("0.0 dB", juce::dontSendNotification);
    volumeValueLabel_.setJustificationType(juce::Justification::centred);
    volumeValueLabel_.setColour(juce::Label::textColourId,
                                DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    volumeValueLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    addChildComponent(volumeValueLabel_);

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
        volumeSlider_.setBounds(headerArea.removeFromRight(80));
        headerArea.removeFromRight(4);
        volumeValueLabel_.setBounds(headerArea.removeFromRight(50));
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

            // Convert linear gain to fader position for volume slider
            float db = gainToDb(track->volume);
            float faderPos = dbToFaderPos(db);
            volumeSlider_.setValue(faderPos, juce::dontSendNotification);

            // Update volume label
            juce::String dbText;
            if (db <= MIN_DB) {
                dbText = "-inf";
            } else {
                dbText = juce::String(db, 1) + " dB";
            }
            volumeValueLabel_.setText(dbText, juce::dontSendNotification);

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
    volumeValueLabel_.setVisible(show);
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
    (void)trackId;
    (void)chainId;

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

}  // namespace magda::daw::ui
