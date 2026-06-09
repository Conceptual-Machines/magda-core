#include "MixAnalysisModal.hpp"

#include "../../themes/DarkTheme.hpp"

namespace magda {

namespace {

// Render the measured Input as a compact monospace summary: a per-track levels
// table + the detected frequency collisions. This is the pre-agent data.
juce::String formatFindings(const MixAnalysisAgent::Input& in) {
    auto col = [](juce::String s, int w) { return s.paddedRight(' ', w); };

    juce::String out;
    out << "LEVELS (" << static_cast<int>(in.tracks.size()) << " tracks)\n";
    out << col("NAME", 16) << col("LUFS", 7) << col("PEAK", 7) << col("PLR", 6) << col("COR", 6)
        << "WID\n";
    auto line = [&](const juce::String& name, const MixAnalysisAgent::TrackMix& t) {
        out << col(name.substring(0, 15), 16) << col(juce::String(t.integratedLufs, 1), 7)
            << col(juce::String(t.samplePeakDb, 1), 7) << col(juce::String(t.plr, 1), 6)
            << col(juce::String(t.correlation, 2), 6) << juce::String(t.width, 2) << "\n";
    };
    for (const auto& t : in.tracks)
        line(juce::String(t.name), t);
    if (in.master)
        line("[MASTER]", *in.master);

    out << "\nCOLLISIONS (" << static_cast<int>(in.masking.size()) << ")\n";
    if (in.masking.empty()) {
        out << "  none detected\n";
    } else {
        for (const auto& m : in.masking) {
            out << "  " << col(juce::String(m.a) + " vs " + juce::String(m.b), 26)
                << juce::String(juce::roundToInt(m.loHz)) << "-"
                << juce::String(juce::roundToInt(m.hiHz)) << " Hz   " << juce::String(m.severity, 2)
                << "\n";
        }
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Spinner
// ---------------------------------------------------------------------------

MixAnalysisModal::Spinner::Spinner() {
    startTimerHz(30);
}
MixAnalysisModal::Spinner::~Spinner() {
    stopTimer();
}
void MixAnalysisModal::Spinner::timerCallback() {
    angle += 0.18f;
    if (angle > juce::MathConstants<float>::twoPi)
        angle -= juce::MathConstants<float>::twoPi;
    repaint();
}
void MixAnalysisModal::Spinner::paint(juce::Graphics& g) {
    const auto b = getLocalBounds().toFloat();
    const float sz = juce::jmin(b.getWidth(), b.getHeight()) - 4.0f;
    const float cx = b.getCentreX();
    const float cy = b.getCentreY();
    juce::Path arc;
    arc.addCentredArc(cx, cy, sz * 0.5f, sz * 0.5f, 0.0f, angle,
                      angle + juce::MathConstants<float>::pi * 1.5f, true);
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_CYAN).withAlpha(0.85f));
    g.strokePath(arc, juce::PathStrokeType(2.0f));
}

// ---------------------------------------------------------------------------
// MixAnalysisModal
// ---------------------------------------------------------------------------

MixAnalysisModal::MixAnalysisModal(MixAnalysisService::Mode mode) : mode_(mode) {
    setSize(480, 380);

    const char* modeName =
        mode == MixAnalysisService::Mode::Live ? "Live capture" : "Offline render";
    titleLabel_.setText(juce::String("Mix analysis  -  ") + modeName, juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(15.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    addAndMakeVisible(titleLabel_);

    statusLabel_.setJustificationType(juce::Justification::centred);
    statusLabel_.setColour(juce::Label::textColourId,
                           DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    addAndMakeVisible(statusLabel_);

    findings_.setMultiLine(true);
    findings_.setReadOnly(true);
    findings_.setScrollbarsShown(true);
    findings_.setCaretVisible(false);
    findings_.setFont(
        juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    findings_.setColour(juce::TextEditor::backgroundColourId,
                        DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    findings_.setColour(juce::TextEditor::textColourId,
                        DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    findings_.setColour(juce::TextEditor::outlineColourId, DarkTheme::getBorderColour());
    addChildComponent(findings_);

    addChildComponent(spinner_);

    primaryButton_.onClick = [this]() {
        auto& svc = MixAnalysisService::getInstance();
        switch (state_) {
            case State::Loading:
                svc.cancel();
                break;
            case State::Listening:
                svc.stopLiveCapture();
                break;
            case State::Results:
            case State::Error:
                // Re-run this mode.
                if (mode_ == MixAnalysisService::Mode::Live)
                    svc.startLiveCapture();
                else
                    svc.runOffline();
                break;
        }
    };
    addAndMakeVisible(primaryButton_);

    MixAnalysisService::getInstance().addListener(this);

    // Show cached findings instantly; otherwise kick off the run for this mode.
    auto& svc = MixAnalysisService::getInstance();
    const bool haveCached = svc.cached(mode_).has_value();
    const bool busyOrCapturing = svc.isBusy() || svc.isCapturing();
    if (!haveCached && !busyOrCapturing) {
        if (mode_ == MixAnalysisService::Mode::Live)
            svc.startLiveCapture();
        else
            svc.runOffline();
    }
    refresh();
}

MixAnalysisModal::~MixAnalysisModal() {
    MixAnalysisService::getInstance().removeListener(this);
}

MixAnalysisModal::State MixAnalysisModal::deriveState() const {
    auto& svc = MixAnalysisService::getInstance();
    if (mode_ == MixAnalysisService::Mode::Live && svc.isCapturing())
        return State::Listening;
    if (svc.isBusy())
        return State::Loading;
    if (svc.cached(mode_).has_value())
        return State::Results;
    if (svc.lastError().isNotEmpty())
        return State::Error;
    return State::Loading;
}

void MixAnalysisModal::mixAnalysisChanged() {
    refresh();
}

void MixAnalysisModal::renderResults() {
    if (auto cached = MixAnalysisService::getInstance().cached(mode_))
        findings_.setText(formatFindings(*cached), juce::dontSendNotification);
}

void MixAnalysisModal::refresh() {
    state_ = deriveState();
    auto& svc = MixAnalysisService::getInstance();

    const bool results = state_ == State::Results;
    const bool loading = state_ == State::Loading;
    findings_.setVisible(results);
    spinner_.setVisible(loading);

    switch (state_) {
        case State::Loading:
            statusLabel_.setText(svc.progressText().isNotEmpty() ? svc.progressText()
                                                                 : juce::String("Analyzing mix..."),
                                 juce::dontSendNotification);
            primaryButton_.setButtonText("Stop");
            break;
        case State::Listening:
            statusLabel_.setText("Listening... play the mix, then Stop.",
                                 juce::dontSendNotification);
            primaryButton_.setButtonText("Stop & show");
            break;
        case State::Results:
            statusLabel_.setText({}, juce::dontSendNotification);
            primaryButton_.setButtonText("Re-run");
            renderResults();
            break;
        case State::Error:
            statusLabel_.setText(svc.lastError(), juce::dontSendNotification);
            primaryButton_.setButtonText("Retry");
            break;
    }
    resized();
    repaint();
}

void MixAnalysisModal::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getPanelBackgroundColour());
}

void MixAnalysisModal::resized() {
    auto b = getLocalBounds().reduced(12);
    titleLabel_.setBounds(b.removeFromTop(22));
    b.removeFromTop(6);

    auto footer = b.removeFromBottom(28);
    primaryButton_.setBounds(footer.removeFromRight(110));

    if (state_ == State::Results) {
        findings_.setBounds(b);
    } else if (state_ == State::Loading) {
        spinner_.setBounds(b.withSizeKeepingCentre(40, 40).translated(0, -16));
        statusLabel_.setBounds(b.removeFromBottom(40));
    } else {
        statusLabel_.setBounds(b);
    }
}

}  // namespace magda
