#include "custom_ui/SamplerUI.hpp"

#include <BinaryData.h>

#include <cmath>

#include "ui/themes/CursorManager.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kNameRowH = 20;         // top sample-name / root / load row
constexpr int kCtrlRowH = 30;         // one control row: label(12) + control(18)
constexpr int kCtrlBlockH = 64;       // two control rows (bottom-aligned in each column)
constexpr int kRightColPercent = 38;  // right (synth) column width as % of body
}  // namespace

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
    rootNoteSlider_.setShowFillIndicator(false);
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

    // --- ADSR graph (its own time axis, decoupled from the waveform) ---
    addAndMakeVisible(envGraph_);
    envGraph_.onStageChanged = [this](int paramIndex, float v) {
        // Drive the matching value box, which forwards the write to the plugin.
        switch (paramIndex) {
            case 0:
                attackSlider_.setValue(v, juce::sendNotificationSync);
                break;
            case 1:
                decaySlider_.setValue(v, juce::sendNotificationSync);
                break;
            case 2:
                sustainSlider_.setValue(v, juce::sendNotificationSync);
                break;
            case 3:
                releaseSlider_.setValue(v, juce::sendNotificationSync);
                break;
            default:
                break;
        }
    };
    // Keep the graph in lockstep when a value box is edited directly (overrides the
    // generic onValueChanged installed by setupTimeSlider for the ADSR sliders).
    attackSlider_.onValueChanged = [this](double v) {
        if (onParameterChanged)
            onParameterChanged(0, static_cast<float>(v));
        envGraph_.setStageValue(AdsrGraph::Attack, static_cast<float>(v));
    };
    decaySlider_.onValueChanged = [this](double v) {
        if (onParameterChanged)
            onParameterChanged(1, static_cast<float>(v));
        envGraph_.setStageValue(AdsrGraph::Decay, static_cast<float>(v));
    };
    sustainSlider_.onValueChanged = [this](double v) {
        if (onParameterChanged)
            onParameterChanged(2, static_cast<float>(v));
        envGraph_.setStageValue(AdsrGraph::Sustain, static_cast<float>(v));
    };
    releaseSlider_.onValueChanged = [this](double v) {
        if (onParameterChanged)
            onParameterChanged(3, static_cast<float>(v));
        envGraph_.setStageValue(AdsrGraph::Release, static_cast<float>(v));
    };
    syncEnvGraph();

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

    // --- Glide (param 13): portamento time ---
    glideSlider_.setRange(0.0, 2000.0, 1.0);
    glideSlider_.setValue(0.0, juce::dontSendNotification);
    glideSlider_.setValueFormatter([](double v) {
        return v < 1000.0 ? juce::String(static_cast<int>(v)) + " ms"
                          : juce::String(v / 1000.0, 2) + " s";
    });
    glideSlider_.setValueParser([](const juce::String& text) {
        juce::String t = text.trim();
        if (t.endsWithIgnoreCase("ms"))
            return static_cast<double>(t.dropLastCharacters(2).trim().getFloatValue());
        if (t.endsWithIgnoreCase("s"))
            return static_cast<double>(t.dropLastCharacters(1).trim().getFloatValue()) * 1000.0;
        return t.getDoubleValue();
    });
    glideSlider_.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(13, static_cast<float>(value));
    };
    glideSlider_.setParamIndex(13);
    addAndMakeVisible(glideSlider_);

    // --- Voice Mode (param 12): segmented Poly/Mono/Legato over a hidden slider ---
    voiceModeSlider_.setRange(0.0, 2.0, 1.0);
    voiceModeSlider_.setValue(0.0, juce::dontSendNotification);
    voiceModeSlider_.setParamIndex(12);
    voiceModeSlider_.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(12, static_cast<float>(value));
    };
    addChildComponent(voiceModeSlider_);  // hidden carrier; buttons drive it
    static const char* kModeNames[3] = {"Poly", "Mono", "Legato"};
    for (int m = 0; m < 3; ++m) {
        auto btn = std::make_unique<juce::TextButton>(kModeNames[m]);
        btn->setLookAndFeel(&FlatTabButtonLookAndFeel::getInstance());
        btn->setClickingTogglesState(false);
        btn->setColour(juce::TextButton::buttonColourId,
                       DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.10f));
        btn->setColour(juce::TextButton::buttonOnColourId,
                       DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        btn->setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
        btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        btn->setConnectedEdges((m > 0 ? juce::Button::ConnectedOnLeft : 0) |
                               (m < 2 ? juce::Button::ConnectedOnRight : 0));
        btn->onClick = [this, m]() { setVoiceMode(m); };
        addAndMakeVisible(*btn);
        voiceModeButtons_[static_cast<size_t>(m)] = std::move(btn);
    }
    updateVoiceModeButtons();

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
    setupLabel(voiceModeLabel_, "VOICE");
    setupLabel(glideLabel_, "GLIDE");
}

void SamplerUI::setVoiceMode(int mode) {
    voiceMode_ = juce::jlimit(0, 2, mode);
    voiceModeSlider_.setValue(voiceMode_, juce::dontSendNotification);
    if (onParameterChanged)
        onParameterChanged(12, static_cast<float>(voiceMode_));
    updateVoiceModeButtons();
}

void SamplerUI::updateVoiceModeButtons() {
    for (int m = 0; m < 3; ++m)
        if (voiceModeButtons_[static_cast<size_t>(m)])
            voiceModeButtons_[static_cast<size_t>(m)]->setToggleState(m == voiceMode_,
                                                                      juce::dontSendNotification);
}

SamplerUI::~SamplerUI() {
    stopTimer();
    for (auto& btn : voiceModeButtons_)
        if (btn)
            btn->setLookAndFeel(nullptr);
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
                                 float velAmount, const juce::String& sampleName, int rootNote,
                                 float voiceMode, float glide) {
    attackSlider_.setValue(attack, juce::dontSendNotification);
    decaySlider_.setValue(decay, juce::dontSendNotification);
    sustainSlider_.setValue(sustain, juce::dontSendNotification);
    releaseSlider_.setValue(release, juce::dontSendNotification);
    syncEnvGraph();  // mirror the live ADSR values into the graph
    pitchSlider_.setValue(pitch, juce::dontSendNotification);
    fineSlider_.setValue(fine, juce::dontSendNotification);
    levelSlider_.setValue(level, juce::dontSendNotification);
    waveformGain_ = juce::Decibels::decibelsToGain(level);
    velAmountSlider_.setValue(velAmount, juce::dontSendNotification);

    glideSlider_.setValue(glide, juce::dontSendNotification);
    voiceMode_ = juce::jlimit(0, 2, static_cast<int>(std::lround(voiceMode)));
    voiceModeSlider_.setValue(voiceMode_, juce::dontSendNotification);
    updateVoiceModeButtons();

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
    // Left column: the waveform sits above that column's 2-row control block.
    auto area = getLocalBounds().reduced(4);
    area.removeFromTop(kNameRowH + 2);
    const int rightColW = juce::jmax(280, area.getWidth() * kRightColPercent / 100);
    area.removeFromRight(rightColW + 4);  // right (synth) column + gap -> left column
    area.removeFromBottom(kCtrlBlockH);   // left column's control block
    return area;
}

void SamplerUI::syncEnvGraph() {
    auto timeInfo = [](float mn, float mx, int idx, float val) {
        magda::ParameterInfo i;
        i.minValue = mn;
        i.maxValue = mx;
        i.scale = magda::ParameterScale::Logarithmic;  // usable spread for short times
        i.unit = "s";
        i.defaultValue = mn;
        i.currentValue = val;
        i.paramIndex = idx;
        return i;
    };
    const float a = static_cast<float>(attackSlider_.getValue());
    const float d = static_cast<float>(decaySlider_.getValue());
    const float s = static_cast<float>(sustainSlider_.getValue());
    const float r = static_cast<float>(releaseSlider_.getValue());
    envGraph_.setStage(AdsrGraph::Attack, 0, timeInfo(0.001f, 5.0f, 0, a), a);
    envGraph_.setStage(AdsrGraph::Decay, 1, timeInfo(0.001f, 5.0f, 1, d), d);
    magda::ParameterInfo si;
    si.minValue = 0.0f;
    si.maxValue = 1.0f;
    si.scale = magda::ParameterScale::Linear;
    si.currentValue = s;
    si.paramIndex = 2;
    envGraph_.setStage(AdsrGraph::Sustain, 2, si, s);
    envGraph_.setStage(AdsrGraph::Release, 3, timeInfo(0.001f, 10.0f, 3, r), r);
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

    // Set marker position immediately
    {
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
    //                    7=sampleStart, 8=sampleEnd, 9=loopStart, 10=loopEnd, 11=velAmount,
    //                    12=voiceMode, 13=glide
    return {&attackSlider_,    &decaySlider_,     &sustainSlider_, &releaseSlider_,
            &pitchSlider_,     &fineSlider_,      &levelSlider_,   &startSlider_,
            &endSlider_,       &loopStartSlider_, &loopEndSlider_, &velAmountSlider_,
            &voiceModeSlider_, &glideSlider_};
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

        // (ADSR is shown in its own dedicated graph below the waveform, not as an
        // overlay — the amp envelope's time axis is unrelated to sample position,
        // especially for looped samples.)

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

    // --- Divider between the left (waveform) and right (synth) columns ---
    auto body = getLocalBounds().reduced(4);
    body.removeFromTop(kNameRowH + 2);
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    auto wave = getWaveformBounds();
    g.drawVerticalLine(wave.getRight() + 2, static_cast<float>(body.getY()),
                       static_cast<float>(body.getBottom()));
}

// =============================================================================
// Layout
// =============================================================================

void SamplerUI::resized() {
    auto area = getLocalBounds().reduced(4);

    // Row 1: Sample name + Root note + Load button (full width).
    auto sampleRow = area.removeFromTop(kNameRowH);
    loadButton_->setBounds(sampleRow.removeFromRight(20));
    sampleRow.removeFromRight(4);
    auto rootArea = sampleRow.removeFromRight(70);
    rootNoteLabel_.setBounds(rootArea.removeFromLeft(30));
    rootNoteSlider_.setBounds(rootArea);
    sampleRow.removeFromRight(4);
    sampleNameLabel_.setBounds(sampleRow);
    area.removeFromTop(2);

    // Two columns: LEFT (waveform + sample controls), RIGHT (ADSR graph + synth
    // controls). Each is the viz on top with a 2-row control block bottom-aligned,
    // so both viz areas share the same top and bottom.
    const int rightColW = juce::jmax(280, area.getWidth() * kRightColPercent / 100);
    auto rightCol = area.removeFromRight(rightColW);
    area.removeFromRight(4);  // gap
    auto leftCol = area;      // waveform column (waveform rect = getWaveformBounds)

    auto layoutCell = [](juce::Rectangle<int> cell, juce::Label& label,
                         LinkableTextSlider& slider) {
        label.setBounds(cell.removeFromTop(12));
        slider.setBounds(cell.removeFromTop(18).reduced(1, 0));
    };

    // --- LEFT column control block (waveform fills above it) ---
    auto leftCtrl = leftCol.removeFromBottom(kCtrlBlockH);
    {
        // Row 1: START | END | (icon) L.START | L.END
        auto row1 = leftCtrl.removeFromTop(kCtrlRowH);
        leftCtrl.removeFromTop(2);
        auto row2 = leftCtrl.removeFromTop(kCtrlRowH);
        int quarter = row1.getWidth() / 4;
        int iconW = 20;
        int loopW = (row1.getWidth() - 2 * quarter - iconW) / 2;
        auto r1lab = row1.removeFromTop(12);
        startLabel_.setBounds(r1lab.removeFromLeft(quarter));
        endLabel_.setBounds(r1lab.removeFromLeft(quarter));
        r1lab.removeFromLeft(iconW);
        loopStartLabel_.setBounds(r1lab.removeFromLeft(loopW));
        loopEndLabel_.setBounds(r1lab);
        startSlider_.setBounds(row1.removeFromLeft(quarter).reduced(1, 0));
        endSlider_.setBounds(row1.removeFromLeft(quarter).reduced(1, 0));
        loopButton_->setBounds(row1.removeFromLeft(iconW));
        loopStartSlider_.setBounds(row1.removeFromLeft(loopW).reduced(1, 0));
        loopEndSlider_.setBounds(row1.reduced(1, 0));

        // Row 2: PITCH | FINE | VOICE (segmented) | GLIDE
        const int unit = row2.getWidth() / 5;
        auto r2lab = row2.removeFromTop(12);
        pitchLabel_.setBounds(r2lab.removeFromLeft(unit));
        fineLabel_.setBounds(r2lab.removeFromLeft(unit));
        voiceModeLabel_.setBounds(r2lab.removeFromLeft(unit * 2));
        glideLabel_.setBounds(r2lab);
        pitchSlider_.setBounds(row2.removeFromLeft(unit).reduced(1, 0));
        fineSlider_.setBounds(row2.removeFromLeft(unit).reduced(1, 0));
        auto voiceRow = row2.removeFromLeft(unit * 2).reduced(1, 0);
        int btnW = voiceRow.getWidth() / 3;
        for (int m = 0; m < 3; ++m) {
            auto cell = (m == 2) ? voiceRow : voiceRow.removeFromLeft(btnW);
            if (voiceModeButtons_[static_cast<size_t>(m)])
                voiceModeButtons_[static_cast<size_t>(m)]->setBounds(cell);
        }
        glideSlider_.setBounds(row2.reduced(1, 0));
    }

    // --- RIGHT column: ADSR graph (top) + 2-row control block ---
    auto rightCtrl = rightCol.removeFromBottom(kCtrlBlockH);
    rightCol.removeFromBottom(2);
    envGraph_.setBounds(rightCol);
    {
        // Row 1: ATK | DEC | SUS | REL
        auto row1 = rightCtrl.removeFromTop(kCtrlRowH);
        rightCtrl.removeFromTop(2);
        auto row2 = rightCtrl.removeFromTop(kCtrlRowH);
        int q = row1.getWidth() / 4;
        layoutCell(row1.removeFromLeft(q), attackLabel_, attackSlider_);
        layoutCell(row1.removeFromLeft(q), decayLabel_, decaySlider_);
        layoutCell(row1.removeFromLeft(q), sustainLabel_, sustainSlider_);
        layoutCell(row1, releaseLabel_, releaseSlider_);

        // Row 2: LEVEL | VEL
        int half = row2.getWidth() / 2;
        auto r2lab = row2.removeFromTop(12);
        levelLabel_.setBounds(r2lab.removeFromLeft(half));
        velAmountLabel_.setBounds(r2lab);
        levelSlider_.setBounds(row2.removeFromLeft(half).reduced(1, 0));
        velAmountSlider_.setBounds(row2.reduced(1, 0));
    }

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
