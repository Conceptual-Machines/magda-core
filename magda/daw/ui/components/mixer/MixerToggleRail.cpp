#include "MixerToggleRail.hpp"

#include <BinaryData.h>

#include "../../themes/DarkTheme.hpp"
#include "MixAnalysisModal.hpp"
#include "core/Config.hpp"

namespace magda {

MixerToggleRail::MixerToggleRail() {
    auto& cfg = Config::getInstance();

    setupAnalyzeButton();
    MixAnalysisService::getInstance().addListener(this);

    setupButton(sendsButton_, "MixerShowSends", BinaryData::iconsendsboldm_svg,
                BinaryData::iconsendsboldm_svgSize, "Show sends", cfg.getMixerShowSends(),
                [](bool v) { Config::getInstance().setMixerShowSends(v); });

    setupButton(routingButton_, "MixerShowRouting", BinaryData::inputoutput_svg,
                BinaryData::inputoutput_svgSize, "Show I/O routing", cfg.getMixerShowRouting(),
                [](bool v) { Config::getInstance().setMixerShowRouting(v); });

    setupButton(monitorButton_, "MixerShowMonitor", BinaryData::recordmonitor_svg,
                BinaryData::recordmonitor_svgSize, "Show record/monitor row",
                cfg.getMixerShowMonitor(),
                [](bool v) { Config::getInstance().setMixerShowMonitor(v); });

    setupButton(oscilloscopeButton_, "MixerShowOscilloscope", BinaryData::oscilloscope3_svg,
                BinaryData::oscilloscope3_svgSize, "Show mini oscilloscope",
                cfg.getMixerShowOscilloscope(),
                [](bool v) { Config::getInstance().setMixerShowOscilloscope(v); });

    setupButton(spectrumButton_, "MixerShowSpectrum", BinaryData::iconspectrumboldm_svg,
                BinaryData::iconspectrumboldm_svgSize, "Show mini spectrum",
                cfg.getMixerShowSpectrum(),
                [](bool v) { Config::getInstance().setMixerShowSpectrum(v); });

    setupButton(fxChainButton_, "MixerShowFxChain", BinaryData::iconinsertsboldm_svg,
                BinaryData::iconinsertsboldm_svgSize, "Show mini FX chain",
                cfg.getMixerShowFxChain(),
                [](bool v) { Config::getInstance().setMixerShowFxChain(v); });
}

void MixerToggleRail::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.fillRect(bounds.getRight() - 1, bounds.getY(), 1, bounds.getHeight());
}

void MixerToggleRail::resized() {
    constexpr int BTN_SIZE = 28;
    constexpr int BTN_SPACING = 6;
    constexpr int EDGE_PADDING = 8;

    auto bounds = getLocalBounds();
    int x = (bounds.getWidth() - BTN_SIZE) / 2;

    // Rail mirrors the channel-strip layout: things that live near the top of
    // the strip (analyzers / sends / FX chain) anchor to the top of the rail;
    // things that live near the bottom of the strip (routing / monitor)
    // anchor to the bottom. The whole-mix Analyze action sits on its own, centred.
    SvgButton* topGroup[] = {oscilloscopeButton_.get(), spectrumButton_.get(), sendsButton_.get(),
                             fxChainButton_.get()};
    SvgButton* bottomGroup[] = {routingButton_.get(), monitorButton_.get()};

    int yTop = EDGE_PADDING;
    for (auto* btn : topGroup) {
        if (btn != nullptr) {
            btn->setBounds(x, yTop, BTN_SIZE, BTN_SIZE);
            yTop += BTN_SIZE + BTN_SPACING;
        }
    }

    // Analyze: centred vertically in the rail, distinct from the toggle groups.
    if (analyzeButton_ != nullptr)
        analyzeButton_->setBounds(x, (bounds.getHeight() - BTN_SIZE) / 2, BTN_SIZE, BTN_SIZE);

    int yBottom = bounds.getHeight() - EDGE_PADDING - BTN_SIZE;
    for (auto* btn : bottomGroup) {
        if (btn != nullptr) {
            btn->setBounds(x, yBottom, BTN_SIZE, BTN_SIZE);
            yBottom -= BTN_SIZE + BTN_SPACING;
        }
    }
}

void MixerToggleRail::setupButton(std::unique_ptr<SvgButton>& btn, const juce::String& name,
                                  const char* svgData, size_t svgSize, const juce::String& tooltip,
                                  bool initialState, std::function<void(bool)> setter) {
    btn = std::make_unique<SvgButton>(name, svgData, svgSize);
    btn->setOriginalColor(juce::Colour(0xFFB3B3B3));
    btn->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    btn->setPressedColor(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    btn->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    btn->setBorderThickness(1.0f);
    btn->setTooltip(tooltip);
    btn->setWantsKeyboardFocus(false);
    applyToggleState(btn.get(), initialState);

    btn->onClick = [this, raw = btn.get(), setter = std::move(setter)]() {
        bool newState = !raw->isActive();
        setter(newState);
        applyToggleState(raw, newState);
        Config::getInstance().save();
        if (onToggleChanged)
            onToggleChanged();
    };

    addAndMakeVisible(*btn);
}

void MixerToggleRail::applyToggleState(SvgButton* btn, bool on) {
    if (btn == nullptr)
        return;
    btn->setActive(on);
    const auto base = DarkTheme::getColour(DarkTheme::TEXT_SECONDARY);
    btn->setNormalColor(on ? base : base.withAlpha(0.3f));
    btn->repaint();
}

// ---------------------------------------------------------------------------
// Analyze action button (whole-mix measured analysis -> modal)
// ---------------------------------------------------------------------------

MixerToggleRail::~MixerToggleRail() {
    MixAnalysisService::getInstance().removeListener(this);
}

void MixerToggleRail::setupAnalyzeButton() {
    analyzeButton_ = std::make_unique<SvgButton>("MixAnalyze", BinaryData::iconcheckmixboldm_svg,
                                                 BinaryData::iconcheckmixboldm_svgSize);
    analyzeButton_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    analyzeButton_->setNormalColor(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    analyzeButton_->setHoverColor(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    analyzeButton_->setActiveColor(DarkTheme::getColour(DarkTheme::ACCENT_CYAN));
    analyzeButton_->setActiveBackgroundColor(
        DarkTheme::getColour(DarkTheme::ACCENT_CYAN).withAlpha(0.25f));
    analyzeButton_->setBorderColor(DarkTheme::getColour(DarkTheme::BORDER));
    analyzeButton_->setBorderThickness(1.0f);
    analyzeButton_->setTooltip("Analyze the mix");
    analyzeButton_->setWantsKeyboardFocus(false);
    // One action: analyse the selection via an offline render. (Live capture is
    // implemented in MixAnalysisService/MixAnalysisModal but deliberately not
    // exposed yet -- postponed to keep the first ship's test surface small;
    // re-enable by offering a Live/Offline menu here.)
    analyzeButton_->onClick = [this]() { openModal(MixAnalysisService::Mode::Offline); };
    addAndMakeVisible(*analyzeButton_);
    updateAnalyzeButtonMode();
}

void MixerToggleRail::openModal(MixAnalysisService::Mode mode) {
    auto content = std::make_unique<MixAnalysisModal>(mode);
    juce::CallOutBox::launchAsynchronously(std::move(content), analyzeButton_->getScreenBounds(),
                                           nullptr);
}

void MixerToggleRail::updateAnalyzeButtonMode() {
    if (!analyzeButton_)
        return;
    auto& svc = MixAnalysisService::getInstance();
    if (svc.isBusy()) {
        // Analyzing: a stop glyph (clicking reopens the modal to stop there).
        analyzeButton_->updateSvgData(BinaryData::server_stop_svg, BinaryData::server_stop_svgSize);
        analyzeButton_->setActive(true);
        analyzeButton_->setTooltip("Analyzing... (click to view / stop)");
    } else if (svc.isCapturing()) {
        analyzeButton_->updateSvgData(BinaryData::analysis2_svg, BinaryData::analysis2_svgSize);
        analyzeButton_->setActive(true);
        analyzeButton_->setTooltip("Listening... (click to view / stop)");
    } else {
        analyzeButton_->updateSvgData(BinaryData::iconcheckmixboldm_svg,
                                      BinaryData::iconcheckmixboldm_svgSize);
        analyzeButton_->setActive(false);
        analyzeButton_->setTooltip("Analyze the mix");
        // Dim to the disengaged-toggle look until there's an analysis to show;
        // brighten once data exists (matches the other rail buttons' on/off weight).
        const auto base = DarkTheme::getColour(DarkTheme::TEXT_SECONDARY);
        const bool hasData = svc.latest().has_value();
        analyzeButton_->setNormalColor(hasData ? base : base.withAlpha(0.3f));
    }
    analyzeButton_->repaint();
}

void MixerToggleRail::mixAnalysisChanged() {
    updateAnalyzeButtonMode();
}

}  // namespace magda
