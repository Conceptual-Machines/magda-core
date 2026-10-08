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
    for (const auto& well : wells_)
        paintWell(g, well);
    for (int x : dividers_)
        paintDivider(g, x, getHeight());
    g.setColour(ActiveTheme::getBorderColour());
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
}

void ArrangementToolbar::resized() {
    wells_.clear();
    dividers_.clear();
    const int y = getHeight() / 2 - kButton / 2;

    tools_->setBounds(getWidth() / 2 - EditToolButtons::kWidth / 2, y - kWellPad,
                      EditToolButtons::kWidth, EditToolButtons::kHeight);

    int x = getWidth() - 8 - kWellPad;
    for (auto group = viewGroups_.rbegin(); group != viewGroups_.rend(); ++group) {
        const int right = x;
        for (auto button = group->rbegin(); button != group->rend(); ++button) {
            x -= kButton;
            (*button)->setBounds(x, y, kButton, kButton);
            x -= kGap;
        }
        x += kGap;
        wells_.emplace_back(x - kWellPad, y - kWellPad, right - x + kWellPad * 2,
                            kButton + kWellPad * 2);
        x -= kWellPad + 8;
        if (std::next(group) != viewGroups_.rend()) {
            dividers_.push_back(x);
            x -= 8 + kWellPad;
        }
    }
    repaint();
}

}  // namespace magda
