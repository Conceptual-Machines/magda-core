#include "layout/DeviceShellPainter.hpp"

#include <cmath>

#include "audio/io/AudioIOControl.hpp"
#include "core/TrackManager.hpp"
#include "engine/AudioEngine.hpp"
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
