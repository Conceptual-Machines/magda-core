// Full component regression check. Run with run_chain_interaction_check.py.
#include <functional>
#include <iostream>
#include <stdexcept>
#include <typeinfo>

#include "audio/AudioThumbnailManager.hpp"
#include "core/ClipManager.hpp"
#include "core/ModulatorEngine.hpp"
#include "core/TrackManager.hpp"
#include "ui/components/chain/DeviceSlotComponent.hpp"
#include "ui/components/chain/RackComponent.hpp"
#include "ui/components/chain/layout/StandardDeviceLayout.hpp"
#include "ui/components/chain/slot/DeviceSlotSelectionHandling.hpp"
#include "ui/themes/FontManager.hpp"

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
            for (const auto* name : {"Power", "Close"})
                for (auto* child : rack.getChildren())
                    if (child->getName() == name)
                        check(child->isVisible() && !child->getBounds().isEmpty(),
                              "rack header power/close lost after an empty layout");
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
            const auto container = rows[0]->getParentComponent();
            check(container->getHeight() >= split->getBottom(), "band list is clipped");
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
