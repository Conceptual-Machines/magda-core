#include "custom_ui/SamplerUI.hpp"

#include <BinaryData.h>

#include <algorithm>
#include <cmath>

#include "DisplayListGraphics.hpp"
#include "core/GestureRouter.hpp"
#include "ui/components/common/InternalFileDrag.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/CursorManager.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SdkColourRoles.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"
#include "ui/utils/AudioFileTypes.hpp"

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
    sampleNameLabel_.setColour(juce::Label::textColourId, ActiveTheme::getSecondaryTextColour());
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
    loopButton_ =
        std::make_unique<magda::SvgButton>("Loop", BinaryData::loop_svg, BinaryData::loop_svgSize);
    loopButton_->setIconPadding(0.0f);
    loopButton_->setStateColourReplacement(
        juce::Colour(0xFF1A1A1A), ActiveTheme::PIANO_ROLL_BACKGROUND, ActiveTheme::ACCENT_PRIMARY);
    loopButton_->setStateColourReplacement(juce::Colour(0xFFBCBCBC), ActiveTheme::ICON_TRANSPORT,
                                           ActiveTheme::TEXT_BRIGHT);
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
        waveformView_.setGain(waveformGain_);
        if (hasWaveform_)
            repaint();
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
                       ActiveTheme::getColour(ActiveTheme::BACKGROUND).brighter(0.10f));
        btn->setColour(juce::TextButton::buttonOnColourId,
                       ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY));
        btn->setColour(juce::TextButton::textColourOffId, ActiveTheme::getSecondaryTextColour());
        btn->setColour(juce::TextButton::textColourOnId,
                       ActiveTheme::getColour(ActiveTheme::TEXT_BRIGHT));
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
    label.setColour(juce::Label::textColourId, ActiveTheme::getSecondaryTextColour());
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

void SamplerUI::lookAndFeelChanged() {
    // Re-apply cached theme colours after a live theme switch.
    sampleNameLabel_.setColour(juce::Label::textColourId,
                               hasSampleName_ ? ActiveTheme::getTextColour()
                                              : ActiveTheme::getSecondaryTextColour());

    for (auto& btn : voiceModeButtons_) {
        if (!btn)
            continue;
        btn->setColour(juce::TextButton::buttonColourId,
                       ActiveTheme::getColour(ActiveTheme::BACKGROUND).brighter(0.10f));
        btn->setColour(juce::TextButton::buttonOnColourId,
                       ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY));
        btn->setColour(juce::TextButton::textColourOffId, ActiveTheme::getSecondaryTextColour());
        btn->setColour(juce::TextButton::textColourOnId,
                       ActiveTheme::getColour(ActiveTheme::TEXT_BRIGHT));
    }

    for (auto* label :
         {&rootNoteLabel_, &startLabel_, &endLabel_, &loopStartLabel_, &loopEndLabel_,
          &attackLabel_, &decayLabel_, &sustainLabel_, &releaseLabel_, &pitchLabel_, &fineLabel_,
          &levelLabel_, &velAmountLabel_, &voiceModeLabel_, &glideLabel_})
        label->setColour(juce::Label::textColourId, ActiveTheme::getSecondaryTextColour());

    repaint();
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
    waveformView_.setGain(waveformGain_);
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

    hasSampleName_ = sampleName.isNotEmpty();
    if (hasSampleName_) {
        sampleNameLabel_.setText(sampleName, juce::dontSendNotification);
        sampleNameLabel_.setColour(juce::Label::textColourId, ActiveTheme::getTextColour());
    } else {
        sampleNameLabel_.setText("No sample loaded", juce::dontSendNotification);
        sampleNameLabel_.setColour(juce::Label::textColourId,
                                   ActiveTheme::getSecondaryTextColour());
    }
}

void SamplerUI::setWaveformData(const juce::AudioBuffer<float>* buffer, double /*sampleRate*/,
                                double sampleLengthSeconds) {
    sampleLength_ = sampleLengthSeconds;

    if (buffer == nullptr || buffer->getNumSamples() == 0) {
        hasWaveform_ = false;
        waveformView_.setSource(nullptr, 0.0);
        waveformSource_.reset();
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

    // The view draws channel 0 and fits the whole sample to the pane.
    waveformSource_ = std::make_unique<sdk::BufferWaveformSource>(buffer->getReadPointer(0),
                                                                  buffer->getNumSamples());
    const auto waveArea = getWaveformBounds();
    waveformView_.setSize(waveArea.getWidth(), waveArea.getHeight());
    waveformView_.setSource(waveformSource_.get(), sampleLength_);

    if (!isTimerRunning())
        startTimerHz(30);

    repaint();
}

bool SamplerUI::isInterestedInFileDrag(const juce::StringArray& files) {
    return std::ranges::any_of(files, isAudioFile);
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

// Samples dragged from MAGDA's own browser come through as an internal
// {type:"files"} drag rather than an OS file drag, so route them back into the
// FileDragAndDropTarget handlers above. See InternalFileDrag.hpp.
bool SamplerUI::isInterestedInDragSource(const SourceDetails& details) {
    return magda::dnd::acceptsFilesDrag(*this, details);
}

void SamplerUI::itemDragEnter(const SourceDetails& details) {
    magda::dnd::forwardFilesDragEnter(*this, details);
}

void SamplerUI::itemDragMove(const SourceDetails& details) {
    magda::dnd::forwardFilesDragMove(*this, details);
}

void SamplerUI::itemDragExit(const SourceDetails& details) {
    magda::dnd::forwardFilesDragExit(*this, details);
}

void SamplerUI::itemDropped(const SourceDetails& details) {
    magda::dnd::forwardFilesDrop(*this, details);
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
    const auto a = static_cast<float>(attackSlider_.getValue());
    const auto d = static_cast<float>(decaySlider_.getValue());
    const auto s = static_cast<float>(sustainSlider_.getValue());
    const auto r = static_cast<float>(releaseSlider_.getValue());
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

void SamplerUI::syncWaveformMarkers() {
    waveformView_.setMarkers({startSlider_.getValue(), endSlider_.getValue(),
                              loopButton_->isActive(), loopStartSlider_.getValue(),
                              loopEndSlider_.getValue()});
}

void SamplerUI::applyWaveformMarkers(const sdk::WaveformResponse& response) {
    if (response.markersChanged) {
        const auto& m = waveformView_.markers();
        const auto push = [](LinkableTextSlider& slider, double value) {
            if (slider.getValue() != value)
                slider.setValue(value, juce::sendNotificationSync);
        };
        push(startSlider_, m.start);
        push(endSlider_, m.end);
        push(loopStartSlider_, m.loopStart);
        push(loopEndSlider_, m.loopEnd);
        // The sliders round to their interval; the view follows what they kept.
        syncWaveformMarkers();
    }
    if (response.repaint || response.markersChanged)
        repaint();
}

sdk::WaveformPointer SamplerUI::waveformPointer(const juce::MouseEvent& e) const {
    const auto local = e.getPosition() - getWaveformBounds().getPosition();
    return {static_cast<float>(local.x), static_cast<float>(local.y), e.mods.isShiftDown(),
            e.mods.isCommandDown(),      e.mods.isAltDown(),          e.mods.isMiddleButtonDown()};
}

void SamplerUI::updateWaveformCursor(const juce::MouseEvent& e) {
    if (!getWaveformBounds().contains(e.getPosition()) || !hasWaveform_) {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    syncWaveformMarkers();
    switch (waveformView_.cursor(waveformPointer(e))) {
        case sdk::WaveformCursor::ResizeLeftRight:
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            break;
        case sdk::WaveformCursor::DraggingHand:
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            break;
        case sdk::WaveformCursor::Zoom:
            setMouseCursor(CursorManager::getInstance().getZoomCursor());
            break;
        case sdk::WaveformCursor::ZoomIn:
            setMouseCursor(CursorManager::getInstance().getZoomInCursor());
            break;
        case sdk::WaveformCursor::ZoomOut:
            setMouseCursor(CursorManager::getInstance().getZoomOutCursor());
            break;
        case sdk::WaveformCursor::Normal:
            setMouseCursor(juce::MouseCursor::NormalCursor);
            break;
    }
}

// =============================================================================
// Mouse Interaction on Waveform
// =============================================================================

void SamplerUI::mouseDown(const juce::MouseEvent& e) {
    if (!getWaveformBounds().contains(e.getPosition()) || !hasWaveform_) {
        waveformGesture_ = false;
        Component::mouseDown(e);
        return;
    }
    waveformGesture_ = true;
    syncWaveformMarkers();
    applyWaveformMarkers(waveformView_.pointerDown(waveformPointer(e)));
    if (e.mods.isCommandDown())
        setMouseCursor(CursorManager::getInstance().getZoomCursor());
}

void SamplerUI::mouseDrag(const juce::MouseEvent& e) {
    if (!waveformGesture_ || !hasWaveform_) {
        Component::mouseDrag(e);
        return;
    }
    applyWaveformMarkers(waveformView_.pointerDrag(waveformPointer(e)));
    if (e.mods.isCommandDown())
        updateWaveformCursor(e);
}

void SamplerUI::mouseUp(const juce::MouseEvent& e) {
    if (waveformGesture_)
        waveformView_.pointerUp(waveformPointer(e));
    waveformGesture_ = false;
    updateWaveformCursor(e);
}

void SamplerUI::mouseMove(const juce::MouseEvent& e) {
    updateWaveformCursor(e);
}

void SamplerUI::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (!getWaveformBounds().contains(e.getPosition()) || !hasWaveform_ || sampleLength_ <= 0.0) {
        Component::mouseWheelMove(e, wheel);
        return;
    }

    const auto gesture = magda::GestureRouter::getInstance().resolve(
        magda::GestureContext::Sampler, wheel, e.mods, e.getPosition());
    if (gesture.type != magda::GestureActionType::ZoomHorizontal) {
        Component::mouseWheelMove(e, wheel);
        return;
    }

    applyWaveformMarkers(
        waveformView_.zoomBy(1.0 + static_cast<double>(gesture.magnitude), waveformPointer(e).x));
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
    g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
    g.setColour(ActiveTheme::getColour(ActiveTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds().reduced(1));

    // Waveform area
    auto waveformArea = getWaveformBounds();

    if (hasWaveform_ && !waveformArea.isEmpty()) {
        syncWaveformMarkers();
        waveformView_.setPlayhead(sampleLength_ > 0.0 ? playheadPosition_ : 0.0);
        waveformView_.render(waveformList_);
        g.saveState();
        g.setOrigin(waveformArea.getPosition());
        sdk::juce_host::drawDisplayList(g, waveformList_, sdkRoleColour);
        g.restoreState();
    } else {
        g.setColour(ActiveTheme::getColour(ActiveTheme::SURFACE));
        g.fillRect(waveformArea);
        g.setColour(ActiveTheme::getSecondaryTextColour());
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.drawText("Drop sample or click Load", waveformArea, juce::Justification::centred);
    }

    // --- Divider between the left (waveform) and right (synth) columns ---
    auto body = getLocalBounds().reduced(4);
    body.removeFromTop(kNameRowH + 2);
    g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER));
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

    // The view keeps the whole sample in reach at the new width.
    const auto waveBounds = getWaveformBounds();
    waveformView_.setSize(waveBounds.getWidth(), waveBounds.getHeight());
}

}  // namespace magda::daw::ui
