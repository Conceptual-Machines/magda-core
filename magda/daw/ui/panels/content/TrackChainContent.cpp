#include "TrackChainContent.hpp"

#include <cmath>

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/MixerMetrics.hpp"
#include "core/DeviceInfo.hpp"
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
// DeviceSlotComponent - Interactive device display
//==============================================================================
class TrackChainContent::DeviceSlotComponent : public juce::Component {
  public:
    static constexpr int GAIN_SLIDER_WIDTH = 28;
    static constexpr int MODULATOR_PANEL_WIDTH = 60;
    static constexpr int PARAM_PANEL_WIDTH = 80;

    static DeviceButtonLookAndFeel& getDeviceButtonLookAndFeel() {
        static DeviceButtonLookAndFeel laf;
        return laf;
    }

    DeviceSlotComponent(TrackChainContent& owner, magda::TrackId trackId,
                        const magda::DeviceInfo& device)
        : owner_(owner),
          trackId_(trackId),
          device_(device),
          gainSliderVisible_(device.gainPanelOpen),
          modPanelVisible_(device.modPanelOpen),
          paramPanelVisible_(device.paramPanelOpen),
          collapsed_(!device.expanded) {
        // Bypass button
        bypassButton_.setButtonText("B");
        bypassButton_.setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
        bypassButton_.setColour(juce::TextButton::buttonOnColourId,
                                DarkTheme::getColour(DarkTheme::STATUS_WARNING));
        bypassButton_.setColour(juce::TextButton::textColourOffId,
                                DarkTheme::getSecondaryTextColour());
        bypassButton_.setColour(juce::TextButton::textColourOnId,
                                DarkTheme::getColour(DarkTheme::BACKGROUND));
        bypassButton_.setClickingTogglesState(true);
        bypassButton_.setToggleState(device_.bypassed, juce::dontSendNotification);
        bypassButton_.onClick = [this]() {
            magda::TrackManager::getInstance().setDeviceBypassed(trackId_, device_.id,
                                                                 bypassButton_.getToggleState());
        };
        addAndMakeVisible(bypassButton_);

        // Modulator toggle button
        modToggleButton_.setButtonText("M");
        modToggleButton_.setColour(juce::TextButton::buttonColourId,
                                   DarkTheme::getColour(DarkTheme::SURFACE));
        modToggleButton_.setColour(juce::TextButton::buttonOnColourId,
                                   DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        modToggleButton_.setColour(juce::TextButton::textColourOffId,
                                   DarkTheme::getSecondaryTextColour());
        modToggleButton_.setColour(juce::TextButton::textColourOnId,
                                   DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        modToggleButton_.setClickingTogglesState(true);
        modToggleButton_.setToggleState(modPanelVisible_, juce::dontSendNotification);
        modToggleButton_.onClick = [this]() {
            modPanelVisible_ = modToggleButton_.getToggleState();
            // Save state to TrackManager
            if (auto* dev = magda::TrackManager::getInstance().getDevice(trackId_, device_.id)) {
                dev->modPanelOpen = modPanelVisible_;
            }
            resized();
            repaint();
            // Notify parent to re-layout all slots
            owner_.resized();
            owner_.repaint();
        };
        addAndMakeVisible(modToggleButton_);

        // Gain toggle button
        gainToggleButton_.setButtonText("G");
        gainToggleButton_.setColour(juce::TextButton::buttonColourId,
                                    DarkTheme::getColour(DarkTheme::SURFACE));
        gainToggleButton_.setColour(juce::TextButton::buttonOnColourId,
                                    DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        gainToggleButton_.setColour(juce::TextButton::textColourOffId,
                                    DarkTheme::getSecondaryTextColour());
        gainToggleButton_.setColour(juce::TextButton::textColourOnId,
                                    DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        gainToggleButton_.setClickingTogglesState(true);
        gainToggleButton_.setToggleState(gainSliderVisible_, juce::dontSendNotification);
        gainToggleButton_.onClick = [this]() {
            gainSliderVisible_ = gainToggleButton_.getToggleState();
            gainMeter_.setVisible(gainSliderVisible_);
            // Save state to TrackManager
            if (auto* dev = magda::TrackManager::getInstance().getDevice(trackId_, device_.id)) {
                dev->gainPanelOpen = gainSliderVisible_;
            }
            resized();
            repaint();
            // Notify parent to re-layout all slots
            owner_.resized();
            owner_.repaint();
        };
        addAndMakeVisible(gainToggleButton_);

        // Gain meter with text slider - restore dB value from device
        gainMeter_.setGainDb(device_.gainDb, juce::dontSendNotification);
        gainMeter_.setVisible(gainSliderVisible_);
        gainMeter_.onGainChanged = [this](double db) {
            // Save gain dB value to TrackManager
            if (auto* dev = magda::TrackManager::getInstance().getDevice(trackId_, device_.id)) {
                dev->gainDb = static_cast<float>(db);
            }
        };
        if (gainSliderVisible_) {
            addAndMakeVisible(gainMeter_);
        } else {
            addChildComponent(gainMeter_);
        }

        // Parameter toggle button
        paramToggleButton_.setButtonText("P");
        paramToggleButton_.setColour(juce::TextButton::buttonColourId,
                                     DarkTheme::getColour(DarkTheme::SURFACE));
        paramToggleButton_.setColour(juce::TextButton::buttonOnColourId,
                                     DarkTheme::getColour(DarkTheme::ACCENT_PURPLE));
        paramToggleButton_.setColour(juce::TextButton::textColourOffId,
                                     DarkTheme::getSecondaryTextColour());
        paramToggleButton_.setColour(juce::TextButton::textColourOnId,
                                     DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        paramToggleButton_.setClickingTogglesState(true);
        paramToggleButton_.setToggleState(paramPanelVisible_, juce::dontSendNotification);
        paramToggleButton_.onClick = [this]() {
            paramPanelVisible_ = paramToggleButton_.getToggleState();
            // Save state to TrackManager
            if (auto* dev = magda::TrackManager::getInstance().getDevice(trackId_, device_.id)) {
                dev->paramPanelOpen = paramPanelVisible_;
            }
            resized();
            repaint();
            // Notify parent to re-layout all slots
            owner_.resized();
            owner_.repaint();
        };
        addAndMakeVisible(paramToggleButton_);

        // Mock parameter knobs (will be replaced with real params later)
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

        // Modulator slot buttons (mock - 3 slots)
        for (int i = 0; i < 3; ++i) {
            auto& btn = modSlotButtons_[i];
            btn = std::make_unique<juce::TextButton>("+");
            btn->setColour(juce::TextButton::buttonColourId,
                           DarkTheme::getColour(DarkTheme::SURFACE));
            btn->setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
            btn->onClick = [this, i]() {
                // Show modulator type menu
                juce::PopupMenu menu;
                menu.addItem(1, "LFO");
                menu.addItem(2, "Bezier LFO");
                menu.addItem(3, "ADSR");
                menu.addItem(4, "Envelope Follower");
                menu.showMenuAsync(juce::PopupMenu::Options(), [this, i](int result) {
                    if (result > 0) {
                        juce::StringArray types = {"", "LFO", "BEZ", "ADSR", "ENV"};
                        modSlotButtons_[i]->setButtonText(types[result]);
                        DBG("Added modulator type " << result << " to slot " << i);
                    }
                });
            };
            addChildComponent(*btn);
        }

        // UI button (opens plugin editor window)
        uiButton_.setButtonText("U");
        uiButton_.setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::SURFACE));
        uiButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
        uiButton_.onClick = [this]() {
            // TODO: Open plugin UI window
            DBG("Open plugin UI for: " << device_.name);
        };
        addAndMakeVisible(uiButton_);

        // Delete button
        deleteButton_.setButtonText(juce::String::fromUTF8("✕"));
        deleteButton_.setColour(juce::TextButton::buttonColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
        deleteButton_.setColour(juce::TextButton::textColourOffId,
                                DarkTheme::getSecondaryTextColour());
        deleteButton_.onClick = [this]() {
            magda::TrackManager::getInstance().removeDeviceFromTrack(trackId_, device_.id);
        };
        addAndMakeVisible(deleteButton_);

        // Apply square button look and feel to all buttons
        bypassButton_.setLookAndFeel(&getDeviceButtonLookAndFeel());
        modToggleButton_.setLookAndFeel(&getDeviceButtonLookAndFeel());
        paramToggleButton_.setLookAndFeel(&getDeviceButtonLookAndFeel());
        gainToggleButton_.setLookAndFeel(&getDeviceButtonLookAndFeel());
        uiButton_.setLookAndFeel(&getDeviceButtonLookAndFeel());
        deleteButton_.setLookAndFeel(&getDeviceButtonLookAndFeel());
    }

    ~DeviceSlotComponent() override {
        // Clear LookAndFeel references
        bypassButton_.setLookAndFeel(nullptr);
        modToggleButton_.setLookAndFeel(nullptr);
        paramToggleButton_.setLookAndFeel(nullptr);
        gainToggleButton_.setLookAndFeel(nullptr);
        uiButton_.setLookAndFeel(nullptr);
        deleteButton_.setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();

        // Mod panel on left (visible even when collapsed)
        if (modPanelVisible_) {
            auto modArea = bounds.removeFromLeft(MODULATOR_PANEL_WIDTH);
            g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
            g.fillRoundedRectangle(modArea.toFloat(), 4.0f);
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.drawRoundedRectangle(modArea.toFloat(), 4.0f, 1.0f);

            // Draw "Mod" label at top
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(FontManager::getInstance().getUIFont(8.0f));
            g.drawText("Mod", modArea.removeFromTop(14), juce::Justification::centred);
        }

        // Param panel (between mod and main, visible even when collapsed)
        if (paramPanelVisible_) {
            auto paramArea = bounds.removeFromLeft(PARAM_PANEL_WIDTH);
            g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
            g.fillRoundedRectangle(paramArea.toFloat(), 4.0f);
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.drawRoundedRectangle(paramArea.toFloat(), 4.0f, 1.0f);

            // Draw "Params" label at top
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(FontManager::getInstance().getUIFont(8.0f));
            g.drawText("Params", paramArea.removeFromTop(14), juce::Justification::centred);
        }

        // Gain panel on right (visible even when collapsed)
        if (gainSliderVisible_) {
            bounds.removeFromRight(GAIN_SLIDER_WIDTH);
        }

        // Background for main area
        auto bgColour = device_.bypassed ? DarkTheme::getColour(DarkTheme::SURFACE).withAlpha(0.5f)
                                         : DarkTheme::getColour(DarkTheme::SURFACE);
        g.setColour(bgColour);
        g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

        // Border
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(bounds.toFloat(), 4.0f, 1.0f);

        if (!collapsed_) {
            // Device name
            auto textBounds = bounds.reduced(6);
            textBounds.removeFromTop(20);     // Skip header row
            textBounds.removeFromBottom(20);  // Skip footer row

            auto textColour = device_.bypassed ? DarkTheme::getSecondaryTextColour().withAlpha(0.5f)
                                               : DarkTheme::getTextColour();
            g.setColour(textColour);
            g.setFont(FontManager::getInstance().getUIFontBold(11.0f));
            g.drawText(device_.name, textBounds, juce::Justification::centred);

            // Manufacturer + format
            auto mfrBounds = textBounds;
            mfrBounds.removeFromTop(16);
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(FontManager::getInstance().getUIFont(9.0f));
            g.drawText(device_.manufacturer + " - " + device_.getFormatString(), mfrBounds,
                       juce::Justification::centred);
        }
    }

    void mouseDoubleClick(const juce::MouseEvent&) override {
        collapsed_ = !collapsed_;
        // Save state to TrackManager
        if (auto* dev = magda::TrackManager::getInstance().getDevice(trackId_, device_.id)) {
            dev->expanded = !collapsed_;
        }
        resized();
        repaint();
        // Notify parent to re-layout all slots
        owner_.resized();
        owner_.repaint();
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(4);

        // Modulator panel on the left if visible (always, even when collapsed)
        if (modPanelVisible_) {
            auto modArea = bounds.removeFromLeft(MODULATOR_PANEL_WIDTH - 4);
            modArea.removeFromTop(14);  // Skip "Mod" label
            modArea = modArea.reduced(2);

            int slotHeight = (modArea.getHeight() - 4) / 3;
            for (int i = 0; i < 3; ++i) {
                modSlotButtons_[i]->setBounds(modArea.removeFromTop(slotHeight).reduced(0, 1));
                modSlotButtons_[i]->setVisible(true);
            }
        } else {
            for (auto& btn : modSlotButtons_) {
                btn->setVisible(false);
            }
        }

        // Parameter panel (always, even when collapsed)
        if (paramPanelVisible_) {
            auto paramArea = bounds.removeFromLeft(PARAM_PANEL_WIDTH - 4);
            paramArea.removeFromTop(14);  // Skip "Params" label
            paramArea = paramArea.reduced(2);

            // Layout knobs in a 2x2 grid
            int knobSize = (paramArea.getWidth() - 2) / 2;
            int row = 0, col = 0;
            for (auto& knob : paramKnobs_) {
                int x = paramArea.getX() + col * (knobSize + 2);
                int y = paramArea.getY() + row * (knobSize + 2);
                knob->setBounds(x, y, knobSize, knobSize);
                knob->setVisible(true);
                col++;
                if (col >= 2) {
                    col = 0;
                    row++;
                }
            }
        } else {
            for (auto& knob : paramKnobs_) {
                knob->setVisible(false);
            }
        }

        // Gain meter on the right if visible (always, even when collapsed)
        if (gainSliderVisible_) {
            auto meterArea = bounds.removeFromRight(GAIN_SLIDER_WIDTH - 4);
            gainMeter_.setBounds(meterArea.reduced(2, 2));
            gainMeter_.setVisible(true);
        } else {
            gainMeter_.setVisible(false);
        }

        // Layout buttons for main plugin area
        if (collapsed_) {
            // Collapsed mode: vertical column of buttons
            // Top group: ON, U, X (device controls)
            // Bottom group: M, P, G (panel toggles)
            int buttonSize = 16;
            int spacing = 2;
            int x = bounds.getX() + (bounds.getWidth() - buttonSize) / 2;  // Center horizontally
            int y = bounds.getY();

            // Device controls at top
            bypassButton_.setBounds(x, y, buttonSize, buttonSize);
            y += buttonSize + spacing;
            uiButton_.setBounds(x, y, buttonSize, buttonSize);
            y += buttonSize + spacing;
            deleteButton_.setBounds(x, y, buttonSize, buttonSize);
            y += buttonSize + spacing + 4;  // Extra gap between groups

            // Panel toggles at bottom
            modToggleButton_.setBounds(x, y, buttonSize, buttonSize);
            y += buttonSize + spacing;
            paramToggleButton_.setBounds(x, y, buttonSize, buttonSize);
            y += buttonSize + spacing;
            gainToggleButton_.setBounds(x, y, buttonSize, buttonSize);
        } else {
            // Expanded mode: header and footer layout
            int btnSize = 16;
            int btnSpacing = 2;

            // Calculate inset dynamically based on visible panel widths
            int leftPanelWidth = 0;
            if (modPanelVisible_)
                leftPanelWidth += MODULATOR_PANEL_WIDTH;
            if (paramPanelVisible_)
                leftPanelWidth += PARAM_PANEL_WIDTH;
            int rightPanelWidth = gainSliderVisible_ ? GAIN_SLIDER_WIDTH : 0;

            int leftInset = leftPanelWidth > 0 ? leftPanelWidth / 15 : 0;
            int rightInset = rightPanelWidth > 0 ? rightPanelWidth / 7 : 0;

            // Header: [ON] [U] ... [X]
            auto headerRow = bounds.removeFromTop(18);
            headerRow.removeFromLeft(leftInset);
            headerRow.removeFromRight(rightInset);
            bypassButton_.setBounds(headerRow.removeFromLeft(btnSize));
            headerRow.removeFromLeft(btnSpacing);
            uiButton_.setBounds(headerRow.removeFromLeft(btnSize));
            deleteButton_.setBounds(headerRow.removeFromRight(btnSize));

            // Footer: [M] [P] ... [G]
            auto footerRow = bounds.removeFromBottom(18);
            footerRow.removeFromLeft(leftInset);
            footerRow.removeFromRight(rightInset);
            modToggleButton_.setBounds(footerRow.removeFromLeft(btnSize));
            footerRow.removeFromLeft(btnSpacing);
            paramToggleButton_.setBounds(footerRow.removeFromLeft(btnSize));
            gainToggleButton_.setBounds(footerRow.removeFromRight(btnSize));
        }
    }

    bool isGainSliderVisible() const {
        return gainSliderVisible_;
    }
    bool isModPanelVisible() const {
        return modPanelVisible_;
    }

    int getExpandedWidth() const {
        int width = collapsed_ ? 36 : 130;  // Collapsed = vertical buttons only, expanded = full
        if (modPanelVisible_)
            width += MODULATOR_PANEL_WIDTH;
        if (paramPanelVisible_)
            width += PARAM_PANEL_WIDTH;
        if (gainSliderVisible_)
            width += GAIN_SLIDER_WIDTH;
        return width;
    }

    bool isCollapsed() const {
        return collapsed_;
    }

  private:
    TrackChainContent& owner_;
    magda::TrackId trackId_;
    magda::DeviceInfo device_;
    juce::TextButton bypassButton_;
    juce::TextButton modToggleButton_;
    juce::TextButton paramToggleButton_;
    juce::TextButton gainToggleButton_;
    juce::TextButton uiButton_;
    juce::TextButton deleteButton_;
    GainMeterComponent gainMeter_;
    std::unique_ptr<juce::TextButton> modSlotButtons_[3];
    std::vector<std::unique_ptr<juce::Slider>> paramKnobs_;
    bool gainSliderVisible_ = false;
    bool modPanelVisible_ = false;
    bool paramPanelVisible_ = false;
    bool collapsed_ = false;
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
