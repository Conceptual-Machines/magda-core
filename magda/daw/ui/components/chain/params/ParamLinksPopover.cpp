#include "params/ParamLinksPopover.hpp"

#include "core/TrackManager.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

/// The rack whose macros and modifiers a device inside it sees: the last rack on its path.
magda::ChainNodePath enclosingRack(const magda::ChainNodePath& devicePath) {
    auto path = devicePath;
    while (!path.steps.empty() && path.steps.back().type != magda::ChainStepType::Rack)
        path.steps.pop_back();
    return path;
}

template <typename Scope> magda::ChainNodePath ownerPath(Scope scope, const ParamLinkContext& ctx) {
    if (scope == Scope::Track)
        return magda::ChainNodePath::trackLevel(ctx.devicePath.trackId);
    if (scope == Scope::Rack)
        return enclosingRack(ctx.devicePath);
    return ctx.devicePath;
}

}  // namespace

/** @brief One link: its chip, amount, polarity and remove. */
struct ParamLinksPopover::Row : juce::Component {
    juce::String chip;
    bool isMacro = false;
    magda::ChainNodePath owner;
    int index = -1;
    magda::ControlTarget target;
    float amount = 0.0f;
    bool bipolar = false;

    magda::DraggableValueLabel amountLabel{magda::DraggableValueLabel::Format::Percentage};
    juce::TextButton polarity;
    juce::TextButton remove{juce::String::fromUTF8("\xc3\x97")};

    Row() {
        amountLabel.setRange(-1.0, 1.0, 0.0);
        amountLabel.onValueChange = [this]() {
            amount = static_cast<float>(amountLabel.getValue());
            if (auto* popover = getParentComponent())
                popover->repaint();
            auto& tracks = magda::TrackManager::getInstance();
            if (isMacro)
                tracks.setMacroLinkAmount(owner, index, target, amount);
            else
                tracks.setModLinkAmount(owner, index, target, amount);
        };
        addAndMakeVisible(amountLabel);

        polarity.setClickingTogglesState(true);
        polarity.onClick = [this]() {
            bipolar = polarity.getToggleState();
            polarity.setButtonText(bipolar ? "BI" : "UNI");
            auto& tracks = magda::TrackManager::getInstance();
            if (isMacro)
                tracks.setMacroLinkBipolar(owner, index, target, bipolar);
            else
                tracks.setModLinkBipolar(owner, index, target, bipolar);
        };
        addAndMakeVisible(polarity);

        remove.setTooltip("Remove this link");
        addAndMakeVisible(remove);
    }

    void sync() {
        amountLabel.setValue(amount, juce::dontSendNotification);
        polarity.setToggleState(bipolar, juce::dontSendNotification);
        polarity.setButtonText(bipolar ? "BI" : "UNI");
    }

    void paint(juce::Graphics& g) override {
        const auto colour = ActiveTheme::getColour(isMacro ? ActiveTheme::ACCENT_MODULATION
                                                           : ActiveTheme::ACCENT_ATTENTION);
        const auto box = getLocalBounds().removeFromLeft(46).reduced(0, 4).toFloat();
        g.setColour(colour.withAlpha(0.18f));
        g.fillRoundedRectangle(box, 3.0f);
        g.setColour(colour);
        g.setFont(FontManager::getInstance().getMonoFont(9.5f));
        g.drawText(chip, box, juce::Justification::centred, false);
    }

    void resized() override {
        auto area = getLocalBounds();
        area.removeFromLeft(52);
        remove.setBounds(area.removeFromRight(20).reduced(0, 3));
        area.removeFromRight(4);
        polarity.setBounds(area.removeFromRight(34).reduced(0, 3));
        area.removeFromRight(6);
        amountLabel.setBounds(area.reduced(0, 3));
    }
};

ParamLinksPopover::ParamLinksPopover(juce::String paramName,
                                     std::function<ParamLinkContext()> context,
                                     std::function<float()> baseValue)
    : paramName_(std::move(paramName)),
      context_(std::move(context)),
      baseValue_(std::move(baseValue)) {
    setWantsKeyboardFocus(true);
    addMouseListener(this, true);  // leaving through a row counts as leaving
    refresh();
}

ParamLinksPopover::~ParamLinksPopover() {
    removeMouseListener(this);
}

bool ParamLinksPopover::refresh() {
    auto ctx = context_();
    ctx.selectedModIndex = -1;
    ctx.selectedMacroIndex = -1;
    const auto target = magda::ControlTarget::pluginParam(ctx.devicePath, ctx.paramIndex);
    const auto chips = linkChips(ctx);
    const auto macros = getLinkedMacros(ctx);
    const auto mods = getLinkedMods(ctx);

    // One row per link, in the chips' order: macros first.
    std::vector<std::unique_ptr<Row>> rows;
    std::size_t chip = 0;
    for (const auto& macro : macros) {
        auto row = std::make_unique<Row>();
        row->chip = chips[chip++].text;
        row->isMacro = true;
        row->owner = ownerPath(macro.scope, ctx);
        row->index = macro.macroIndex;
        row->amount = macro.link.amount;
        row->bipolar = macro.link.bipolar;
        rows.push_back(std::move(row));
    }
    for (const auto& mod : mods) {
        auto row = std::make_unique<Row>();
        row->chip = chips[chip++].text;
        row->owner = ownerPath(mod.scope, ctx);
        row->index = mod.modIndex;
        row->amount = mod.link.amount;
        row->bipolar = mod.link.bipolar;
        rows.push_back(std::move(row));
    }

    for (auto& row : rows) {
        row->target = target;
        row->sync();
        auto* raw = row.get();
        auto safeThis = juce::Component::SafePointer<ParamLinksPopover>(this);
        row->remove.onClick = [safeThis, raw]() {
            const auto owner = raw->owner;
            const auto index = raw->index;
            const auto target = raw->target;
            const bool macro = raw->isMacro;
            // Async: the removal refreshes this popover, which destroys the row clicked.
            juce::MessageManager::callAsync([safeThis, owner, index, target, macro]() {
                auto& tracks = magda::TrackManager::getInstance();
                if (macro)
                    tracks.removeMacroLink(owner, index, target);
                else
                    tracks.removeModLink(owner, index, target);
                if (safeThis != nullptr && !safeThis->refresh() && safeThis->onDismiss)
                    safeThis->onDismiss();
            });
        };
        addAndMakeVisible(*row);
    }
    rows_ = std::move(rows);
    setSize(WIDTH, getPreferredHeight());
    resized();
    repaint();
    return !rows_.empty();
}

int ParamLinksPopover::getPreferredHeight() const {
    return TITLE_HEIGHT + RANGE_HEIGHT + static_cast<int>(rows_.size()) * ROW_HEIGHT + 10;
}

void ParamLinksPopover::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_PANEL));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FRAME_BORDER));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

    auto area = getLocalBounds().reduced(10, 4);
    auto& fonts = FontManager::getInstance();
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_TEXT));
    g.setFont(fonts.getUIFontMedium(12.0f));
    g.drawText(paramName_, area.removeFromTop(TITLE_HEIGHT), juce::Justification::centredLeft,
               true);

    // The range: where the links can take the parameter from its base, macros over modifiers.
    const auto track = area.removeFromTop(RANGE_HEIGHT).reduced(0, 4).toFloat();
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_WELL));
    g.fillRoundedRectangle(track, 3.0f);
    const float base = juce::jlimit(0.0f, 1.0f, baseValue_());
    for (const bool macros : {true, false}) {
        float low = 0.0f;
        float high = 0.0f;
        for (const auto& row : rows_) {
            if (row->isMacro != macros)
                continue;
            if (row->bipolar) {
                low -= std::abs(row->amount);
                high += std::abs(row->amount);
            } else {
                (row->amount < 0.0f ? low : high) += row->amount;
            }
        }
        if (low == 0.0f && high == 0.0f)
            continue;
        const float from = juce::jlimit(0.0f, 1.0f, base + low);
        const float to = juce::jlimit(0.0f, 1.0f, base + high);
        auto lane = macros ? track.withHeight(track.getHeight() / 2.0f)
                           : track.withTrimmedTop(track.getHeight() / 2.0f);
        g.setColour(ActiveTheme::getColour(macros ? ActiveTheme::ACCENT_MODULATION
                                                  : ActiveTheme::ACCENT_ATTENTION)
                        .withAlpha(0.7f));
        g.fillRect(lane.withX(track.getX() + track.getWidth() * from)
                       .withWidth(juce::jmax(1.0f, track.getWidth() * (to - from))));
    }
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_VALUE_TEXT));
    g.fillRect(juce::Rectangle<float>(track.getX() + track.getWidth() * base - 1.0f,
                                      track.getY() - 2.0f, 2.0f, track.getHeight() + 4.0f));
}

void ParamLinksPopover::resized() {
    auto area = getLocalBounds().reduced(10, 4);
    area.removeFromTop(TITLE_HEIGHT + RANGE_HEIGHT);
    for (auto& row : rows_)
        row->setBounds(area.removeFromTop(ROW_HEIGHT));
}

void ParamLinksPopover::mouseExit(const juce::MouseEvent&) {
    if (!isMouseOver(true) && !juce::ModifierKeys::currentModifiers.isAltDown() && onDismiss)
        onDismiss();
}

bool ParamLinksPopover::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey && onDismiss) {
        onDismiss();
        return true;
    }
    return false;
}

}  // namespace magda::daw::ui
