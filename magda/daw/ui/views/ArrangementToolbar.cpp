#include "ArrangementToolbar.hpp"

#include "../components/common/EditToolbar.hpp"
#include "../components/common/SvgButton.hpp"
#include "../themes/ActiveTheme.hpp"

namespace magda {

using namespace edit_toolbar;

ArrangementToolbar::ArrangementToolbar() {
    tools_ = std::make_unique<EditToolButtons>(
        EditToolState::arrangement(), std::array<const char*, 5>{
                                          "Pointer (1)",
                                          "Pencil: draw a MIDI clip on an empty lane (2)",
                                          "Slice: split a clip where you click (3)",
                                          "Glue: join a clip to the next one on its track (4)",
                                          "Erase: click or sweep over clips (5)",
                                      });
    addAndMakeVisible(*tools_);
}

ArrangementToolbar::~ArrangementToolbar() = default;

void ArrangementToolbar::setViewGroups(std::vector<std::vector<SvgButton*>> groups) {
    viewGroups_ = std::move(groups);
    for (auto& group : viewGroups_)
        for (auto* button : group) {
            styleButton(*button);
            addAndMakeVisible(*button);
        }
    resized();
}

void ArrangementToolbar::paint(juce::Graphics& g) {
    g.fillAll(ActiveTheme::getColour(ActiveTheme::MIDI_TOOLBAR));
    decor_.paint(g, getHeight());
    g.setColour(ActiveTheme::getBorderColour());
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
}

void ArrangementToolbar::resized() {
    decor_.clear();
    Row row(decor_, getLocalBounds(), getWidth() - 8, Row::From::Right);
    for (auto group = viewGroups_.rbegin(); group != viewGroups_.rend(); ++group) {
        if (group != viewGroups_.rbegin())
            row.divider();
        row.openWell();
        for (auto button = group->rbegin(); button != group->rend(); ++button)
            row.button(**button);
        row.closeWell();
    }
    // Centred, but slid left of the view groups when the toolbar is narrow.
    const int viewLeft = row.position() - 16;
    const int toolsX = juce::jmax(8, juce::jmin(getWidth() / 2 - EditToolButtons::kWidth / 2,
                                                viewLeft - EditToolButtons::kWidth));
    tools_->setBounds(toolsX, getHeight() / 2 - EditToolButtons::kHeight / 2,
                      EditToolButtons::kWidth, EditToolButtons::kHeight);
    repaint();
}

}  // namespace magda
