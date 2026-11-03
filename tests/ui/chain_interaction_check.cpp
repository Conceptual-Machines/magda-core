// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

// Full component regression check. Run with run_chain_interaction_check.py.
#include <array>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <typeinfo>

#include "audio/AudioThumbnailManager.hpp"
#include "audio/plugins/FaustPlugin.hpp"
#include "audio/plugins/compiled/MagdaFilterCompiledPlugin.hpp"
#include "core/ClipManager.hpp"
#include "core/Config.hpp"
#include "core/ModulatorEngine.hpp"
#include "core/TrackCommands.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "ui/components/chain/DeviceSlotComponent.hpp"
#include "ui/components/chain/RackComponent.hpp"
#include "ui/components/chain/compiled/CompiledEqBandLayout.hpp"
#include "ui/components/chain/compiled/CompiledFilterCurveView.hpp"
#include "ui/components/chain/compiled/CompiledMultibandBandLayout.hpp"
#include "ui/components/chain/custom_ui/FaustMeterPanel.hpp"
#include "ui/components/chain/custom_ui/FaustUI.hpp"
#include "ui/components/chain/custom_ui/SamplerUI.hpp"
#include "ui/components/chain/layout/CompiledFaustDeviceLayout.hpp"
#include "ui/components/chain/layout/FaceplateBelowGeometry.hpp"
#include "ui/components/chain/layout/FaustDeviceLayout.hpp"
#include "ui/components/chain/layout/StandardDeviceLayout.hpp"
#include "ui/components/chain/params/ParamControlStyle.hpp"
#include "ui/components/chain/slot/DevicePresetMenu.hpp"
#include "ui/components/chain/slot/DeviceSlotContentLayout.hpp"
#include "ui/components/chain/slot/DeviceSlotSelectionHandling.hpp"
#include "ui/components/chain/slot/ExternalInsertLayout.hpp"
#include "ui/components/common/MidiActivityLed.hpp"
#include "ui/components/common/SegmentedChoice.hpp"
#include "ui/components/mixer/LevelMeter.hpp"
#include "ui/panels/content/TrackChainContent.hpp"
#include "ui/state/ExpandedFaceplate.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/views/ExpandedFaceplateHost.hpp"
#include "ui/views/SessionView.hpp"

using namespace magda;
using namespace magda::daw::ui;
namespace {
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void click(juce::Component& node, float y = 8, int modifiers = 0, int count = 1) {
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent event(
        juce::Desktop::getInstance().getMainMouseSource(), {50, y},
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | modifiers), 1, 0, 0, 0, 0,
        &node, &node, now, {50, y}, now, count, false);
    node.mouseDown(event);
    node.mouseUp(event);
}
juce::Button& button(juce::Component& node, const juce::String& name) {
    for (auto* child : node.getChildren())
        if (auto* candidate = dynamic_cast<juce::Button*>(child))
            if (candidate->getComponentID() == name || candidate->getButtonText() == name)
                return *candidate;
    throw std::runtime_error("missing explicit layout button");
}
void press(juce::Button& target) {
    check(target.isVisible() && !target.getBounds().isEmpty(), "layout button is not reachable");
    check(target.getParentComponent()->getComponentAt(target.getBounds().getCentre()) == &target,
          "layout button is obscured by another component");
    if (target.getClickingTogglesState())
        target.setToggleState(!target.getToggleState(), juce::dontSendNotification);
    target.onClick();
}
SamplerUI* findSamplerUI(juce::Component& root) {
    for (auto* child : root.getChildren()) {
        if (auto* sampler = dynamic_cast<SamplerUI*>(child))
            return sampler;
        if (auto* nested = findSamplerUI(*child))
            return nested;
    }
    return nullptr;
}
void unchanged(NodeComponent& node) {
    check(!node.isCollapsed(), "selection collapsed the node");
    check(!node.isParamPanelVisible(), "selection opened the controller panel");
    check(!node.isModPanelVisible(), "selection opened the modulation panel");
}
}  // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    // Singleton timers must stop before JUCE tears down its message manager.
    struct Shutdown {
        ~Shutdown() {
            ModulatorEngine::getInstance().shutdown();
            ClipManager::getInstance().shutdown();
            AudioThumbnailManager::getInstance().shutdown();
            FontManager::getInstance().shutdown();
            // Stop JUCE timer producers, then release their queued messages before
            // ScopedJuceInitialiser_GUI destroys the message manager.
            juce::DeletedAtShutdown::deleteAll();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        }
    } shutdown;
    try {
        auto& tracks = TrackManager::getInstance();
        auto& selection = SelectionManager::getInstance();
        const auto track = tracks.createTrack("Chain interaction regression");
        DeviceInfo model;
        model.name = "Test plugin";
        model.format = PluginFormat::VST3;
        model.loadState = DeviceLoadState::Loaded;
        const auto firstId = tracks.addDeviceToTrack(track, model);
        const auto secondId = tracks.addDeviceToTrack(track, model);
        const auto firstPath = ChainNodePath::topLevelDevice(track, firstId);
        const auto secondPath = ChainNodePath::topLevelDevice(track, secondId);
        const auto rackId = tracks.addRackToTrack(track, "Test rack");
        {
            DeviceSlotComponent first(*tracks.getDeviceInChainByPath(firstPath));
            DeviceSlotComponent second(*tracks.getDeviceInChainByPath(secondPath));
            first.setNodePath(firstPath);
            second.setNodePath(secondPath);
            RackComponent rack(track, *tracks.getRack(track, rackId));
            rack.setSize(0, 0);
            rack.setSize(rack.getPreferredWidth(), rack.getPreferredHeight());
            for (auto* child : rack.getChildren())
                if (child->getName() == "Power")
                    check(child->isVisible() && !child->getBounds().isEmpty(),
                          "rack power lost after an empty layout");
            for (auto* node :
                 {static_cast<NodeComponent*>(&first), static_cast<NodeComponent*>(&second),
                  static_cast<NodeComponent*>(&rack)}) {
                node->setVisible(true);
                node->setSize(500, 350);
            }

            for (auto* node :
                 {static_cast<NodeComponent*>(&first), static_cast<NodeComponent*>(&second),
                  static_cast<NodeComponent*>(&rack), static_cast<NodeComponent*>(&first)}) {
                click(*node);
                check(node->isSelected(), "header failed to select the node");
                unchanged(*node);
                click(*node);
                unchanged(*node);
                click(*node, 100);
                unchanged(*node);
                selection.selectChainNode(node->getNodePath());
                unchanged(*node);
            }

            selection.selectChainNode(firstPath);
            click(second, 8, 0, 2);
            unchanged(second);  // A double-click event cannot collapse an unselected node.
            selection.selectChainNode(firstPath);
            const auto eventTime = juce::Time::getCurrentTime();
            auto& child = button(first, "Macro");
            const juce::MouseEvent childDouble(
                juce::Desktop::getInstance().getMainMouseSource(), {5, 5},
                juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0, &child,
                &child, eventTime, {5, 5}, eventTime, 2, false);
            static_cast<juce::Component&>(first).mouseDown(childDouble);
            first.mouseUp(childDouble);
            unchanged(first);  // Forwarded child double-clicks never fold the device.
            click(second, 8, juce::ModifierKeys::commandModifier);
            unchanged(second);
            click(rack, 8, juce::ModifierKeys::shiftModifier);
            unchanged(rack);

            for (auto* node :
                 {static_cast<NodeComponent*>(&first), static_cast<NodeComponent*>(&rack)}) {
                press(button(*node, "Macro"));
                check(node->isParamPanelVisible(), "explicit controller button did not open panel");
                selection.selectChainNode(secondPath);
                selection.selectChainNode(node->getNodePath());
                check(node->isParamPanelVisible(), "selection changed the open panel state");
                press(button(*node, "Macro"));
                unchanged(*node);
                press(button(*node, "Mod"));
                check(node->isModPanelVisible(), "explicit modulation button did not open panel");
                selection.selectChainNode(secondPath);
                selection.selectChainNode(node->getNodePath());
                check(node->isModPanelVisible(), "selection changed the open modulation panel");
                press(button(*node, "Mod"));
                unchanged(*node);
                click(*node, 8, juce::ModifierKeys::shiftModifier, 2);
                unchanged(*node);
                selection.selectChainNode(node->getNodePath());
                const auto selected = selection.getSelectedChainNode();
                click(*node, 8, 0, 2);
                check(node->isCollapsed(), "double-click did not collapse node");
                check(selection.getSelectedChainNode() == selected,
                      "double-click changed selection");
                click(*node);
                check(node->isCollapsed(), "selection expanded collapsed node");
                check(!node->isParamPanelVisible(), "collapsed selection opened panel");
                click(*node, 8, 0, 2);
                unchanged(*node);
            }
            // Parameter identity must survive offset indices, custom ordering and pages.
            ParamHostComponent grid(std::make_unique<StandardDeviceLayout>());
            DeviceInfo parameterModel;
            for (int index = 2; index < 42; ++index) {
                ParameterInfo parameter;
                parameter.paramIndex = index;
                parameter.name = "Parameter " + juce::String(index);
                parameterModel.parameters.push_back(parameter);
            }
            auto selectCell = [&](int cell) {
                const int parameter = grid.getSlot(cell)->getParamIndex();
                applyDeviceSlotParamSelectionChange(firstPath, {firstPath, parameter}, grid, {});
                for (int i = 0; i < grid.getSlotCount(); ++i)
                    check(grid.getSlot(i)->isSelected() == (i == cell),
                          "parameter selection highlighted the wrong grid cell");
            };
            grid.updateParameterSlots(parameterModel, 0, {});
            selectCell(0);  // Parameter 2 is cell 0, not cell 2.
            selectCell(3);
            parameterModel.visibleParameters = {8, 2, 6, 4};
            grid.updateParameterSlots(parameterModel, 0, {});
            selectCell(0);
            selectCell(1);
            applyDeviceSlotParamSelectionChange(firstPath, {secondPath, 2}, grid, {});
            for (int i = 0; i < grid.getSlotCount(); ++i)
                check(!grid.getSlot(i)->isSelected(), "selection leaked between devices");
            parameterModel.visibleParameters.clear();
            grid.updateParameterSlots(parameterModel, 1, {});
            selectCell(0);
            selectCell(7);
            applyDeviceSlotParamSelectionChange(firstPath, {}, grid, {});
            for (int i = 0; i < grid.getSlotCount(); ++i)
                check(!grid.getSlot(i)->isSelected(), "cleared selection left a cell highlighted");

            // As many rows as the body holds, up to 4 in Text and 3 in Knobs (#3201).
            const bool knobs = resolveControlStyle({}) == ParamControlStyle::Knobs;
            const auto pagesCoverEveryParameter = [&] {
                return grid.getLayout().totalPages(parameterModel) * grid.getSlotCount() >=
                       static_cast<int>(parameterModel.parameters.size());
            };
            const auto layOutAt = [&grid](int height) {
                grid.setSize(560, height);
                const juce::Font font{juce::FontOptions{}};
                grid.layoutContent(font, font);
            };
            layOutAt(320);
            check(grid.getRowCount() == (knobs ? 3 : 4), "tall plug-in grid lost a row");
            check(pagesCoverEveryParameter(), "full plug-in grid pages missed parameters");
            layOutAt(168);
            check(grid.getRowCount() == (knobs ? 2 : 3),
                  "200px device grid kept rows it can't fit");
            check(pagesCoverEveryParameter(), "short plug-in grid pages missed parameters");
            layOutAt(320);
            check(grid.getRowCount() == (knobs ? 3 : 4),
                  "plug-in grid did not return to full rows");
        }
        {
            // A faceplate reads its links afresh: a chain rebuild frees the arrays a
            // kept context named, and the drop that causes it updates the faceplate.
            const auto filterPath = ChainNodePath::topLevelDevice(track, 1000);
            DeviceInfo filter;
            filter.id = 1000;
            filter.pluginId = magda::daw::audio::compiled::MagdaFilterCompiledPlugin::xmlTypeName;
            auto mods = std::make_unique<ModArray>(1);
            (*mods)[0].links.push_back({ControlTarget::pluginParam(filterPath, 0), 0.5f});
            int resolved = 0;
            CompiledFilterCurveView faceplate(filter.pluginId);
            faceplate.setLinkContextProvider([&]() {
                ++resolved;
                ParamLinkContext context;
                context.deviceId = filter.id;
                context.devicePath = filterPath;
                context.deviceMods = mods.get();
                return std::optional{context};
            });
            faceplate.updateFromDevice(filter);
            faceplate.updateFromDevice(filter);
            mods = std::make_unique<ModArray>(*mods);  // The rebuild: old storage freed.
            const int before = resolved;
            faceplate.updateFromDevice(filter);
            check(resolved > before, "the faceplate read its links without resolving them");
        }
        {
            // The Filter faceplate is a handle: across sets cutoff, up sets resonance,
            // scroll sets drive.
            using Filter = magda::daw::audio::compiled::MagdaFilterCompiledPlugin;
            CompiledFilterCurveView faceplate(Filter::xmlTypeName);
            faceplate.setSize(600, 240);
            std::map<int, float> written;
            faceplate.setOnParameterChanged([&](int slot, float value) { written[slot] = value; });
            bool takesClicks = false;
            bool childrenTakeClicks = false;
            faceplate.getInterceptsMouseClicks(takesClicks, childrenTakeClicks);
            check(takesClicks, "an interactive faceplate ignores the mouse");
            const auto event = [&](juce::Point<float> at, juce::Point<float> downAt) {
                return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, {},
                                        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &faceplate, &faceplate,
                                        juce::Time::getCurrentTime(), downAt,
                                        juce::Time::getCurrentTime(), 1, false);
            };
            const juce::Point<float> start{200.0f, 150.0f};
            faceplate.mouseDown(event(start, start));
            const float lowCutoff = written[Filter::kCutoffSlot];
            const float startResonance = written[Filter::kResonanceSlot];
            faceplate.mouseDrag(event({400.0f, 90.0f}, start));
            faceplate.mouseUp(event({400.0f, 90.0f}, start));
            check(written[Filter::kCutoffSlot] > lowCutoff, "dragging right did not raise cutoff");
            check(written[Filter::kResonanceSlot] > startResonance,
                  "dragging up did not raise resonance");
            juce::MouseWheelDetails wheel{};
            wheel.deltaY = 0.5f;
            faceplate.mouseWheelMove(event(start, start), wheel);
            check(written[Filter::kDriveSlot] > 0.0f, "scrolling up did not raise drive");
        }
        {
            // Multiband: one band's ten knobs a page, then the whole-device five.
            using MB = magda::daw::audio::compiled::MagdaMultibandCompiledPlugin;
            const CompiledMultibandBandLayout layout;
            DeviceInfo model;
            for (int index = 0; index < 38; ++index) {
                ParameterInfo parameter;
                parameter.paramIndex = index;
                model.parameters.push_back(parameter);
            }
            const auto slotOf = [&](int cell, int page) {
                return model
                    .parameters[static_cast<size_t>(
                        layout.cellFor(model, cell, page).paramArrayIndex)]
                    .paramIndex;
            };
            check(slotOf(0, 1) == MB::kMidUpperThresholdSlot && slotOf(9, 2) == MB::kHighGainSlot &&
                      slotOf(10, 0) == MB::kAmountSlot && slotOf(14, 2) == MB::kOutputSlot,
                  "a multiband page does not show its band's knobs, then the device's");
            const juce::Rectangle<int> body(0, 0, 1040, 300);
            const auto geometry = MultibandEditorGeometry::of(body);
            for (int cell = 0; cell < layout.cellCount(); ++cell) {
                const auto knob = layout.cellBounds(cell, body, true);
                check((geometry.knobs.contains(knob) || geometry.globals.contains(knob)) &&
                          !knob.intersects(geometry.plot) && !knob.intersects(geometry.tabs),
                      "a multiband knob left the editor and the device column");
            }
        }
        {
            // EQ: one band at a time; each page is a band, and its knobs sit in the editor.
            using Eq = magda::daw::audio::compiled::MagdaEqCompiledPlugin;
            const CompiledEqBandLayout layout;
            DeviceInfo model;
            for (int index = 0; index < Eq::kHostSlotCount; ++index) {
                ParameterInfo parameter;
                parameter.paramIndex = index;
                model.parameters.push_back(parameter);
            }
            const auto slotOf = [&](int cell, int page) {
                return model
                    .parameters[static_cast<size_t>(
                        layout.cellFor(model, cell, page).paramArrayIndex)]
                    .paramIndex;
            };
            check(slotOf(0, 4) == Eq::bandSlot(4, Eq::kBandFreqOffset) &&
                      slotOf(1, 4) == Eq::bandSlot(4, Eq::kBandGainOffset) &&
                      slotOf(2, 4) == Eq::bandSlot(4, Eq::kBandQOffset) &&
                      slotOf(3, 4) == Eq::kOutputSlot,
                  "an EQ page does not show its band's freq, gain and Q, then Output");
            const juce::Rectangle<int> body(0, 0, 900, 420);
            const auto geometry = EqBandEditorGeometry::of(body);
            for (int cell = 0; cell < 4; ++cell) {
                const auto knob = layout.cellBounds(cell, body, true);
                check(geometry.editor.contains(knob) && !knob.intersects(geometry.plot) &&
                          !knob.intersects(geometry.chips) && !knob.intersects(geometry.types) &&
                          !knob.intersects(geometry.slope),
                      "an EQ knob left the band editor");
            }
        }
        {
            // External Insert at the chain's 172 px body: its rows stand clear of each other, and
            // the knobs fit in every control style (#3212).
            const juce::Rectangle<int> body(0, 0, 574, 172);
            const auto geometry = ExternalInsertGeometry::of(body);
            check(geometry.routing.getBottom() < geometry.ccHeader.getY() &&
                      geometry.ccHeader.getBottom() <= geometry.cells[0].getY() &&
                      geometry.cells[0].getBottom() <= geometry.status.getY() &&
                      body.contains(geometry.routing) && body.contains(geometry.status),
                  "the External Insert's rows overlap or leave a 172 px body");
            for (const auto style :
                 {ParamControlStyle::Knobs, ParamControlStyle::Sliders, ParamControlStyle::Text})
                check(geometry.cells[0].getHeight() >= ParamSlotComponent::minimumHeight(style),
                      "an External Insert knob is under its style's minimum at 172 px");
            check(body.contains(geometry.cells.back()),
                  "the External Insert's knob row leaves the body");
            check(ExternalInsertGeometry::pickerWidth(geometry.routing.getWidth(), true) >= 100,
                  "the External Instrument's routing pickers are too narrow for a port name");
        }
        {
            // An External Insert's slot follows its edits and undo, on the page it is showing
            // (#3212).
            DeviceInfo insertModel;
            insertModel.name = "External FX";
            insertModel.pluginId = "insert";
            insertModel.format = PluginFormat::Internal;
            insertModel.loadState = DeviceLoadState::Loaded;
            const auto insertPath =
                ChainNodePath::topLevelDevice(track, tracks.addDeviceToTrack(track, insertModel));
            DeviceSlotComponent slot(*tracks.getDeviceInChainByPath(insertPath));
            slot.setNodePath(insertPath);
            slot.setSize(slot.getPreferredWidth(), 200);

            std::function<int(juce::Component&)> shownKnobs = [&shownKnobs](juce::Component& node) {
                int shown = dynamic_cast<ParamSlotComponent*>(&node) != nullptr && node.isVisible();
                for (auto* child : node.getChildren())
                    shown += shownKnobs(*child);
                return shown;
            };
            std::function<juce::Button*(juce::Component&)> nextPage =
                [&nextPage](juce::Component& node) -> juce::Button* {
                for (auto* child : node.getChildren()) {
                    if (child->getName() == "Next page" &&
                        dynamic_cast<juce::ArrowButton*>(child) != nullptr)
                        return static_cast<juce::Button*>(child);
                    if (auto* found = nextPage(*child))
                        return found;
                }
                return nullptr;
            };
            const auto edit = [&insertPath](std::function<void(DeviceInfo&)> change) {
                UndoManager::getInstance().executeCommand(
                    std::make_unique<EditInsertCommand>(insertPath, "Edit", std::move(change)));
            };

            check(shownKnobs(slot) == 0, "a new External Insert shows a knob");
            edit([](DeviceInfo& device) { insert_cc::addKnob(device); });
            check(shownKnobs(slot) == 1, "an added CC knob did not reach the slot");
            UndoManager::getInstance().undo();
            check(shownKnobs(slot) == 0, "an undone CC knob stayed in the slot");

            edit([](DeviceInfo& device) {
                for (int knob = 0; knob < insert_cc::kKnobsPerPage + 1; ++knob)
                    insert_cc::addKnob(device);
            });
            check(shownKnobs(slot) == insert_cc::kKnobsPerPage,
                  "a full page of knobs did not show");
            auto* next = nextPage(slot);
            check(next != nullptr && next->onClick != nullptr, "two pages of knobs have no arrow");
            next->onClick();
            check(shownKnobs(slot) == 1, "the arrow did not turn to the second page of knobs");
            edit([](DeviceInfo& device) {
                insert_cc::setKnobCc(device, device.insert.ccSlots.back().paramIndex, 74);
            });
            check(shownKnobs(slot) == 1, "editing a knob on the second page left that page");
            UndoManager::getInstance().undo();
            UndoManager::getInstance().undo();
            check(shownKnobs(slot) == 0, "undoing every knob left one in the slot");
        }
        {
            // Multiband: whole-device knobs in a left column, band knobs above the faceplate.
            static constexpr int knobs[] = {0, 1, 2, 3, 4, 6, 9, 7, 10, 8, 11};
            const CompiledFaustDeviceLayout layout(12, 3, false, nullptr, knobs, {}, 0, 5);
            const juce::Rectangle<int> content(0, 0, 504, 300);
            const auto faceplate = FaceplateBelowGeometry::of(content, true, 1).faceplate;
            for (int cell = 0; cell < 5; ++cell)
                check(layout.cellBounds(cell, content, true).getRight() <=
                          FaceplateBelowGeometry::kColumnWidth,
                      "a whole-device knob left the left column");
            for (int cell = 5; cell < 11; ++cell) {
                const auto bounds = layout.cellBounds(cell, content, true);
                check(bounds.getBottom() <= faceplate.getY() && !bounds.intersects(faceplate),
                      "a band knob overlaps the faceplate");
            }
            for (int cell = 5; cell < 10; ++cell)
                check(layout.cellBounds(cell, content, true).getRight() <=
                          layout.cellBounds(cell + 1, content, true).getX(),
                      "the band knobs are not one row, low to high");
        }
        {
            // The Compressor's four choices fit across its faceplate, Autogain as one toggle.
            int strip = 0;
            for (const auto& options :
                 {juce::StringArray{"Clean", "Glue"}, juce::StringArray{"Peak", "RMS"},
                  juce::StringArray{"Pre", "Post"}, juce::StringArray{"Autogain"}})
                strip += SegmentedChoice::preferredWidthFor(options) + 8;
            check(strip - 8 <= 420 - 14, "the Compressor's choices overrun its faceplate");
        }
        {
            // A Faust patch's view stands beside the grid in the 172px body (#3213).
            DeviceInfo patch;
            for (int index = 0; index < 3; ++index) {
                ParameterInfo parameter;
                parameter.paramIndex = index;
                parameter.name = "Control " + juce::String(index);
                patch.parameters.push_back(parameter);
            }
            ParamHostComponent grid(std::make_unique<FaustDeviceLayout>());
            grid.updateParameterSlots(patch, 0, {});
            juce::Component view, meters, errorStrip;
            DeviceSlotTraits traits;
            traits.isFaust = true;
            const juce::Rectangle<int> body(0, 0, 750, 172);
            const auto layOut = [&](bool gridShown, bool viewShown, int meterHeight) {
                layoutDeviceSlotContentBody(body, traits, false, false,
                                            {.faustHeader = &errorStrip,
                                             .faustCustomView = &view,
                                             .faustCustomViewWidth = 176,
                                             .faustCustomViewShown = viewShown,
                                             .faustMeterPanel = &meters,
                                             .faustMeterPanelPreferredHeight = meterHeight,
                                             .paramGridShown = gridShown,
                                             .paramGrid = &grid},
                                            0);
            };
            const int minimumRow = ParamSlotComponent::minimumHeight(resolveControlStyle({}));

            layOut(true, true, 0);
            check(view.isVisible() && view.getHeight() == 148, "the view lost the body's height");
            check(grid.getRight() <= view.getX(), "the view overlaps the grid");
            check(grid.getSlot(0)->getHeight() >= minimumRow, "the view squeezed the grid's rows");

            layOut(true, true, FaustMeterPanel::kPreferredHeight);
            check(meters.isVisible() && meters.getRight() <= view.getX(),
                  "the meters overlap the view");
            check(grid.getSlot(0)->getHeight() >= minimumRow,
                  "the meters squeezed the grid's rows");

            layOut(false, true, FaustMeterPanel::kPreferredHeight);
            check(!grid.isVisible() && !meters.isVisible(), "the hidden grid left its column");
            check(view.getWidth() == body.getWidth() - 28, "the view alone doesn't take the body");

            layOut(true, false, 0);
            check(!view.isVisible() && grid.getWidth() == body.getWidth(),
                  "the hidden view kept its column");

            // Beside a view the grid narrows to its controls and hides the cells it left.
            check(grid.fitColumnsToControls(patch, true), "the grid didn't fit its controls");
            check(grid.getCellsPerRow() == 2 && grid.getSlotCount() == 4,
                  "three controls are not a 2 x 2");
            check(!grid.getSlot(4)->isVisible(), "a cell outside the fitted grid stayed visible");
            check(grid.fitColumnsToControls(patch, false) && grid.getSlotCount() == 16,
                  "the grid didn't go back to its full width");
        }
        {
            // A failed load shows in the strip, and the next good one clears it.
            FaustUI faust;
            check(faust.errorHeight() == 0, "the error strip shows with no error");
            faust.setError("line 3: syntax error");
            check(faust.errorHeight() == FaustUI::kErrorHeight, "the error strip stayed away");
            faust.setError({});
            check(faust.errorHeight() == 0, "the error strip didn't clear");

            // Patches load from the preset box.
            juce::PopupMenu menu;
            faust.addPatchItems(menu);
            juce::StringArray items;
            for (juce::PopupMenu::MenuItemIterator item(menu, true); item.next();)
                items.add(item.getItem().text);
            check(items.contains("Patches"), "the preset box lost its Patches submenu");
            check(items.contains("Load .dsp File..."), "the preset box can't load a .dsp file");
            check(items.contains("Save .dsp File..."), "the preset box can't save a .dsp file");
        }
        {
            // The track chain's header folds to a strip and comes back with its controls.
            selection.selectTrack(track);
            TrackChainContent chain;
            int foldChanges = 0;
            chain.onHeaderFoldChanged = [&foldChanges]() { ++foldChanges; };
            juce::Component bar;
            bar.setVisible(true);
            bar.setSize(900, 28);
            chain.populateHeader(bar);
            chain.layoutHeader(bar.getLocalBounds());
            const auto laidOutControls = [&bar]() {
                int count = 0;
                for (auto* child : bar.getChildren())
                    if (child->getName() != "Fold Header" && child->isVisible() &&
                        !child->getBounds().isEmpty())
                        ++count;
                return count;
            };
            juce::Button* fold = nullptr;
            for (auto* child : bar.getChildren())
                if (child->getName() == "Fold Header")
                    fold = dynamic_cast<juce::Button*>(child);
            check(fold != nullptr, "the track chain header has no fold button");
            check(chain.preferredHeaderHeight() == 0, "the header starts folded");
            const int controls = laidOutControls();
            check(controls > 0, "the header has no controls to fold");

            // The controls fade out at full height, and only then does the header shrink.
            press(*fold);
            check(chain.preferredHeaderHeight() == 0, "the header shrank before the fade");
            check(foldChanges == 0, "the panel laid out before the fade");
            juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
            check(chain.preferredHeaderHeight() == TrackChainContent::FOLDED_HEADER_HEIGHT,
                  "the header didn't fold");
            check(foldChanges == 1, "folding didn't tell the panel to lay out");
            check(Config::getInstance().getTrackChainHeaderFolded(), "the fold wasn't remembered");
            bar.setSize(900, TrackChainContent::FOLDED_HEADER_HEIGHT);
            chain.layoutHeader(bar.getLocalBounds());
            // Only the MIDI light stays, above the strip's button so it isn't painted over.
            check(laidOutControls() == 1, "a control stayed on the folded strip");
            const juce::Component* led = nullptr;
            for (auto* child : bar.getChildren())
                if (dynamic_cast<MidiActivityLed*>(child) != nullptr)
                    led = child;
            check(led != nullptr && led->isVisible() && !led->getBounds().isEmpty(),
                  "the folded strip lost the MIDI light");
            check(bar.getIndexOfChildComponent(led) > bar.getIndexOfChildComponent(fold),
                  "the strip's button paints over the MIDI light");
            check(fold->getBounds() == bar.getLocalBounds(), "the strip isn't all button");

            // The header grows at once, and its controls fade in.
            press(*fold);
            check(chain.preferredHeaderHeight() == 0, "the header didn't unfold");
            check(foldChanges == 2, "unfolding didn't tell the panel to lay out");
            bar.setSize(900, 28);
            chain.layoutHeader(bar.getLocalBounds());
            const auto* faded = bar.getChildComponent(0);
            check(faded->getAlpha() < 1.0f, "the controls didn't start the fade in");
            juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
            check(faded->getAlpha() == 1.0f, "the controls didn't finish fading in");
            check(laidOutControls() == controls, "unfolding lost a control");
            check(!Config::getInstance().getTrackChainHeaderFolded(), "the unfold wasn't kept");

            // Its right click reaches the post-FX panel and analyzers.
            const auto postFxItem = [&chain](const juce::String& name) {
                const auto menu = chain.createPostFxMenu();
                for (juce::PopupMenu::MenuItemIterator item(menu); item.next();)
                    if (item.getItem().text == name)
                        return item.getItem();
                throw std::runtime_error("the post-FX menu lost an item");
            };
            bool panelRequested = false;
            chain.onPostFxPanelToggled = [&panelRequested](bool open) { panelRequested = open; };
            check(!postFxItem("Post-FX Panel").isTicked, "the closed panel is ticked");
            postFxItem("Post-FX Panel").action();
            check(panelRequested, "the menu didn't ask for the post-FX panel");
            chain.setPostFxPanelOpen(true);
            check(postFxItem("Post-FX Panel").isTicked, "the open panel isn't ticked");

            for (const auto& [pluginId, name] :
                 {std::pair<const char*, const char*>{"oscilloscope", "Oscilloscope"},
                  {"spectrumanalyzer", "Spectrum Analyzer"},
                  {"levels", "Levels"}}) {
                check(!postFxItem(name).isTicked, "a missing analyzer is ticked");
                postFxItem(name).action();
                check(tracks.findPostFxDevice(track, pluginId) != INVALID_DEVICE_ID,
                      "the menu didn't add the analyzer");
                check(postFxItem(name).isTicked, "a present analyzer isn't ticked");
                postFxItem(name).action();
                check(tracks.findPostFxDevice(track, pluginId) == INVALID_DEVICE_ID,
                      "the menu didn't remove the analyzer");
            }
            chain.depopulateHeader(bar);
            selection.clearSelection();
        }
        {
            // A Faust slot's Edit button stands beside the floppy disk, at its size.
            DeviceInfo patch;
            patch.name = "Faust";
            patch.pluginId = daw::audio::FaustPlugin::xmlTypeName;
            patch.format = PluginFormat::Internal;
            patch.loadState = DeviceLoadState::Loaded;
            DeviceSlotComponent slot(patch);
            slot.setSize(slot.getPreferredWidth(), 200);
            std::function<juce::Component*(juce::Component&, const juce::String&)> named =
                [&named](juce::Component& node, const juce::String& name) -> juce::Component* {
                for (auto* child : node.getChildren()) {
                    if (child->getName() == name)
                        return child;
                    if (auto* found = named(*child, name))
                        return found;
                }
                return nullptr;
            };
            auto* edit = named(slot, "Edit Source");
            check(edit != nullptr, "a Faust slot lost its Edit button");
            check(edit->isVisible() && !edit->getBounds().isEmpty(), "Edit is not reachable");
            // Other panels in the slot have a Save Preset of their own.
            const auto* save = named(*edit->getParentComponent(), "Save Preset");
            check(save != nullptr, "Edit doesn't share the floppy disk's selector");
            check(edit->getX() >= save->getRight() && edit->getWidth() == save->getWidth(),
                  "Edit doesn't stand beside the floppy disk");
        }
        {
            // Every device opens one preset menu, with a plugin's files and programs in it (#3213).
            const auto itemsOf = [](const juce::PopupMenu& menu) {
                juce::StringArray items;
                for (juce::PopupMenu::MenuItemIterator item(menu, true); item.next();)
                    items.add(item.getItem().text);
                return items;
            };
            DeviceInfo device;
            device.name = "Preset menu regression device";

            PluginPresetScanner::Entry preset;
            preset.name = "Factory Bass";
            preset.file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("Factory Bass.vstpreset");
            PluginPresetSources plugin;
            plugin.files = {preset};
            plugin.programs = juce::StringArray{"One", ""};
            plugin.extension = ".vstpreset";
            const auto hosted = buildDevicePresetMenu(device, plugin, {});
            const auto hostedItems = itemsOf(hosted.menu);
            for (const auto* expected :
                 {"Plugin Presets", "Factory Bass", "Built-in Programs", "One", "Program 2",
                  "Save as .vstpreset...", "Reveal in Finder", "Reveal Plugin Preset Folder",
                  "Rescan"})
                check(hostedItems.contains(expected), "the hosted preset menu lost an item");
            check(!hostedItems.contains("(no presets yet)"),
                  "the hosted preset menu says it has no presets");
            check(hosted.pluginFiles.size() == 1 && hosted.pluginFiles[0] == preset.file,
                  "a plugin preset doesn't resolve to its file");

            const auto bare = itemsOf(buildDevicePresetMenu(device, {}, {}).menu);
            check(bare.contains("(no presets yet)") && bare.contains("Reveal in Finder"),
                  "a device with no presets lost its menu");
            for (const auto* absent : {"Plugin Presets", "Built-in Programs", "Rescan"})
                check(!bare.contains(absent), "a device without plugin presets shows their items");

            // A device with its own items to load has something, even with no saved presets.
            const auto withItems =
                itemsOf(buildDevicePresetMenu(device, {}, [](juce::PopupMenu& menu) {
                            menu.addItem("Load .dsp File...", [] {});
                        }).menu);
            check(!withItems.contains("(no presets yet)"),
                  "the preset menu says no presets above the device's own items");
        }
        {
            // A curated device's controls stand in balanced rows of at most two.
            for (const auto [cells, columns] :
                 {std::pair{3, 2}, std::pair{6, 2}, std::pair{7, 2}, std::pair{15, 2}}) {
                ParamHostComponent grid(std::make_unique<CompiledFaustDeviceLayout>(cells, cells));
                DeviceInfo model;
                for (int index = 0; index < cells; ++index) {
                    ParameterInfo parameter;
                    parameter.paramIndex = index;
                    parameter.name = "Control " + juce::String(index);
                    model.parameters.push_back(parameter);
                }
                grid.updateParameterSlots(model, 0, {});
                const auto message = "a curated grid of " + std::to_string(cells) +
                                     " controls has " + std::to_string(grid.getCellsPerRow()) +
                                     " columns, not " + std::to_string(columns);
                check(grid.getCellsPerRow() == columns, message.c_str());
                {
                    // A faceplate-first device's knobs wrap the same way: four make a 2 x 2.
                    static constexpr int knobs[] = {0, 1, 2, 5};
                    ParamHostComponent grid(
                        std::make_unique<CompiledFaustDeviceLayout>(6, 6, false, nullptr, knobs));
                    DeviceInfo model;
                    for (int index = 0; index < 6; ++index) {
                        ParameterInfo parameter;
                        parameter.paramIndex = index;
                        parameter.name = "Control " + juce::String(index);
                        model.parameters.push_back(parameter);
                    }
                    grid.updateParameterSlots(model, 0, {});
                    check(grid.getCellsPerRow() == 2 && grid.getSlotCount() == 4,
                          "four faceplate-first knobs are not a 2 x 2");
                }
                {
                    // A device's own mix lives in the side strip, so its grid drops it.
                    ParamHostComponent grid(std::make_unique<CompiledFaustDeviceLayout>(
                        6, 6, false, nullptr, std::span<const int>{}, std::array{4}));
                    DeviceInfo model;
                    for (int index = 0; index < 6; ++index) {
                        ParameterInfo parameter;
                        parameter.paramIndex = index;
                        parameter.name = "Control " + juce::String(index);
                        model.parameters.push_back(parameter);
                    }
                    grid.updateParameterSlots(model, 0, {});
                    check(grid.getSlotCount() == 5, "the mix slot stayed in the grid");
                    for (int cell = 0; cell < grid.getSlotCount(); ++cell)
                        check(grid.getSlot(cell)->getParamIndex() != 4,
                              "the mix slot stayed in the grid");
                }
            }
        }
        {
            // A multiband rack: faceplate on top, bands high to low split by their crossovers.
            const auto bandsId = tracks.addMultibandRackToTrack(track);
            RackComponent bands(track, *tracks.getRack(track, bandsId));
            bands.setVisible(true);
            bands.setSize(bands.getPreferredWidth(), bands.getPreferredHeight());
            std::vector<juce::Component*> rows, dividers;
            juce::Component* faceplate = nullptr;
            juce::Component* split = nullptr;
            const std::function<void(juce::Component&)> walk = [&](juce::Component& parent) {
                for (auto* child : parent.getChildren()) {
                    const juce::String type = typeid(*child).name();
                    if (type.contains("CrossoverDisplay"))
                        faceplate = child;
                    else if (type.contains("CrossoverDivider"))
                        dividers.push_back(child);
                    else if (type.contains("ChainRowComponent"))
                        rows.push_back(child);
                    else if (auto* button = dynamic_cast<juce::Button*>(child);
                             button != nullptr && button->getButtonText() == "Split band")
                        split = child;
                    walk(*child);
                }
            };
            walk(bands);
            check(faceplate != nullptr && faceplate->isVisible() &&
                      !faceplate->getBounds().isEmpty(),
                  "multiband faceplate missing");
            check(rows.size() == 3 && dividers.size() == 2, "multiband rows or dividers missing");
            check(rows[2]->getY() < dividers[1]->getY() && dividers[1]->getY() < rows[1]->getY() &&
                      rows[1]->getY() < dividers[0]->getY() &&
                      dividers[0]->getY() < rows[0]->getY(),
                  "bands are not high to low with their crossovers between them");
            check(split != nullptr && split->getY() > rows[0]->getBottom(),
                  "Split band is not under the bands");
            {
                // The MID label sits centred between the crossovers on the log axis.
                const auto plot = faceplate->getLocalBounds().toFloat().reduced(1.0f);
                const auto xFor = [&](float hz) {
                    return plot.getX() + plot.getWidth() * std::log(hz / 20.0f) / std::log(1000.0f);
                };
                const juce::Point<float> mid{(xFor(180.0f) + xFor(3200.0f)) / 2.0f,
                                             plot.getY() + 14.0f};
                const auto now = juce::Time::getCurrentTime();
                const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), mid,
                                             juce::ModifierKeys::leftButtonModifier, 1, 0, 0, 0, 0,
                                             faceplate, faceplate, now, mid, now, 1, false);
                faceplate->mouseDown(event);
                const auto* rack = tracks.getRack(track, bandsId);
                check(selection.getSelectedChainNode() ==
                          ChainNodePath::rack(track, bandsId).withChain(rack->chains[1].id),
                      "clicking a faceplate band label did not select the band");
            }
            const auto container = rows[0]->getParentComponent();
            check(container->getHeight() >= split->getBottom(), "band list is clipped");
        }
        {
            // The Sampler's waveform goes out alone; the header's button swaps it for the zones.
            DeviceInfo samplerModel;
            samplerModel.name = "Sampler";
            samplerModel.pluginId = "magdasampler";
            samplerModel.format = PluginFormat::Internal;
            samplerModel.loadState = DeviceLoadState::Loaded;
            const auto samplerPath =
                ChainNodePath::topLevelDevice(track, tracks.addDeviceToTrack(track, samplerModel));
            auto& expanded = ExpandedFaceplate::getInstance();
            ExpandedFaceplateHost host;
            host.setSize(1200, 800);
            auto slot =
                std::make_unique<DeviceSlotComponent>(*tracks.getDeviceInChainByPath(samplerPath));
            slot->setNodePath(samplerPath);
            slot->setVisible(true);
            slot->setSize(1400, 400);
            auto* sampler = findSamplerUI(*slot);
            check(sampler != nullptr, "the Sampler slot has no faceplate");
            auto& waveform = sampler->expandablePart();
            auto& waveformExpand = button(*sampler, "Expand");

            press(waveformExpand);
            check(expanded.holds(waveform), "the waveform button did not lend the waveform");
            check(waveform.getParentComponent() == &host && waveform.isVisible(),
                  "the arrangement does not show the lent waveform");
            check(sampler->getParentComponent() == slot.get(),
                  "lending the waveform took the whole faceplate");
            const auto lentBounds = waveform.getBounds();
            sampler->resized();
            check(waveform.getBounds() == lentBounds, "the faceplate laid out the lent waveform");

            SegmentedChoice* tabs = nullptr;
            for (auto* child : sampler->getChildren())
                if (auto* choice = dynamic_cast<SegmentedChoice*>(child);
                    choice != nullptr && choice->isVisible() && choice->getWidth() == 240)
                    tabs = choice;
            check(tabs != nullptr, "the Sampler has no tabs");
            tabs->onChange(3);
            check(waveform.isVisible() && !waveformExpand.isVisible(),
                  "an envelope tab hid the lent waveform");
            tabs->onChange(0);
            check(waveformExpand.isVisible(), "the Sample tab lost its expand button");

            press(button(*slot, "Expand"));
            check(expanded.holds(*sampler), "the header button did not swap to the zones");
            check(waveform.getParentComponent() == sampler && waveform.isVisible(),
                  "the swap left the waveform out of the faceplate");
            expanded.collapse();
            check(sampler->getParentComponent() == slot.get() && sampler->isVisible(),
                  "the faceplate did not come back");

            press(waveformExpand);
            press(waveformExpand);
            check(!expanded.isActive(), "the waveform button did not close its expansion");
            check(waveform.getParentComponent() == sampler && waveform.isVisible() &&
                      !waveform.getBounds().isEmpty(),
                  "the waveform did not come back");

            press(waveformExpand);
            slot.reset();
            check(expanded.faceplate() == nullptr, "a deleted Sampler left its waveform out");
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            check(!expanded.isActive(), "a deleted Sampler's expansion never ended");
        }
        {
            // A stereo file draws in two lanes, as audio clips do: only the left lane lights up
            // for left-only audio, where the view's own mono wave lights both sides of its centre.
            const juce::TemporaryFile temporary(".wav");
            const auto file = temporary.getFile();
            juce::AudioBuffer<float> audio(2, 44100);
            audio.clear();
            for (int i = 0; i < audio.getNumSamples(); ++i)
                audio.setSample(0, i, 0.9f * std::sin(0.05f * static_cast<float>(i)));
            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(
                    file.createOutputStream().release(), 44100.0, 2, 16, {}, 0));
                check(writer != nullptr, "could not write the test sample");
                writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
            }
            auto* thumbnail =
                AudioThumbnailManager::getInstance().getThumbnail(file.getFullPathName());
            check(thumbnail != nullptr, "no thumbnail for the test sample");
            for (int i = 0; i < 200 && !thumbnail->isFullyLoaded(); ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil(20);

            SamplerWaveformPane pane;
            pane.setSize(800, 300);
            const auto litPixels = [&pane](int fromY, int toY) {
                const auto shot = pane.createComponentSnapshot(pane.getLocalBounds(), false, 1.0f);
                const auto background = shot.getPixelAt(400, 20).getBrightness();
                int lit = 0;
                for (int y = fromY; y < toY; ++y)
                    for (int x = 0; x < shot.getWidth(); ++x)
                        if (std::abs(shot.getPixelAt(x, y).getBrightness() - background) > 0.15f)
                            ++lit;
                return lit;
            };
            constexpr int kWaveTop = sdk::WaveformView::kRulerHeight;
            const int waveBottom = pane.getHeight() - sdk::WaveformView::kScrollBarHeight;
            const int middle = (kWaveTop + waveBottom) / 2;

            pane.setAudio(&audio, 1.0, file.getFullPathName());
            const int clipLeft = litPixels(kWaveTop + 4, middle - 2);
            const int clipRight = litPixels(middle + 2, waveBottom - 4);
            check(clipLeft > 5000, "the clip wave drew nothing for the left channel");
            check(clipRight * 10 < clipLeft, "the clip wave did not keep the channels apart");

            pane.setAudio(&audio, 1.0);
            const int viewUpper = litPixels(kWaveTop + 4, middle - 2);
            const int viewLower = litPixels(middle + 2, waveBottom - 4);
            check(viewUpper > 1000 && viewLower * 2 > viewUpper,
                  "a sample with no file lost the view's wave");
            pane.setAudio(nullptr, 0.0);
        }
        {
            // Session strips and the master meter left to right, above volume and pan.
            SessionView session;
            session.setSize(1400, 800);
            session.resized();
            std::function<void(juce::Component&, std::vector<LevelMeter*>&)> findMeters =
                [&findMeters](juce::Component& root, std::vector<LevelMeter*>& found) {
                    for (auto* child : root.getChildren()) {
                        if (auto* meter = dynamic_cast<LevelMeter*>(child))
                            found.push_back(meter);
                        findMeters(*child, found);
                    }
                };
            std::vector<LevelMeter*> found;
            findMeters(session, found);
            check(found.size() >= 2, "the Session view has no strip or master meters");
            for (const auto* meter : found) {
                check(meter->isVisible() && meter->getWidth() > meter->getHeight() &&
                          meter->getHeight() == 8,
                      "a Session meter is not a thin horizontal bar");
                const auto* strip = meter->getParentComponent();
                bool belowIt = false;
                for (const auto* sibling : strip->getChildren())
                    belowIt = belowIt || (sibling != meter &&
                                          sibling->getBounds().getY() >= meter->getBottom());
                check(belowIt, "a Session meter is not above its strip's controls");
            }
        }
        selection.clearSelection();
        tracks.clearAllTracks();
        tracks.shutdown();
        std::cout << "PASS: device and rack single clicks preserve layout; double clicks "
                     "toggle it\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
