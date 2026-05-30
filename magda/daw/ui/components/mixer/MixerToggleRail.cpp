#include "MixerToggleRail.hpp"

#include <BinaryData.h>

#include "../../themes/DarkTheme.hpp"
#include "core/Config.hpp"

namespace magda {

MixerToggleRail::MixerToggleRail() {
    auto& cfg = Config::getInstance();

    setupButton(sendsButton_, "MixerShowSends", BinaryData::send_svg, BinaryData::send_svgSize,
                "Show sends", cfg.getMixerShowSends(),
                [](bool v) { Config::getInstance().setMixerShowSends(v); });

    setupButton(routingButton_, "MixerShowRouting", BinaryData::inputoutput_svg,
                BinaryData::inputoutput_svgSize, "Show I/O routing", cfg.getMixerShowRouting(),
                [](bool v) { Config::getInstance().setMixerShowRouting(v); });

    setupButton(monitorButton_, "MixerShowMonitor", BinaryData::recordmonitor_svg,
                BinaryData::recordmonitor_svgSize, "Show record/monitor row",
                cfg.getMixerShowMonitor(),
                [](bool v) { Config::getInstance().setMixerShowMonitor(v); });

    setupButton(oscilloscopeButton_, "MixerShowOscilloscope", BinaryData::oscilloscope_svg,
                BinaryData::oscilloscope_svgSize, "Show mini oscilloscope",
                cfg.getMixerShowOscilloscope(),
                [](bool v) { Config::getInstance().setMixerShowOscilloscope(v); });

    setupButton(spectrumButton_, "MixerShowSpectrum", BinaryData::spectrum_svg,
                BinaryData::spectrum_svgSize, "Show mini spectrum", cfg.getMixerShowSpectrum(),
                [](bool v) { Config::getInstance().setMixerShowSpectrum(v); });

    setupButton(fxChainButton_, "MixerShowFxChain", BinaryData::fxchain_svg,
                BinaryData::fxchain_svgSize, "Show mini FX chain", cfg.getMixerShowFxChain(),
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
    // anchor to the bottom.
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

}  // namespace magda
