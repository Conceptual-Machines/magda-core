#include "layout/DeviceShellPainter.hpp"

#include <cmath>
#include <iterator>

#include "audio/io/AudioIOControl.hpp"
#include "core/TrackManager.hpp"
#include "engine/AudioEngine.hpp"
#include "ui/components/mixer/LevelMeterScale.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui::device_shell {

namespace {

constexpr float kRadius = 7.0f;

void paintHeader(juce::Graphics& g, juce::Rectangle<float> frame, int headerHeight,
                 const ShellRows& rows) {
    // The header band keeps the frame's top corners and squares its bottom.
    juce::Path header;
    header.addRoundedRectangle(frame.getX(), frame.getY(), frame.getWidth(),
                               static_cast<float>(headerHeight), kRadius, kRadius, true, true,
                               false, false);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_HEAD));
    g.fillPath(header);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));
    g.fillRect(frame.getX(), frame.getY() + static_cast<float>(headerHeight) - 1.0f,
               frame.getWidth(), 1.0f);

    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE2));
    for (const auto& separator : {rows.headerSeparatorLeft, rows.headerSeparatorRight})
        if (!separator.isEmpty())
            g.fillRect(separator);
}

void paintIdRowAndStrip(juce::Graphics& g, juce::Rectangle<float> frame, const ShellRows& rows) {
    const auto line = ActiveTheme::getColour(ActiveTheme::DEVICE_LINE);

    if (!rows.idRow.isEmpty()) {
        const juce::Rectangle<float> row(frame.getX(), static_cast<float>(rows.idRow.getY()),
                                         frame.getWidth(),
                                         static_cast<float>(rows.idRow.getHeight()));
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_ID_ROW));
        g.fillRect(row);
        g.setColour(line);
        g.fillRect(row.getX(), row.getBottom() - 1.0f, row.getWidth(), 1.0f);
    }

    if (!rows.sideStrip.isEmpty()) {
        const juce::Rectangle<float> strip(
            static_cast<float>(rows.sideStrip.getX()), static_cast<float>(rows.sideStrip.getY()),
            frame.getRight() - static_cast<float>(rows.sideStrip.getX()),
            static_cast<float>(rows.sideStrip.getHeight()));
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_HEAD2));
        g.fillRect(strip);
        g.setColour(line);
        g.fillRect(strip.getX(), strip.getY(), 1.0f, strip.getHeight());
    }
}

void paintFooter(juce::Graphics& g, juce::Rectangle<float> frame, const ShellRows& rows,
                 const juce::String& footerInfo, bool midiLedLit) {
    const auto line = ActiveTheme::getColour(ActiveTheme::DEVICE_LINE);
    const float top = static_cast<float>(rows.footer.getY());
    juce::Path footer;
    footer.addRoundedRectangle(frame.getX(), top, frame.getWidth(), frame.getBottom() - top,
                               kRadius, kRadius, false, false, true, true);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_HEAD2));
    g.fillPath(footer);
    g.setColour(line);
    g.fillRect(frame.getX(), top, frame.getWidth(), 1.0f);

    if (!rows.footerSeparator.isEmpty()) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE2));
        g.fillRect(rows.footerSeparator);
    }

    if (!rows.footerInfo.isEmpty()) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
        g.setFont(FontManager::getInstance().getMonoFont(11.0f));
        g.drawText(footerInfo, rows.footerInfo, juce::Justification::centredLeft, false);
    }

    if (!rows.midiLed.isEmpty()) {
        const auto led = rows.midiLed.toFloat();
        if (midiLedLit) {
            const auto green = ActiveTheme::getColour(ActiveTheme::DEVICE_GREEN);
            g.setColour(green.withAlpha(0.35f));
            g.fillEllipse(led.expanded(3.0f));
            g.setColour(green);
        } else {
            g.setColour(line);
        }
        g.fillEllipse(led);
    }
}

}  // namespace

void paintFrame(juce::Graphics& g, juce::Rectangle<int> bounds, int headerHeight,
                const ShellRows& rows, const juce::String& footerInfo, bool midiLedLit) {
    const auto frame = bounds.toFloat();
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_BG));
    g.fillRoundedRectangle(frame, kRadius);

    if (headerHeight > 0)
        paintHeader(g, frame, headerHeight, rows);
    paintIdRowAndStrip(g, frame, rows);
    if (!rows.footer.isEmpty())
        paintFooter(g, frame, rows, footerInfo, midiLedLit);

    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FRAME_BORDER));
    g.drawRoundedRectangle(frame.reduced(0.5f), kRadius, 1.0f);
}

juce::String audioInfoText(int outputChannels) {
    double sampleRate = 0.0;
    if (auto* engine = magda::TrackManager::getInstance().getAudioEngine())
        if (auto* audioIO = engine->getAudioIO())
            sampleRate = audioIO->status().sampleRate;
    juce::String info = outputChannels == 1 ? "mono" : "stereo";
    if (sampleRate > 0.0)
        info << juce::String::fromUTF8(" \xc2\xb7 ")
             << juce::String(sampleRate / 1000.0, std::fmod(sampleRate, 1000.0) == 0.0 ? 0 : 1)
             << " kHz";
    return info;
}

void paintGainSlider(juce::Graphics& g, juce::Rectangle<int> area, double db, double minDb) {
    const auto track = area.toFloat();
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_WELL));
    g.fillRoundedRectangle(track, 4.0f);

    const float fill =
        track.getWidth() * static_cast<float>(magda::level_meter_scale::dbFillProportion(db));
    if (fill > 0.0f) {
        g.setGradientFill(juce::ColourGradient(
            ActiveTheme::getColour(ActiveTheme::DEVICE_SLIDER_FILL_LO), track.getX(), 0.0f,
            ActiveTheme::getColour(ActiveTheme::DEVICE_SLIDER_FILL_HI), track.getRight(), 0.0f,
            false));
        g.fillRoundedRectangle(track.withWidth(fill), 4.0f);
    }
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER));
    g.drawRoundedRectangle(track.reduced(0.5f), 4.0f, 1.0f);

    const auto text = db <= minDb + 0.05 ? juce::String("-inf dB") : juce::String(db, 1) + " dB";
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_VALUE_TEXT));
    g.setFont(FontManager::getInstance().getMonoFont(10.5f));
    g.drawText(text, area, juce::Justification::centred, false);
}

void paintPanSlider(juce::Graphics& g, juce::Rectangle<int> area, double pan) {
    const auto track = area.toFloat();
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_WELL));
    g.fillRoundedRectangle(track, 4.0f);

    const float amount = static_cast<float>(juce::jlimit(-1.0, 1.0, pan));
    const float centre = track.getCentreX();
    const float reach = track.getWidth() * 0.5f * std::abs(amount);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_SLIDER_FILL_HI));
    g.fillRect(juce::Rectangle<float>(amount < 0.0f ? centre - reach : centre, track.getY() + 1.0f,
                                      reach, track.getHeight() - 2.0f));
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));
    g.fillRect(
        juce::Rectangle<float>(centre - 0.5f, track.getY() + 1.0f, 1.0f, track.getHeight() - 2.0f));
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER));
    g.drawRoundedRectangle(track.reduced(0.5f), 4.0f, 1.0f);

    const int percent = juce::roundToInt(std::abs(amount) * 100.0f);
    const auto text =
        percent == 0 ? juce::String("C") : juce::String(percent) + (amount < 0.0f ? " L" : " R");
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
    g.setFont(FontManager::getInstance().getMonoFont(10.5f));
    g.drawText(text, area, juce::Justification::centred, false);
}

juce::Colour chainColour(int index) {
    // oklch(0.62 0.12 hue), the spec's chain dot, converted to sRGB.
    static constexpr float hues[] = {240.0f, 150.0f, 25.0f, 300.0f, 85.0f, 195.0f, 345.0f, 120.0f};
    const float hue = juce::degreesToRadians(hues[static_cast<size_t>(index) % std::size(hues)]);
    const float L = 0.62f, a = 0.12f * std::cos(hue), b = 0.12f * std::sin(hue);
    const float l = std::pow(L + 0.3963377774f * a + 0.2158037573f * b, 3.0f);
    const float m = std::pow(L - 0.1055613458f * a - 0.0638541728f * b, 3.0f);
    const float s = std::pow(L - 0.0894841775f * a - 1.2914855480f * b, 3.0f);
    const auto encode = [](float linear) {
        linear = juce::jlimit(0.0f, 1.0f, linear);
        const float v = linear <= 0.0031308f ? 12.92f * linear
                                             : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
        return static_cast<juce::uint8>(juce::roundToInt(v * 255.0f));
    };
    return juce::Colour(encode(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s),
                        encode(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s),
                        encode(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s));
}

void styleDeltaButton(juce::TextButton& delta) {
    delta.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    delta.setColour(juce::TextButton::buttonOnColourId,
                    ActiveTheme::getColour(ActiveTheme::DEVICE_ICON_HOVER_BG));
    delta.setColour(juce::TextButton::textColourOffId,
                    ActiveTheme::getColour(ActiveTheme::DEVICE_ICON));
    delta.setColour(juce::TextButton::textColourOnId,
                    ActiveTheme::getColour(ActiveTheme::DEVICE_BLUE));
}

}  // namespace magda::daw::ui::device_shell
