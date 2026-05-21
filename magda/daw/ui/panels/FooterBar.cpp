#include "FooterBar.hpp"

#include <BinaryData.h>

#include "../../../agents/llama_model_manager.hpp"
#include "../../scripting_app.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "audio/midi/MidiDeviceMatch.hpp"
#include "core/Config.hpp"
#include "core/StringTable.hpp"
#include "media_db/MediaDbContext.hpp"
#include "media_db/SampleTaggerDownloader.hpp"

namespace magda {

namespace {
constexpr int kBadgePadX = 8;
constexpr int kBadgeGap = 6;
constexpr int kBadgeHeight = 20;
constexpr int kBadgeDotSize = 6;
constexpr int kBadgeDotPad = 4;
constexpr int kStripLeftMargin = 12;
constexpr int kLocalModelButtonSize = 20;
constexpr int kLocalModelGap = 5;
constexpr int kLocalModelRightGap = 10;
}  // namespace

FooterBar::FooterBar() {
    setupButtons();
    setupLocalModelButtons();
    setupBottomCollapseButton();
    ViewModeController::getInstance().addListener(this);
    BindingRegistry::getInstance().addListener(this);
    ControllerRegistry::getInstance().addListener(this);
    refreshLiveInputs();
    refreshControllerBadges();
    refreshLocalModelStatus();
    startTimerHz(2);  // poll for MIDI device hot-plug
    updateButtonStates();
}

FooterBar::~FooterBar() {
    stopTimer();
    ControllerRegistry::getInstance().removeListener(this);
    BindingRegistry::getInstance().removeListener(this);
    ViewModeController::getInstance().removeListener(this);
    // RAII cleanup handled automatically by ManagedChild
}

bool FooterBar::refreshLiveInputs() {
    auto fresh = juce::MidiInput::getAvailableDevices();
    bool changed = fresh.size() != liveInputs_.size();
    if (!changed) {
        for (int i = 0; i < fresh.size(); ++i) {
            if (fresh[i].identifier != liveInputs_[i].identifier ||
                fresh[i].name != liveInputs_[i].name) {
                changed = true;
                break;
            }
        }
    }
    liveInputs_ = std::move(fresh);
    return changed;
}

void FooterBar::refreshControllerBadges() {
    controllerBadges_.clear();

    // A Lua script and the controller-profile system are mutually exclusive
    // input layers — only one is actually driving the engine at a time. When
    // a script is loaded it owns the strip; the profile pills come back when
    // the script is unloaded.
    auto luaName = scripting_app::activeLuaScriptName();
    lastActiveLuaScriptName_ = luaName;
    if (luaName.isNotEmpty()) {
        ControllerBadge b;
        b.label = luaName;
        b.connected = true;
        auto ports = scripting_app::luaScriptPorts(luaName);
        if (ports.dawInputPort.isNotEmpty()) {
            b.connected = false;
            for (const auto& dev : liveInputs_) {
                if (magda::midi::matches(ports.dawInputPort, dev.identifier, dev.name)) {
                    b.connected = true;
                    break;
                }
            }
        }
        controllerBadges_.push_back(std::move(b));
    } else {
        for (const auto& c : ControllerRegistry::getInstance().all()) {
            if (!BindingRegistry::getInstance().hasAnyBindingForController(c.id))
                continue;

            ControllerBadge b;
            b.label = c.name;
            b.connected = false;
            // Use the shared port matcher rather than strict equality: stored
            // inputPort can be either a JUCE identifier or a display name,
            // with varying case, depending on platform — magda::midi::matches
            // handles all of those consistently.
            for (const auto& dev : liveInputs_) {
                if (magda::midi::matches(c.inputPort, dev.identifier, dev.name)) {
                    b.connected = true;
                    break;
                }
            }
            controllerBadges_.push_back(std::move(b));
        }
    }

    resized();  // recompute badge hit areas
    repaint();
}

void FooterBar::bindingRegistryChanged(BindingScope /*scope*/) {
    refreshControllerBadges();
}

void FooterBar::controllerRegistryChanged() {
    refreshControllerBadges();
}

void FooterBar::timerCallback() {
    bool needsRefresh = refreshLiveInputs();
    // No listener API on LuaController — poll for a change in the active
    // script so a load / unload from the Controllers dialog reflects in the
    // footer without waiting for a binding-registry change.
    if (scripting_app::activeLuaScriptName() != lastActiveLuaScriptName_)
        needsRefresh = true;
    if (needsRefresh)
        refreshControllerBadges();

    if (refreshLocalModelStatus())
        repaint();
}

void FooterBar::mouseUp(const juce::MouseEvent& e) {
    if (controllerStripArea_.contains(e.getPosition()) && onControllersClicked)
        onControllersClicked();
    if (localModelStripArea_.contains(e.getPosition()) && onLocalModelsClicked)
        onLocalModelsClicked();
}

void FooterBar::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));

    // Draw top border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawLine(0.0f, 0.0f, static_cast<float>(getWidth()), 0.0f, 1.0f);

    // Enabled-controller badges on the left.
    auto font = FontManager::getInstance().getUIFont(11.0f);
    g.setFont(font);
    for (const auto& b : controllerBadges_) {
        // Pill background.
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRoundedRectangle(b.hitArea.toFloat(), 4.0f);

        // Connection dot — green when the live MIDI port is available, dim
        // grey when the controller is enabled but the port is unplugged.
        auto dotArea = juce::Rectangle<float>(
            static_cast<float>(b.hitArea.getX() + kBadgeDotPad),
            static_cast<float>(b.hitArea.getCentreY() - kBadgeDotSize / 2.0f),
            static_cast<float>(kBadgeDotSize), static_cast<float>(kBadgeDotSize));
        g.setColour(b.connected ? DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(0.95f)
                                : juce::Colour(DarkTheme::TEXT_DIM).withAlpha(0.55f));
        g.fillEllipse(dotArea);

        // Label.
        g.setColour(DarkTheme::getTextColour());
        auto textArea = b.hitArea;
        textArea.removeFromLeft(kBadgeDotPad + kBadgeDotSize + 4);
        textArea.removeFromRight(kBadgePadX / 2);
        g.drawText(b.label, textArea, juce::Justification::centredLeft, true);
    }

    // Local model buttons use icons for identity and a small dot for state.
    for (size_t i = 0; i < localModelButtons_.size(); ++i) {
        const auto* button = localModelButtons_[i].get();
        if (button == nullptr || !button->isVisible())
            continue;

        juce::Colour dotColour;
        switch (localModelStates_[i]) {
            case LocalModelState::Loaded:
                dotColour = DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(0.95f);
                break;
            case LocalModelState::Loading:
                dotColour = DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.95f);
                break;
            case LocalModelState::Idle:
                dotColour = juce::Colour(0xFFE5B84B);
                break;
            case LocalModelState::Unavailable:
                dotColour = juce::Colour(DarkTheme::TEXT_DIM).withAlpha(0.55f);
                break;
        }

        auto dotArea = button->getBounds().toFloat().removeFromBottom(8.0f).removeFromRight(8.0f);
        g.setColour(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
        g.fillEllipse(dotArea.expanded(1.5f));
        g.setColour(dotColour);
        g.fillEllipse(dotArea.reduced(1.5f));
    }
}

void FooterBar::resized() {
    auto bounds = getLocalBounds();

    // Center the view mode buttons horizontally
    int totalButtonsWidth = NUM_MODES * BUTTON_SIZE + (NUM_MODES - 1) * BUTTON_SPACING;
    int startX = (bounds.getWidth() - totalButtonsWidth) / 2;

    int buttonY = (bounds.getHeight() - BUTTON_SIZE) / 2;

    for (int i = 0; i < NUM_MODES; ++i) {
        int buttonX = startX + i * (BUTTON_SIZE + BUTTON_SPACING);
        modeButtons[static_cast<size_t>(i)]->setBounds(buttonX, buttonY, BUTTON_SIZE, BUTTON_SIZE);
    }

    // Bottom panel collapse button on the right side
    if (bottomCollapseButton_) {
        constexpr int collapseSize = 20;
        int cy = (bounds.getHeight() - collapseSize) / 2;
        bottomCollapseButton_->setBounds(bounds.getWidth() - collapseSize - 8, cy, collapseSize,
                                         collapseSize);
    }

    const int modelStripWidth =
        NUM_LOCAL_MODELS * kLocalModelButtonSize + (NUM_LOCAL_MODELS - 1) * kLocalModelGap;
    const int collapseLeft =
        bottomCollapseButton_ != nullptr ? bottomCollapseButton_->getX() : bounds.getRight();
    int modelX = collapseLeft - kLocalModelRightGap - modelStripWidth;
    const int modelY = (bounds.getHeight() - kLocalModelButtonSize) / 2;
    const int minModelX = startX + totalButtonsWidth + 12;
    const bool hasModelRoom = modelX >= minModelX;
    localModelStripArea_ =
        hasModelRoom ? juce::Rectangle<int>(modelX, modelY, modelStripWidth, kLocalModelButtonSize)
                     : juce::Rectangle<int>();

    for (auto& button : localModelButtons_) {
        if (button == nullptr)
            continue;
        if (hasModelRoom) {
            button->setBounds(modelX, modelY, kLocalModelButtonSize, kLocalModelButtonSize);
            button->setVisible(true);
            modelX += kLocalModelButtonSize + kLocalModelGap;
        } else {
            button->setBounds({});
            button->setVisible(false);
        }
    }

    // Lay out controller badges on the left. Each pill is a small rounded box
    // with a connection dot + label; cap text width so a long controller name
    // can't push past the centred view-mode buttons.
    auto font = FontManager::getInstance().getUIFont(11.0f);
    int x = kStripLeftMargin;
    int badgeY = (bounds.getHeight() - kBadgeHeight) / 2;
    const int maxRight = startX - 12;  // keep badges clear of the centre buttons
    for (auto& b : controllerBadges_) {
        int textW = juce::jmax(40, juce::GlyphArrangement::getStringWidthInt(font, b.label));
        int badgeW = kBadgeDotPad + kBadgeDotSize + 4 + textW + (kBadgePadX / 2);
        if (x + badgeW > maxRight) {
            // No room for this one — collapse remaining badges with an empty
            // hit area so they don't get clicked off-screen.
            b.hitArea = {};
            continue;
        }
        b.hitArea = juce::Rectangle<int>(x, badgeY, badgeW, kBadgeHeight);
        x += badgeW + kBadgeGap;
    }
    controllerStripArea_ = juce::Rectangle<int>(kStripLeftMargin, badgeY,
                                                juce::jmax(0, x - kStripLeftMargin), kBadgeHeight);
}

void FooterBar::viewModeChanged(ViewMode /*mode*/, const AudioEngineProfile& /*profile*/) {
    updateButtonStates();
}

void FooterBar::setupButtons() {
    // Icon data for each mode: Session=Live, Arrangement=Arrange, Mix
    struct IconData {
        const char* data;
        int size;
        ViewMode mode;
        const char* name;
        const char* tooltipKey;
    };

    const std::array<IconData, NUM_MODES> icons = {{
        {BinaryData::Session_svg, BinaryData::Session_svgSize, ViewMode::Live, "Session",
         "footer.tooltip.session"},
        {BinaryData::Arrangement_svg, BinaryData::Arrangement_svgSize, ViewMode::Arrange,
         "Arrangement", "footer.tooltip.arrangement"},
        {BinaryData::Mix_svg, BinaryData::Mix_svgSize, ViewMode::Mix, "Mix", "footer.tooltip.mix"},
    }};

    for (size_t i = 0; i < NUM_MODES; ++i) {
        // Create button using RAII wrapper
        modeButtons[i] = magda::ManagedChild<SvgButton>::create(icons[i].name, icons[i].data,
                                                                static_cast<size_t>(icons[i].size));

        modeButtons[i]->setTooltip(tr(icons[i].tooltipKey));
        modeButtons[i]->setClickingTogglesState(false);
        modeButtons[i]->onClick = [mode = icons[i].mode]() {
            ViewModeController::getInstance().setViewMode(mode);
        };

        // Set colors for the SvgButton
        modeButtons[i]->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        modeButtons[i]->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        modeButtons[i]->setActiveColor(DarkTheme::getColour(DarkTheme::ACCENT_CYAN));
        modeButtons[i]->setActiveBackgroundColor(
            DarkTheme::getColour(DarkTheme::ACCENT_CYAN).withAlpha(0.2f));

        addAndMakeVisible(*modeButtons[i]);  // Safe with ManagedChild
    }
}

void FooterBar::updateButtonStates() {
    auto currentMode = ViewModeController::getInstance().getViewMode();

    const std::array<ViewMode, NUM_MODES> modes = {ViewMode::Live, ViewMode::Arrange,
                                                   ViewMode::Mix};

    for (size_t i = 0; i < NUM_MODES; ++i) {
        bool isActive = (modes[i] == currentMode);
        modeButtons[i]->setActive(isActive);
    }

    repaint();
}

void FooterBar::setupLocalModelButtons() {
    struct LocalModelIcon {
        const char* name;
        const char* data;
        int size;
    };

    const std::array<LocalModelIcon, NUM_LOCAL_MODELS> icons = {{
        {"LocalLLM", BinaryData::ai_svg, BinaryData::ai_svgSize},
        {"SampleAnalyzer", BinaryData::database_svg, BinaryData::database_svgSize},
    }};

    for (size_t i = 0; i < icons.size(); ++i) {
        localModelButtons_[i] = std::make_unique<SvgButton>(icons[i].name, icons[i].data,
                                                            static_cast<size_t>(icons[i].size));
        localModelButtons_[i]->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        localModelButtons_[i]->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        localModelButtons_[i]->setPressedColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        localModelButtons_[i]->setIconPadding(3.0f);
        localModelButtons_[i]->onClick = [this]() {
            if (onLocalModelsClicked)
                onLocalModelsClicked();
        };
        addAndMakeVisible(*localModelButtons_[i]);
    }
}

bool FooterBar::refreshLocalModelStatus() {
    std::array<LocalModelState, NUM_LOCAL_MODELS> nextStates = localModelStates_;
    std::array<juce::String, NUM_LOCAL_MODELS> tooltips;

    auto& llama = LlamaModelManager::getInstance();
    const auto configuredModelPath = juce::String(Config::getInstance().getLocalModelPath());
    const bool hasConfiguredLocalModel =
        configuredModelPath.isNotEmpty() && juce::File(configuredModelPath).existsAsFile();

    if (llama.isLoaded()) {
        nextStates[0] = LocalModelState::Loaded;
        tooltips[0] = "Local LLM: loaded";
    } else if (llama.isLoading()) {
        nextStates[0] = LocalModelState::Loading;
        tooltips[0] = "Local LLM: loading";
    } else if (hasConfiguredLocalModel) {
        nextStates[0] = LocalModelState::Idle;
        tooltips[0] = "Local LLM: configured";
    } else {
        nextStates[0] = LocalModelState::Unavailable;
        tooltips[0] = "Local LLM: not configured";
    }

    auto& mediaCtx = magda::media::MediaDbContext::getInstance();
    const bool analyzerInstalled = magda::media::SampleTaggerDownloader::isInstalled();
    const bool analyzerLoaded = mediaCtx.isAudioEncoderLoaded() && mediaCtx.isTextEncoderLoaded() &&
                                mediaCtx.isTokenizerLoaded();

    if (analyzerLoaded) {
        nextStates[1] = LocalModelState::Loaded;
        tooltips[1] = "Sample Analyzer: loaded";
    } else if (mediaCtx.isLoadInProgress()) {
        nextStates[1] = LocalModelState::Loading;
        tooltips[1] = "Sample Analyzer: loading";
    } else if (analyzerInstalled) {
        nextStates[1] = LocalModelState::Idle;
        tooltips[1] = "Sample Analyzer: installed";
    } else {
        nextStates[1] = LocalModelState::Unavailable;
        tooltips[1] = "Sample Analyzer: not installed";
    }

    bool changed = nextStates != localModelStates_;
    localModelStates_ = nextStates;

    for (size_t i = 0; i < localModelButtons_.size(); ++i) {
        if (localModelButtons_[i] == nullptr)
            continue;
        if (localModelButtons_[i]->getTooltip() != tooltips[i]) {
            localModelButtons_[i]->setTooltip(tooltips[i]);
            changed = true;
        }
    }

    return changed;
}

void FooterBar::setupBottomCollapseButton() {
    // Start with "close" icon (panel starts expanded by default)
    bottomCollapseButton_ = std::make_unique<SvgButton>(
        "BottomCollapse", BinaryData::bottom_close_svg, BinaryData::bottom_close_svgSize);
    bottomCollapseButton_->setOriginalColor(juce::Colour(0xFFBCBCBC));
    bottomCollapseButton_->onClick = [this]() {
        if (onBottomPanelCollapseToggle)
            onBottomPanelCollapseToggle();
    };
    addAndMakeVisible(*bottomCollapseButton_);
}

void FooterBar::setBottomPanelCollapsed(bool collapsed) {
    bottomCollapsed_ = collapsed;
    updateBottomCollapseIcon();
}

void FooterBar::updateBottomCollapseIcon() {
    if (!bottomCollapseButton_)
        return;

    if (bottomCollapsed_) {
        bottomCollapseButton_->updateSvgData(BinaryData::bottom_open_svg,
                                             BinaryData::bottom_open_svgSize);
    } else {
        bottomCollapseButton_->updateSvgData(BinaryData::bottom_close_svg,
                                             BinaryData::bottom_close_svgSize);
    }
}

}  // namespace magda
