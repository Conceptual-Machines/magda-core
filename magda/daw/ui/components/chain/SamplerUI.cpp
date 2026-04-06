#include "SamplerUI.hpp"

#include <BinaryData.h>

#include <cmath>

#include "ui/themes/CursorManager.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

SamplerUI::SamplerUI() {
    // Sample name label
    sampleNameLabel_.setText("No sample loaded", juce::dontSendNotification);
    sampleNameLabel_.setFont(FontManager::getInstance().getUIFont(11.0f));
    sampleNameLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    sampleNameLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(sampleNameLabel_);

    // Load button (folder icon)
    loadButton_ = std::make_unique<magda::SvgButton>("Load Sample", BinaryData::folderopen_svg,
                                                     BinaryData::folderopen_svgSize);
    loadButton_->onClick = [this]() {
        if (onLoadSampleRequested)
            onLoadSampleRequested();
    };
    addAndMakeVisible(*loadButton_);

    // Root note slider (MIDI note 0-127, displayed as note name)
    rootNoteSlider_.setRange(0, 127, 1);
    rootNoteSlider_.setValue(60, juce::dontSendNotification);
    rootNoteSlider_.setValueFormatter([](double v) {
        static const char* noteNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                          "F#", "G",  "G#", "A",  "A#", "B"};
        int note = juce::roundToInt(v);
        int octave = (note / 12) - 2;  // C3 = 60
        return juce::String(noteNames[note % 12]) + juce::String(octave);
    });
    rootNoteSlider_.setValueParser([](const juce::String& text) {
        // Parse note names like "C3", "F#4", "Bb2"
        juce::String t = text.trim().toUpperCase();
        if (t.isEmpty())
            return 60.0;
        static const char* noteNames[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                          "F#", "G",  "G#", "A",  "A#", "B"};
        // Try sharp notation first
        int semitone = -1;
        int nameLen = 0;
        for (int i = 0; i < 12; ++i) {
            juce::String nn(noteNames[i]);
            if (t.startsWith(nn) && nn.length() > nameLen) {
                semitone = i;
                nameLen = nn.length();
            }
        }
        // Handle flat (b) as alias for sharp of note below
        if (t.length() >= 2 && t[1] == 'B' && t[0] >= 'A' && t[0] <= 'G') {
            // e.g., "Bb" = A#
            for (int i = 0; i < 12; ++i) {
                juce::String nn(noteNames[i]);
                if (nn.length() == 1 && nn[0] == t[0]) {
                    semitone = (i + 11) % 12;  // one semitone below
                    nameLen = 2;
                    break;
                }
            }
        }
        if (semitone < 0)
            return 60.0;
        juce::String octStr = t.substring(nameLen).trim();
        int octave = octStr.isEmpty() ? 3 : octStr.getIntValue();
        return juce::jlimit(0.0, 127.0, static_cast<double>((octave + 2) * 12 + semitone));
    });
    rootNoteSlider_.onValueChanged = [this](double value) {
        if (onRootNoteChanged)
            onRootNoteChanged(juce::roundToInt(value));
    };
    addAndMakeVisible(rootNoteSlider_);

    setupLabel(rootNoteLabel_, "ROOT");
    addAndMakeVisible(rootNoteLabel_);

    // --- Time slider setup helper ---
    auto setupTimeSlider = [this](LinkableTextSlider& slider, int paramIndex, double min,
                                  double max, double defaultVal) {
        slider.setRange(min, max, 0.001);
        slider.setValue(defaultVal, juce::dontSendNotification);
        slider.setValueFormatter([](double v) {
            if (v < 0.01)
                return juce::String(v * 1000.0, 1) + " ms";
            if (v < 1.0)
                return juce::String(v * 1000.0, 0) + " ms";
            return juce::String(v, 2) + " s";
        });
        slider.setValueParser([](const juce::String& text) {
            juce::String t = text.trim();
            if (t.endsWithIgnoreCase("ms"))
                return static_cast<double>(t.dropLastCharacters(2).trim().getFloatValue()) / 1000.0;
            if (t.endsWithIgnoreCase("s"))
                return static_cast<double>(t.dropLastCharacters(1).trim().getFloatValue());
            double v = t.getDoubleValue();
            return v > 10.0 ? v / 1000.0 : v;  // assume ms if > 10
        });
        slider.onValueChanged = [this, paramIndex](double value) {
            if (onParameterChanged)
                onParameterChanged(paramIndex, static_cast<float>(value));
            repaint();
        };
        addAndMakeVisible(slider);
    };

    // --- Sample start slider (param index 7) ---
    setupTimeSlider(startSlider_, 7, 0.0, 300.0, 0.0);

    // --- Sample end slider (param index 8) ---
    setupTimeSlider(endSlider_, 8, 0.0, 300.0, 0.0);

    // --- Loop start slider (param index 9) ---
    setupTimeSlider(loopStartSlider_, 9, 0.0, 300.0, 0.0);

    // --- Loop end slider (param index 10) ---
    setupTimeSlider(loopEndSlider_, 10, 0.0, 300.0, 0.0);

    // --- Loop toggle button (SVG icon) ---
    loopButton_ = std::make_unique<magda::SvgButton>(
        "Loop", BinaryData::loop_off_svg, BinaryData::loop_off_svgSize, BinaryData::loop_on_svg,
        BinaryData::loop_on_svgSize);
    loopButton_->onClick = [this]() {
        bool newState = !loopButton_->isActive();
        loopButton_->setActive(newState);
        if (onLoopEnabledChanged)
            onLoopEnabledChanged(newState);
        repaint();
    };
    addAndMakeVisible(*loopButton_);

    // --- ADSR sliders ---
    setupTimeSlider(attackSlider_, 0, 0.001, 5.0, 0.001);
    setupTimeSlider(decaySlider_, 1, 0.001, 5.0, 0.1);

    // Sustain (0-1, no units)
    sustainSlider_.setRange(0.0, 1.0, 0.01);
    sustainSlider_.setValue(1.0, juce::dontSendNotification);
    sustainSlider_.setValueFormatter(
        [](double v) { return juce::String(static_cast<int>(v * 100)) + "%"; });
    sustainSlider_.setValueParser([](const juce::String& text) {
        juce::String t = text.trim();
        if (t.endsWithIgnoreCase("%"))
            t = t.dropLastCharacters(1).trim();
        double v = t.getDoubleValue();
        return v > 1.0 ? v / 100.0 : v;
    });
    sustainSlider_.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(2, static_cast<float>(value));
        repaint();
    };
    addAndMakeVisible(sustainSlider_);

    setupTimeSlider(releaseSlider_, 3, 0.001, 10.0, 0.1);

    // --- Pitch slider (-24 to +24 semitones) ---
    pitchSlider_.setRange(-24.0, 24.0, 1.0);
    pitchSlider_.setValue(0.0, juce::dontSendNotification);
    pitchSlider_.setValueFormatter(
        [](double v) { return juce::String(static_cast<int>(v)) + " st"; });
    pitchSlider_.setValueParser([](const juce::String& text) {
        return static_cast<double>(
            text.trim().upToFirstOccurrenceOf(" ", false, false).getIntValue());
    });
    pitchSlider_.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(4, static_cast<float>(value));
    };
    addAndMakeVisible(pitchSlider_);

    // --- Fine slider (-100 to +100 cents) ---
    fineSlider_.setRange(-100.0, 100.0, 1.0);
    fineSlider_.setValue(0.0, juce::dontSendNotification);
    fineSlider_.setValueFormatter(
        [](double v) { return juce::String(static_cast<int>(v)) + " ct"; });
    fineSlider_.setValueParser([](const juce::String& text) {
        return static_cast<double>(
            text.trim().upToFirstOccurrenceOf(" ", false, false).getIntValue());
    });
    fineSlider_.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(5, static_cast<float>(value));
    };
    addAndMakeVisible(fineSlider_);

    // --- Level slider (-60 to +12 dB) ---
    levelSlider_.setRange(-60.0, 12.0, 0.1);
    levelSlider_.setValue(0.0, juce::dontSendNotification);
    levelSlider_.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(6, static_cast<float>(value));
        // Update waveform scaling to reflect level
        waveformGain_ = juce::Decibels::decibelsToGain(static_cast<float>(value));
        if (waveformBuffer_ != nullptr) {
            auto waveArea = getWaveformBounds();
            buildWaveformPath(waveformBuffer_, waveArea.getWidth(), waveArea.getHeight() - 4);
            repaint();
        }
    };
    addAndMakeVisible(levelSlider_);

    // --- Velocity amount slider (0-100%) ---
    velAmountSlider_.setRange(0.0, 1.0, 0.01);
    velAmountSlider_.setValue(1.0, juce::dontSendNotification);
    velAmountSlider_.setValueFormatter(
        [](double v) { return juce::String(static_cast<int>(v * 100)) + "%"; });
    velAmountSlider_.setValueParser([](const juce::String& text) {
        juce::String t = text.trim();
        if (t.endsWithIgnoreCase("%"))
            t = t.dropLastCharacters(1).trim();
        double v = t.getDoubleValue();
        return v > 1.0 ? v / 100.0 : v;
    });
    velAmountSlider_.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(11, static_cast<float>(value));
        repaint();
    };
    addAndMakeVisible(velAmountSlider_);

    // --- Labels ---
    setupLabel(startLabel_, "START");
    setupLabel(endLabel_, "END");
    setupLabel(loopStartLabel_, "L.START");
    setupLabel(loopEndLabel_, "L.END");
    setupLabel(attackLabel_, "ATK");
    setupLabel(decayLabel_, "DEC");
    setupLabel(sustainLabel_, "SUS");
    setupLabel(releaseLabel_, "REL");
    setupLabel(pitchLabel_, "PITCH");
    setupLabel(fineLabel_, "FINE");
    setupLabel(levelLabel_, "LEVEL");
    setupLabel(velAmountLabel_, "VEL");
}

SamplerUI::~SamplerUI() {
    stopTimer();
}

void SamplerUI::setupLabel(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(FontManager::getInstance().getUIFont(9.0f));
    label.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

void SamplerUI::updateParameters(float attack, float decay, float sustain, float release,
                                 float pitch, float fine, float level, float sampleStart,
                                 float sampleEnd, bool loopEnabled, float loopStart, float loopEnd,
                                 float velAmount, const juce::String& sampleName, int rootNote) {
    attackSlider_.setValue(attack, juce::dontSendNotification);
    decaySlider_.setValue(decay, juce::dontSendNotification);
    sustainSlider_.setValue(sustain, juce::dontSendNotification);
    releaseSlider_.setValue(release, juce::dontSendNotification);
    pitchSlider_.setValue(pitch, juce::dontSendNotification);
    fineSlider_.setValue(fine, juce::dontSendNotification);
    levelSlider_.setValue(level, juce::dontSendNotification);
    waveformGain_ = juce::Decibels::decibelsToGain(level);
    velAmountSlider_.setValue(velAmount, juce::dontSendNotification);

    rootNoteSlider_.setValue(rootNote, juce::dontSendNotification);

    startSlider_.setValue(sampleStart, juce::dontSendNotification);
    endSlider_.setValue(sampleEnd, juce::dontSendNotification);
    loopButton_->setActive(loopEnabled);
    loopStartSlider_.setValue(loopStart, juce::dontSendNotification);
    loopEndSlider_.setValue(loopEnd, juce::dontSendNotification);

    if (sampleName.isNotEmpty()) {
        sampleNameLabel_.setText(sampleName, juce::dontSendNotification);
        sampleNameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    } else {
        sampleNameLabel_.setText("No sample loaded", juce::dontSendNotification);
        sampleNameLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    }
}

void SamplerUI::setWaveformData(const juce::AudioBuffer<float>* buffer, double sampleRate,
                                double sampleLengthSeconds) {
    sampleLength_ = sampleLengthSeconds;
    waveformBuffer_ = buffer;
    waveformSampleRate_ = sampleRate;

    if (buffer == nullptr || buffer->getNumSamples() == 0) {
        hasWaveform_ = false;
        waveformPath_.clear();
        waveformBuffer_ = nullptr;
        stopTimer();
        repaint();
        return;
    }

    // Update slider ranges to match sample length
    startSlider_.setRange(0.0, sampleLengthSeconds, 0.001);
    endSlider_.setRange(0.0, sampleLengthSeconds, 0.001);
    loopStartSlider_.setRange(0.0, sampleLengthSeconds, 0.001);
    loopEndSlider_.setRange(0.0, sampleLengthSeconds, 0.001);

    // Default end to sample length if not yet set
    if (endSlider_.getValue() < 0.001)
        endSlider_.setValue(sampleLengthSeconds, juce::dontSendNotification);

    hasWaveform_ = true;

    // Zoom-to-fit: entire sample fills the waveform width
    auto waveArea = getWaveformBounds();
    int waveWidth = waveArea.getWidth() > 0 ? waveArea.getWidth() : 200;
    pixelsPerSecond_ =
        (sampleLength_ > 0.0) ? static_cast<double>(waveWidth) / sampleLength_ : 100.0;
    scrollOffsetSeconds_ = 0.0;

    int waveHeight = juce::jmax(30, waveArea.getHeight() - 4);
    buildWaveformPath(buffer, waveWidth, waveHeight);

    if (!isTimerRunning())
        startTimerHz(30);

    repaint();
}

void SamplerUI::buildWaveformPath(const juce::AudioBuffer<float>* buffer, int width, int height) {
    waveformPath_.clear();
    if (buffer == nullptr || width <= 0 || height <= 0 || sampleLength_ <= 0.0)
        return;

    const float* data = buffer->getReadPointer(0);
    int numSamples = buffer->getNumSamples();
    float halfHeight = static_cast<float>(height) * 0.5f;

    // Visible time range
    double visibleStart = scrollOffsetSeconds_;

    // Convert visible range to sample indices
    double samplesPerSecond = static_cast<double>(numSamples) / sampleLength_;

    waveformPath_.startNewSubPath(0.0f, halfHeight);

    for (int x = 0; x < width; ++x) {
        double timeAtPixel = visibleStart + static_cast<double>(x) / pixelsPerSecond_;
        int startSample = static_cast<int>(timeAtPixel * samplesPerSecond);
        int endSample = static_cast<int>((timeAtPixel + 1.0 / pixelsPerSecond_) * samplesPerSecond);
        startSample = juce::jlimit(0, numSamples, startSample);
        endSample = juce::jlimit(0, numSamples, endSample);

        float maxVal = 0.0f;
        for (int s = startSample; s < endSample; ++s) {
            float absVal = std::abs(data[s]);
            if (absVal > maxVal)
                maxVal = absVal;
        }
        maxVal *= waveformGain_;

        float y = halfHeight - maxVal * halfHeight;
        waveformPath_.lineTo(static_cast<float>(x), y);
    }

    // Mirror for bottom half
    for (int x = width - 1; x >= 0; --x) {
        double timeAtPixel = visibleStart + static_cast<double>(x) / pixelsPerSecond_;
        int startSample = static_cast<int>(timeAtPixel * samplesPerSecond);
        int endSample = static_cast<int>((timeAtPixel + 1.0 / pixelsPerSecond_) * samplesPerSecond);
        startSample = juce::jlimit(0, numSamples, startSample);
        endSample = juce::jlimit(0, numSamples, endSample);

        float maxVal = 0.0f;
        for (int s = startSample; s < endSample; ++s) {
            float absVal = std::abs(data[s]);
            if (absVal > maxVal)
                maxVal = absVal;
        }
        maxVal *= waveformGain_;

        float y = halfHeight + maxVal * halfHeight;
        waveformPath_.lineTo(static_cast<float>(x), y);
    }

    waveformPath_.closeSubPath();
}

bool SamplerUI::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& f : files) {
        if (f.endsWithIgnoreCase(".wav") || f.endsWithIgnoreCase(".aif") ||
            f.endsWithIgnoreCase(".aiff") || f.endsWithIgnoreCase(".flac") ||
            f.endsWithIgnoreCase(".ogg") || f.endsWithIgnoreCase(".mp3"))
            return true;
    }
    return false;
}

void SamplerUI::filesDropped(const juce::StringArray& files, int /*x*/, int /*y*/) {
    for (const auto& f : files) {
        juce::File file(f);
        if (file.existsAsFile() && onFileDropped) {
            onFileDropped(file);
            break;
        }
    }
}

// =============================================================================
// Coordinate Mapping
// =============================================================================

juce::Rectangle<int> SamplerUI::getWaveformBounds() const {
    auto area = getLocalBounds().reduced(4);
    area.removeFromTop(22);  // Skip sample name row (20 + 2 gap)
    // Controls below: label(12) + slider(18) + margin = 38
    static constexpr int kControlsHeight = 38;
    int waveHeight = juce::jmax(30, area.getHeight() - kControlsHeight - 2);
    return area.removeFromTop(waveHeight);
}

float SamplerUI::secondsToPixelX(double seconds, juce::Rectangle<int> waveArea) const {
    if (sampleLength_ <= 0.0)
        return static_cast<float>(waveArea.getX());
    float x =
        static_cast<float>(waveArea.getX() + (seconds - scrollOffsetSeconds_) * pixelsPerSecond_);
    // Clamp so rightmost markers remain visible within the clip region
    return juce::jmin(x, static_cast<float>(waveArea.getRight() - 1));
}

double SamplerUI::pixelXToSeconds(float pixelX, juce::Rectangle<int> waveArea) const {
    if (waveArea.getWidth() <= 0 || sampleLength_ <= 0.0 || pixelsPerSecond_ <= 0.0)
        return 0.0;
    double seconds =
        scrollOffsetSeconds_ + static_cast<double>(pixelX - waveArea.getX()) / pixelsPerSecond_;
    return juce::jlimit(0.0, sampleLength_, seconds);
}

// =============================================================================
// Mouse Interaction on Waveform
// =============================================================================

SamplerUI::DragTarget SamplerUI::markerHitTest(const juce::MouseEvent& e,
                                               juce::Rectangle<int> waveArea) const {
    if (!hasWaveform_ || sampleLength_ <= 0.0)
        return DragTarget::None;

    float mx = static_cast<float>(e.getPosition().x);
    int my = e.getPosition().y;

    // Check sample end marker
    float endX = secondsToPixelX(endSlider_.getValue(), waveArea);
    if (std::abs(mx - endX) <= kMarkerHitPixels)
        return DragTarget::SampleEnd;

    if (loopButton_->isActive()) {
        float lStartX = secondsToPixelX(loopStartSlider_.getValue(), waveArea);
        float lEndX = secondsToPixelX(loopEndSlider_.getValue(), waveArea);

        // Check loop start/end markers (prioritise over region)
        if (std::abs(mx - lStartX) <= kMarkerHitPixels)
            return DragTarget::LoopStart;
        if (std::abs(mx - lEndX) <= kMarkerHitPixels)
            return DragTarget::LoopEnd;

        // Check loop top bar (drag entire region)
        if (lEndX > lStartX && mx >= lStartX && mx <= lEndX && my >= waveArea.getY() &&
            my < waveArea.getY() + kLoopBarHeight)
            return DragTarget::LoopRegion;
    }

    // Check envelope breakpoints
    auto envTarget = envHitTest(e, waveArea);
    if (envTarget != DragTarget::None)
        return envTarget;

    return DragTarget::None;
}

SamplerUI::DragTarget SamplerUI::envHitTest(const juce::MouseEvent& e,
                                            juce::Rectangle<int> waveArea) const {
    if (!hasWaveform_ || sampleLength_ <= 0.0)
        return DragTarget::None;

    float mx = static_cast<float>(e.getPosition().x);
    float my = static_cast<float>(e.getPosition().y);
    float wY = static_cast<float>(waveArea.getY());
    float wH = static_cast<float>(waveArea.getHeight());

    double startSec = startSlider_.getValue();
    float a = static_cast<float>(attackSlider_.getValue());
    float d = static_cast<float>(decaySlider_.getValue());
    float s = static_cast<float>(sustainSlider_.getValue());
    float noteDur = static_cast<float>(endSlider_.getValue() - startSec);
    float r = static_cast<float>(releaseSlider_.getValue());

    if (noteDur <= 0.0f)
        return DragTarget::None;

    auto envX = [&](float t) -> float {
        return secondsToPixelX(startSec + static_cast<double>(t), waveArea);
    };
    auto envY = [&](float level) -> float { return wY + wH * (1.0f - level); };

    constexpr float hitR = 8.0f;

    // Attack breakpoint (end of attack = peak)
    float ax = envX(a);
    float ay = envY(1.0f);
    if ((mx - ax) * (mx - ax) + (my - ay) * (my - ay) <= hitR * hitR)
        return DragTarget::EnvAttack;

    // Decay breakpoint (end of decay = sustain level)
    float dx = envX(a + d);
    float dy = envY(s);
    if ((mx - dx) * (mx - dx) + (my - dy) * (my - dy) <= hitR * hitR)
        return DragTarget::EnvDecay;

    // Release breakpoint (end of release = zero)
    float rx = envX(noteDur + r);
    float ry = envY(0.0f);
    if ((mx - rx) * (mx - rx) + (my - ry) * (my - ry) <= hitR * hitR)
        return DragTarget::EnvRelease;

    return DragTarget::None;
}

void SamplerUI::mouseDown(const juce::MouseEvent& e) {
    auto waveArea = getWaveformBounds();
    if (!waveArea.contains(e.getPosition()) || !hasWaveform_) {
        Component::mouseDown(e);
        return;
    }

    // Alt+click or middle-click => scroll
    if (e.mods.isAltDown() || e.mods.isMiddleButtonDown()) {
        currentDrag_ = DragTarget::Scroll;
        scrollDragStartOffset_ = scrollOffsetSeconds_;
        return;
    }

    // Cmd+click => zoom drag (drag up = zoom in, drag down = zoom out)
    if (e.mods.isCommandDown()) {
        currentDrag_ = DragTarget::Zoom;
        zoomDragStartY_ = e.getPosition().y;
        zoomDragStartPPS_ = pixelsPerSecond_;
        zoomDragAnchorTime_ = pixelXToSeconds(static_cast<float>(e.getPosition().x), waveArea);
        zoomDragAnchorPixelOffset_ = e.getPosition().x - waveArea.getX();
        setMouseCursor(CursorManager::getInstance().getZoomCursor());
        return;
    }

    // Try hit-testing existing markers/loop bar first
    currentDrag_ = markerHitTest(e, waveArea);

    if (currentDrag_ == DragTarget::LoopRegion) {
        loopDragStartL_ = loopStartSlider_.getValue();
        loopDragStartR_ = loopEndSlider_.getValue();
        return;
    }

    // Shift+click = set loop start
    if (currentDrag_ == DragTarget::None && e.mods.isShiftDown()) {
        currentDrag_ = DragTarget::LoopStart;
    }

    if (currentDrag_ == DragTarget::None)
        return;

    // For non-envelope targets, set marker position immediately
    if (currentDrag_ != DragTarget::EnvAttack && currentDrag_ != DragTarget::EnvDecay &&
        currentDrag_ != DragTarget::EnvRelease) {
        double seconds = pixelXToSeconds(static_cast<float>(e.getPosition().x), waveArea);
        switch (currentDrag_) {
            case DragTarget::SampleEnd:
                endSlider_.setValue(seconds, juce::sendNotificationSync);
                break;
            case DragTarget::LoopStart:
                loopStartSlider_.setValue(seconds, juce::sendNotificationSync);
                break;
            case DragTarget::LoopEnd:
                loopEndSlider_.setValue(seconds, juce::sendNotificationSync);
                break;
            default:
                break;
        }
    }
    repaint();
}

void SamplerUI::mouseDrag(const juce::MouseEvent& e) {
    auto waveArea = getWaveformBounds();
    if (currentDrag_ == DragTarget::None || !hasWaveform_) {
        Component::mouseDrag(e);
        return;
    }

    if (currentDrag_ == DragTarget::Scroll) {
        double pixelDelta = static_cast<double>(e.getDistanceFromDragStartX());
        double timeDelta = pixelDelta / pixelsPerSecond_;
        double visibleDuration = static_cast<double>(waveArea.getWidth()) / pixelsPerSecond_;
        double maxScroll = juce::jmax(0.0, sampleLength_ - visibleDuration);
        scrollOffsetSeconds_ = juce::jlimit(0.0, maxScroll, scrollDragStartOffset_ - timeDelta);

        if (waveformBuffer_ != nullptr)
            buildWaveformPath(waveformBuffer_, waveArea.getWidth(), waveArea.getHeight() - 4);
        repaint();
        return;
    }

    if (currentDrag_ == DragTarget::Zoom) {
        int deltaY = zoomDragStartY_ - e.getPosition().y;  // drag up = positive = zoom in

        // Update cursor based on zoom direction
        if (deltaY > 0)
            setMouseCursor(CursorManager::getInstance().getZoomInCursor());
        else if (deltaY < 0)
            setMouseCursor(CursorManager::getInstance().getZoomOutCursor());
        else
            setMouseCursor(CursorManager::getInstance().getZoomCursor());

        // Minimum zoom: entire sample fits in view
        double minPPS = static_cast<double>(waveArea.getWidth()) / sampleLength_;

        // Log-scale zoom with adaptive sensitivity
        double zoomRange = std::log(kMaxPixelsPerSecond) - std::log(minPPS);
        double zoomPosition = (std::log(zoomDragStartPPS_) - std::log(minPPS)) / zoomRange;
        double sensitivity = 20.0 + zoomPosition * 10.0;
        double absDeltaY = std::abs(static_cast<double>(deltaY));
        if (absDeltaY > 80.0)
            sensitivity /= 1.0 + (absDeltaY - 80.0) / 150.0;

        double exponent = static_cast<double>(deltaY) / sensitivity;
        double newPPS = zoomDragStartPPS_ * std::pow(2.0, exponent);
        newPPS = juce::jlimit(minPPS, kMaxPixelsPerSecond, newPPS);
        pixelsPerSecond_ = newPPS;

        // Keep anchor time under the same pixel
        scrollOffsetSeconds_ = zoomDragAnchorTime_ -
                               static_cast<double>(zoomDragAnchorPixelOffset_) / pixelsPerSecond_;

        // Clamp scroll
        double visibleDuration = static_cast<double>(waveArea.getWidth()) / pixelsPerSecond_;
        double maxScroll = juce::jmax(0.0, sampleLength_ - visibleDuration);
        scrollOffsetSeconds_ = juce::jlimit(0.0, maxScroll, scrollOffsetSeconds_);

        if (waveformBuffer_ != nullptr)
            buildWaveformPath(waveformBuffer_, waveArea.getWidth(), waveArea.getHeight() - 4);
        repaint();
        return;
    }

    if (currentDrag_ == DragTarget::LoopRegion) {
        double pixelDelta = static_cast<double>(e.getDistanceFromDragStartX());
        double timeDelta = pixelDelta / pixelsPerSecond_;
        double regionLen = loopDragStartR_ - loopDragStartL_;

        // Clamp so region stays within sample bounds
        double newL = loopDragStartL_ + timeDelta;
        if (newL < 0.0)
            newL = 0.0;
        if (newL + regionLen > sampleLength_)
            newL = sampleLength_ - regionLen;

        loopStartSlider_.setValue(newL, juce::sendNotificationSync);
        loopEndSlider_.setValue(newL + regionLen, juce::sendNotificationSync);
        repaint();
        return;
    }

    // Envelope breakpoint dragging
    if (currentDrag_ == DragTarget::EnvAttack || currentDrag_ == DragTarget::EnvDecay ||
        currentDrag_ == DragTarget::EnvRelease) {
        double startSec = startSlider_.getValue();
        double endSec = endSlider_.getValue();
        double seconds = pixelXToSeconds(static_cast<float>(e.getPosition().x), waveArea);
        float my = static_cast<float>(e.getPosition().y);
        float wY = static_cast<float>(waveArea.getY());
        float wH = static_cast<float>(waveArea.getHeight());
        float level = 1.0f - (my - wY) / wH;

        if (currentDrag_ == DragTarget::EnvAttack) {
            // Drag X = attack time (relative to sample start)
            float newAttack = juce::jlimit(0.001f, 5.0f, static_cast<float>(seconds - startSec));
            attackSlider_.setValue(newAttack, juce::sendNotificationSync);
        } else if (currentDrag_ == DragTarget::EnvDecay) {
            // Drag X = attack + decay time, drag Y = sustain level
            float attackVal = static_cast<float>(attackSlider_.getValue());
            float newDecay =
                juce::jlimit(0.001f, 5.0f, static_cast<float>(seconds - startSec) - attackVal);
            float newSustain = juce::jlimit(0.0f, 1.0f, level);
            decaySlider_.setValue(newDecay, juce::sendNotificationSync);
            sustainSlider_.setValue(newSustain, juce::sendNotificationSync);
        } else {
            // Release: drag X = release time (relative to note-off)
            float newRelease = juce::jlimit(0.001f, 10.0f, static_cast<float>(seconds - endSec));
            releaseSlider_.setValue(newRelease, juce::sendNotificationSync);
        }
        repaint();
        return;
    }

    double seconds = pixelXToSeconds(static_cast<float>(e.getPosition().x), waveArea);

    switch (currentDrag_) {
        case DragTarget::SampleEnd:
            endSlider_.setValue(seconds, juce::sendNotificationSync);
            break;
        case DragTarget::LoopStart:
            loopStartSlider_.setValue(seconds, juce::sendNotificationSync);
            break;
        case DragTarget::LoopEnd:
            loopEndSlider_.setValue(seconds, juce::sendNotificationSync);
            break;
        default:
            break;
    }
    repaint();
}

void SamplerUI::mouseUp(const juce::MouseEvent& e) {
    currentDrag_ = DragTarget::None;
    // Update cursor for whatever is now under the mouse
    mouseMove(e);
}

void SamplerUI::mouseMove(const juce::MouseEvent& e) {
    auto waveArea = getWaveformBounds();
    if (!waveArea.contains(e.getPosition()) || !hasWaveform_) {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    if (e.mods.isCommandDown()) {
        setMouseCursor(CursorManager::getInstance().getZoomCursor());
        return;
    }

    auto target = markerHitTest(e, waveArea);
    switch (target) {
        case DragTarget::SampleEnd:
        case DragTarget::LoopStart:
        case DragTarget::LoopEnd:
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            break;
        case DragTarget::LoopRegion:
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            break;
        case DragTarget::EnvAttack:
        case DragTarget::EnvDecay:
        case DragTarget::EnvRelease:
            setMouseCursor(juce::MouseCursor::CrosshairCursor);
            break;
        default:
            setMouseCursor(juce::MouseCursor::NormalCursor);
            break;
    }
}

void SamplerUI::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    auto waveArea = getWaveformBounds();
    if (!waveArea.contains(e.getPosition()) || !hasWaveform_ || sampleLength_ <= 0.0) {
        Component::mouseWheelMove(e, wheel);
        return;
    }

    // Minimum zoom: entire sample fits in view
    double minPPS = static_cast<double>(waveArea.getWidth()) / sampleLength_;

    // Anchor time under the cursor before zoom
    double anchorTime = pixelXToSeconds(static_cast<float>(e.getPosition().x), waveArea);

    // Apply zoom factor
    double zoomFactor = 1.0 + static_cast<double>(wheel.deltaY) * 0.15;
    double newPPS = pixelsPerSecond_ * zoomFactor;
    newPPS = juce::jlimit(minPPS, kMaxPixelsPerSecond, newPPS);
    pixelsPerSecond_ = newPPS;

    // Recalculate scroll so anchor time stays under cursor
    double anchorPixelOffset = static_cast<double>(e.getPosition().x - waveArea.getX());
    scrollOffsetSeconds_ = anchorTime - anchorPixelOffset / pixelsPerSecond_;

    // Clamp scroll
    double visibleDuration = static_cast<double>(waveArea.getWidth()) / pixelsPerSecond_;
    double maxScroll = juce::jmax(0.0, sampleLength_ - visibleDuration);
    scrollOffsetSeconds_ = juce::jlimit(0.0, maxScroll, scrollOffsetSeconds_);

    // Rebuild waveform at new zoom
    if (waveformBuffer_ != nullptr)
        buildWaveformPath(waveformBuffer_, waveArea.getWidth(), waveArea.getHeight() - 4);
    repaint();
}

// =============================================================================
// Timer (Playhead Animation)
// =============================================================================

std::vector<LinkableTextSlider*> SamplerUI::getLinkableSliders() {
    // Parameter indices: 0=attack, 1=decay, 2=sustain, 3=release, 4=pitch, 5=fine, 6=level,
    //                    7=sampleStart, 8=sampleEnd, 9=loopStart, 10=loopEnd, 11=velAmount
    return {&attackSlider_, &decaySlider_,     &sustainSlider_, &releaseSlider_,
            &pitchSlider_,  &fineSlider_,      &levelSlider_,   &startSlider_,
            &endSlider_,    &loopStartSlider_, &loopEndSlider_, &velAmountSlider_};
}

void SamplerUI::timerCallback() {
    if (getPlaybackPosition) {
        double newPos = getPlaybackPosition();
        if (std::abs(newPos - playheadPosition_) > 0.0001) {
            playheadPosition_ = newPos;
            repaint(getWaveformBounds());
        }
    }
}

// =============================================================================
// Paint
// =============================================================================

void SamplerUI::paint(juce::Graphics& g) {
    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds().reduced(1));

    // Waveform area
    auto waveformArea = getWaveformBounds();

    if (hasWaveform_ && !waveformArea.isEmpty()) {
        // Clip all waveform drawing to waveform bounds
        g.saveState();
        g.reduceClipRegion(waveformArea);

        // Draw waveform
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.3f));
        auto pathBounds = waveformArea.reduced(0, 2).toFloat();
        g.saveState();
        g.addTransform(juce::AffineTransform::translation(pathBounds.getX(), pathBounds.getY()));
        g.fillPath(waveformPath_);
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.7f));
        g.strokePath(waveformPath_, juce::PathStrokeType(0.5f));
        g.restoreState();

        // ADSR envelope overlay
        if (sampleLength_ > 0.0) {
            float a = static_cast<float>(attackSlider_.getValue());
            float d = static_cast<float>(decaySlider_.getValue());
            float s = static_cast<float>(sustainSlider_.getValue());
            float r = static_cast<float>(releaseSlider_.getValue());
            double startSec = startSlider_.getValue();
            double endSec = endSlider_.getValue();
            double noteDuration = endSec - startSec;

            if (noteDuration > 0.0) {
                float wY = static_cast<float>(waveformArea.getY());
                float wH = static_cast<float>(waveformArea.getHeight());

                // Time points (relative to sample start)
                float tAttack = a;
                float tDecay = tAttack + d;
                // Sustain holds until release begins (at note-off = end of sample region)
                float tRelease = static_cast<float>(noteDuration);
                float tEnd = tRelease + r;

                // Convert time offsets (from sample start) to pixel X
                auto envX = [&](float t) -> float {
                    return secondsToPixelX(startSec + static_cast<double>(t), waveformArea);
                };
                // Envelope level (1.0 = top, 0.0 = bottom) to pixel Y
                auto envY = [&](float level) -> float { return wY + wH * (1.0f - level); };

                juce::Path envPath;
                envPath.startNewSubPath(envX(0.0f), envY(0.0f));
                envPath.lineTo(envX(tAttack), envY(1.0f));
                envPath.lineTo(envX(tDecay), envY(s));
                envPath.lineTo(envX(tRelease), envY(s));
                envPath.lineTo(envX(tEnd), envY(0.0f));

                g.setColour(juce::Colours::white.withAlpha(0.6f));
                g.strokePath(envPath, juce::PathStrokeType(1.5f));

                // Breakpoint dots
                constexpr float dotR = 3.0f;
                g.fillEllipse(envX(tAttack) - dotR, envY(1.0f) - dotR, dotR * 2, dotR * 2);
                g.fillEllipse(envX(tDecay) - dotR, envY(s) - dotR, dotR * 2, dotR * 2);
                g.fillEllipse(envX(tEnd) - dotR, envY(0.0f) - dotR, dotR * 2, dotR * 2);
            }
        }

        // Loop region highlight (semi-transparent green) + top drag bar
        if (loopButton_->isActive() && sampleLength_ > 0.0) {
            float lStartX = secondsToPixelX(loopStartSlider_.getValue(), waveformArea);
            float lEndX = secondsToPixelX(loopEndSlider_.getValue(), waveformArea);
            if (lEndX > lStartX) {
                g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(0.15f));
                g.fillRect(lStartX, static_cast<float>(waveformArea.getY()), lEndX - lStartX,
                           static_cast<float>(waveformArea.getHeight()));

                // Top drag bar
                g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(0.5f));
                g.fillRect(lStartX, static_cast<float>(waveformArea.getY()), lEndX - lStartX,
                           static_cast<float>(kLoopBarHeight));
            }
        }

        // Sample start marker (orange vertical line)
        if (sampleLength_ > 0.0) {
            float startX = secondsToPixelX(startSlider_.getValue(), waveformArea);
            g.setColour(juce::Colour(0xFFFF9800));  // Orange
            g.drawVerticalLine(static_cast<int>(startX), static_cast<float>(waveformArea.getY()),
                               static_cast<float>(waveformArea.getBottom()));
        }

        // Sample end marker (red vertical line)
        if (sampleLength_ > 0.0) {
            float endX = secondsToPixelX(endSlider_.getValue(), waveformArea);
            g.setColour(juce::Colour(0xFFE53935));  // Red
            g.drawVerticalLine(static_cast<int>(endX), static_cast<float>(waveformArea.getY()),
                               static_cast<float>(waveformArea.getBottom()));
        }

        // Loop start/end markers (green vertical lines)
        if (loopButton_->isActive() && sampleLength_ > 0.0) {
            auto green = DarkTheme::getColour(DarkTheme::ACCENT_GREEN);

            float lStartX = secondsToPixelX(loopStartSlider_.getValue(), waveformArea);
            g.setColour(green);
            g.drawVerticalLine(static_cast<int>(lStartX), static_cast<float>(waveformArea.getY()),
                               static_cast<float>(waveformArea.getBottom()));

            float lEndX = secondsToPixelX(loopEndSlider_.getValue(), waveformArea);
            g.setColour(green);
            g.drawVerticalLine(static_cast<int>(lEndX), static_cast<float>(waveformArea.getY()),
                               static_cast<float>(waveformArea.getBottom()));
        }

        // Playhead (white vertical line)
        if (playheadPosition_ > 0.0 && sampleLength_ > 0.0) {
            float phX = secondsToPixelX(playheadPosition_, waveformArea);
            g.setColour(juce::Colours::white);
            g.drawVerticalLine(static_cast<int>(phX), static_cast<float>(waveformArea.getY()),
                               static_cast<float>(waveformArea.getBottom()));
        }

        g.restoreState();  // Restore clip region
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRect(waveformArea);
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.drawText("Drop sample or click Load", waveformArea, juce::Justification::centred);
    }

    // --- Vertical separators between control columns ---
    auto ctrlArea = getLocalBounds().reduced(4);
    ctrlArea.removeFromTop(22);  // sample name row
    auto waveBounds = getWaveformBounds();
    ctrlArea.removeFromTop(waveBounds.getHeight() + 2);  // waveform + gap

    int totalW = ctrlArea.getWidth();
    int col1W = totalW * 3 / 8;
    int col2W = totalW * 2 / 8;

    int sep1X = ctrlArea.getX() + col1W;
    int sep2X = ctrlArea.getX() + col1W + col2W;
    int sepTop = ctrlArea.getY() + 2;
    int sepBottom = ctrlArea.getBottom();
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawVerticalLine(sep1X, static_cast<float>(sepTop), static_cast<float>(sepBottom));
    g.drawVerticalLine(sep2X, static_cast<float>(sepTop), static_cast<float>(sepBottom));
}

// =============================================================================
// Layout
// =============================================================================

void SamplerUI::resized() {
    auto area = getLocalBounds().reduced(4);

    // Row 1: Sample name + Root note + Load button
    auto sampleRow = area.removeFromTop(20);
    loadButton_->setBounds(sampleRow.removeFromRight(20));
    sampleRow.removeFromRight(4);
    // Root note: label + slider on the right side of the header
    auto rootArea = sampleRow.removeFromRight(70);
    rootNoteLabel_.setBounds(rootArea.removeFromLeft(30));
    rootNoteSlider_.setBounds(rootArea);
    sampleRow.removeFromRight(4);
    sampleNameLabel_.setBounds(sampleRow);
    area.removeFromTop(2);

    // Waveform display — absorbs remaining space above controls
    // Controls: label(12) + slider(18) = 30
    static constexpr int kControlsHeight = 30;
    int waveHeight = juce::jmax(30, area.getHeight() - kControlsHeight - 2);
    area.removeFromTop(waveHeight);
    area.removeFromTop(2);

    auto controlsArea = area;

    // Split into 3 columns: Start/Loop (3/8) | Pitch (2/8) | Amp (3/8)
    int totalW = controlsArea.getWidth();
    int col1W = totalW * 3 / 8;
    int col2W = totalW * 2 / 8;
    auto col1 = controlsArea.removeFromLeft(col1W).reduced(2, 0);
    auto col2 = controlsArea.removeFromLeft(col2W).reduced(2, 0);
    auto col3 = controlsArea.reduced(2, 0);

    // --- Column 1: Start / End / Loop ---
    // Labels: START | END | (icon) L.START | L.END
    auto c1LabelRow = col1.removeFromTop(12);
    int quarterC1 = col1.getWidth() / 4;
    int iconW = 20;
    int loopSliderW = (col1.getWidth() - 2 * quarterC1 - iconW) / 2;
    startLabel_.setBounds(c1LabelRow.removeFromLeft(quarterC1));
    endLabel_.setBounds(c1LabelRow.removeFromLeft(quarterC1));
    c1LabelRow.removeFromLeft(iconW);  // loop icon space
    loopStartLabel_.setBounds(c1LabelRow.removeFromLeft(loopSliderW));
    loopEndLabel_.setBounds(c1LabelRow);

    // Sliders: [start] | [end] | [icon][lstart] | [lend]
    auto c1Row = col1.removeFromTop(18);
    startSlider_.setBounds(c1Row.removeFromLeft(quarterC1).reduced(1, 0));
    endSlider_.setBounds(c1Row.removeFromLeft(quarterC1).reduced(1, 0));
    loopButton_->setBounds(c1Row.removeFromLeft(iconW));
    loopStartSlider_.setBounds(c1Row.removeFromLeft(loopSliderW).reduced(1, 0));
    loopEndSlider_.setBounds(c1Row.reduced(1, 0));

    // --- Column 2: Pitch ---
    // Labels: PITCH | FINE
    auto c2LabelRow = col2.removeFromTop(12);
    int halfCol2 = col2.getWidth() / 2;
    pitchLabel_.setBounds(c2LabelRow.removeFromLeft(halfCol2));
    fineLabel_.setBounds(c2LabelRow);

    // Sliders: [pitch] | [fine]
    auto c2Row = col2.removeFromTop(18);
    pitchSlider_.setBounds(c2Row.removeFromLeft(halfCol2).reduced(1, 0));
    fineSlider_.setBounds(c2Row.reduced(1, 0));

    // --- Column 3: Amp ---
    // Labels: ATK | DEC | SUS | REL | LEVEL | VEL
    auto c3LabelRow = col3.removeFromTop(12);
    int sixthCol3 = col3.getWidth() / 6;
    attackLabel_.setBounds(c3LabelRow.removeFromLeft(sixthCol3));
    decayLabel_.setBounds(c3LabelRow.removeFromLeft(sixthCol3));
    sustainLabel_.setBounds(c3LabelRow.removeFromLeft(sixthCol3));
    releaseLabel_.setBounds(c3LabelRow.removeFromLeft(sixthCol3));
    levelLabel_.setBounds(c3LabelRow.removeFromLeft(sixthCol3));
    velAmountLabel_.setBounds(c3LabelRow);

    // Sliders: [atk] | [dec] | [sus] | [rel] | [level] | [vel]
    auto c3Row = col3.removeFromTop(18);
    attackSlider_.setBounds(c3Row.removeFromLeft(sixthCol3).reduced(1, 0));
    decaySlider_.setBounds(c3Row.removeFromLeft(sixthCol3).reduced(1, 0));
    sustainSlider_.setBounds(c3Row.removeFromLeft(sixthCol3).reduced(1, 0));
    releaseSlider_.setBounds(c3Row.removeFromLeft(sixthCol3).reduced(1, 0));
    levelSlider_.setBounds(c3Row.removeFromLeft(sixthCol3).reduced(1, 0));
    velAmountSlider_.setBounds(c3Row.reduced(1, 0));

    // Rebuild waveform path at new size
    if (hasWaveform_ && waveformBuffer_ != nullptr) {
        auto waveBounds = getWaveformBounds();
        // Update zoom-to-fit minimum if we're at or below it
        double minPPS = (sampleLength_ > 0.0)
                            ? static_cast<double>(waveBounds.getWidth()) / sampleLength_
                            : 100.0;
        if (pixelsPerSecond_ < minPPS)
            pixelsPerSecond_ = minPPS;
        buildWaveformPath(waveformBuffer_, waveBounds.getWidth(), waveBounds.getHeight() - 4);
    }
}

}  // namespace magda::daw::ui
