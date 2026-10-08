#include "ChainSelectorControl.hpp"

#include "core/TrackManager.hpp"
#include "modulation/MacroKnobComponent.hpp"
#include "modulation/ModKnobComponent.hpp"
#include "params/ParamLinkResolver.hpp"
#include "ui/themes/ActiveTheme.hpp"

namespace magda::daw::ui {

ChainSelectorControl::ChainSelectorControl(const magda::ChainNodePath& rackPath)
    : magda::DraggableValueLabel(Format::Integer), rackPath_(rackPath) {
    setRange(0.0, 127.0, 0.0);
    setAutomationTarget(magda::ControlTarget::rackChainSelector(rackPath_));
    setTooltip("Chain selector: each chain sounds where its FADE range covers it, and fades at "
               "the edges. Drop a macro or modifier here, or click it in link mode, to link it");
    magda::LinkModeManager::getInstance().addListener(this);
    startTimerHz(30);
}

ChainSelectorControl::~ChainSelectorControl() {
    stopTimer();
    magda::LinkModeManager::getInstance().removeListener(this);
}

void ChainSelectorControl::paintOverChildren(juce::Graphics& g) {
    // Outlined while linked, dropped on, or offered in link mode.
    if (!dragOver_ && !linkMacro_.isValid() && !linkMod_.isValid() && !modulated_)
        return;
    g.setColour(ActiveTheme::getColour(linkMod_.isValid() ? ActiveTheme::ACCENT_ATTENTION
                                                          : ActiveTheme::ACCENT_MODULATION));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.0f, 1.0f);
}

void ChainSelectorControl::mouseDown(const juce::MouseEvent& e) {
    if (linkMacro_.isValid())
        linkMacro(linkMacro_.parentPath, linkMacro_.macroIndex);
    else if (linkMod_.isValid())
        linkMod(linkMod_.parentPath, linkMod_.modIndex);
    else if (!modulated_)
        magda::DraggableValueLabel::mouseDown(e);
}

void ChainSelectorControl::mouseDrag(const juce::MouseEvent& e) {
    if (!linkMacro_.isValid() && !linkMod_.isValid() && !modulated_)
        magda::DraggableValueLabel::mouseDrag(e);
}

void ChainSelectorControl::mouseUp(const juce::MouseEvent& e) {
    if (!linkMacro_.isValid() && !linkMod_.isValid() && !modulated_)
        magda::DraggableValueLabel::mouseUp(e);
}

bool ChainSelectorControl::isInterestedInDragSource(const SourceDetails& details) {
    if (const auto* macro = dynamic_cast<MacroKnobComponent*>(details.sourceComponent.get()))
        return isInScopeOf(rackPath_, macro->getParentPath());
    if (const auto* mod = dynamic_cast<ModKnobComponent*>(details.sourceComponent.get()))
        return isInScopeOf(rackPath_, mod->getParentPath());
    return false;
}

void ChainSelectorControl::itemDragEnter(const SourceDetails&) {
    dragOver_ = true;
    repaint();
}

void ChainSelectorControl::itemDragExit(const SourceDetails&) {
    dragOver_ = false;
    repaint();
}

void ChainSelectorControl::itemDropped(const SourceDetails& details) {
    dragOver_ = false;
    repaint();
    if (const auto* macro = dynamic_cast<MacroKnobComponent*>(details.sourceComponent.get()))
        linkMacro(macro->getParentPath(), macro->getMacroIndex());
    else if (const auto* mod = dynamic_cast<ModKnobComponent*>(details.sourceComponent.get()))
        linkMod(mod->getParentPath(), mod->getModIndex());
}

void ChainSelectorControl::modLinkModeChanged(bool active, const magda::ModSelection& selection) {
    linkMod_ =
        active && isInScopeOf(rackPath_, selection.parentPath) ? selection : magda::ModSelection{};
    setMouseCursor(linkMod_.isValid() ? juce::MouseCursor::PointingHandCursor
                                      : juce::MouseCursor::NormalCursor);
    repaint();
}

void ChainSelectorControl::macroLinkModeChanged(bool active,
                                                const magda::MacroSelection& selection) {
    linkMacro_ = active && isInScopeOf(rackPath_, selection.parentPath) ? selection
                                                                        : magda::MacroSelection{};
    setMouseCursor(linkMacro_.isValid() ? juce::MouseCursor::PointingHandCursor
                                        : juce::MouseCursor::NormalCursor);
    repaint();
}

void ChainSelectorControl::timerCallback() {
    const auto value = modulatedValue();
    if (value == modulated_)
        return;
    modulated_ = value;
    // Linked, it shows what its links say and is not dragged; unlinked, the stored value.
    if (modulated_) {
        setValue(*modulated_, juce::dontSendNotification);
    } else if (const auto* rack = magda::TrackManager::getInstance().getRackByPath(rackPath_)) {
        setValue(rack->chainSelector, juce::dontSendNotification);
    }
    repaint();
    if (onModulatedValue)
        onModulatedValue(modulated_);
}

std::optional<float> ChainSelectorControl::modulatedValue() const {
    auto& tracks = magda::TrackManager::getInstance();
    const auto* track = tracks.getTrack(rackPath_.trackId);
    if (track == nullptr)
        return std::nullopt;

    // The track's own, then each rack from the outermost in to this one.
    std::vector<const magda::MacroArray*> macros{&track->macros};
    std::vector<const magda::ModArray*> mods{&track->mods};
    for (std::size_t i = 0; i < rackPath_.steps.size(); ++i) {
        if (rackPath_.steps[i].type != magda::ChainStepType::Rack)
            continue;
        auto path = rackPath_;
        path.steps.resize(i + 1);
        if (const auto* rack = tracks.getRackByPath(path)) {
            macros.push_back(&rack->macros);
            mods.push_back(&rack->mods);
        }
    }

    const auto target = magda::ControlTarget::rackChainSelector(rackPath_);
    const auto linked = [&target](const auto* scope) {
        return std::ranges::any_of(*scope, [&target](const auto& s) { return s.getLink(target); });
    };
    if (std::ranges::none_of(macros, linked) && std::ranges::none_of(mods, linked))
        return std::nullopt;

    // Linked, the selector is what its links say: no base under them.
    return juce::jlimit(0.0f, 1.0f, totalLinkOffset(target, macros, mods)) * 127.0f;
}

void ChainSelectorControl::linkMacro(const magda::ChainNodePath& owner, int macroIndex) {
    magda::TrackManager::getInstance().setMacroTarget(
        owner, macroIndex, magda::ControlTarget::rackChainSelector(rackPath_));
}

// Full depth, so the modifier sweeps the whole selector.
void ChainSelectorControl::linkMod(const magda::ChainNodePath& owner, int modIndex) {
    magda::TrackManager::getInstance().setModLinkAmount(
        owner, modIndex, magda::ControlTarget::rackChainSelector(rackPath_), 1.0f);
}

}  // namespace magda::daw::ui
