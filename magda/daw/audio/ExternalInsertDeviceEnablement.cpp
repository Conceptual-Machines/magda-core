#include "ExternalInsertDeviceEnablement.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include "../core/Config.hpp"

namespace magda {

namespace te = tracktion;

ExternalInsertDeviceEnablement::ExternalInsertDeviceEnablement(te::Edit& edit) : edit_(edit) {
    // Restore the auto-enabled set: TE persists device enablement globally,
    // so a port auto-enabled last session comes back enabled at startup and
    // must still count as auto-enabled, not user-enabled.
    for (const auto& name : Config::getInstance().getAutoEnabledInsertInputs())
        autoEnabledInputs_.insert(juce::String(name));
    for (const auto& name : Config::getInstance().getAutoEnabledInsertOutputs())
        autoEnabledOutputs_.insert(juce::String(name));
}

ExternalInsertDeviceEnablement::PortAction ExternalInsertDeviceEnablement::reconcilePort(
    bool usedByInsert, bool portEnabled, bool trackedAsAuto) {
    PortAction action;
    if (usedByInsert) {
        if (!portEnabled) {
            action.changeEnabled = true;
            action.enabled = true;
            action.trackAsAuto = true;
        } else {
            // Already enabled: it stays auto-tracked only if WE enabled it
            // (this session or, via the persisted set, a previous one). A
            // user-enabled port is never auto-disabled later.
            action.trackAsAuto = trackedAsAuto;
        }
    } else {
        if (trackedAsAuto && portEnabled) {
            action.changeEnabled = true;
            action.enabled = false;
        }
        action.trackAsAuto = false;
    }
    return action;
}

void ExternalInsertDeviceEnablement::persistAutoEnabledSets() const {
    auto& config = Config::getInstance();
    std::vector<std::string> inputs;
    inputs.reserve(autoEnabledInputs_.size());
    for (const auto& name : autoEnabledInputs_)
        inputs.push_back(name.toStdString());
    std::vector<std::string> outputs;
    outputs.reserve(autoEnabledOutputs_.size());
    for (const auto& name : autoEnabledOutputs_)
        outputs.push_back(name.toStdString());
    config.setAutoEnabledInsertInputs(inputs);
    config.setAutoEnabledInsertOutputs(outputs);
    config.save();
}

bool ExternalInsertDeviceEnablement::refresh() {
    auto& dm = edit_.engine.getDeviceManager();

    // Every port an enabled insert references, by direction. The insert's
    // return is one of TE's input devices; its send is an output device.
    std::set<juce::String> usedInputs, usedOutputs;
    const auto allPlugins = te::getAllPlugins(edit_, false);
    for (auto plugin : allPlugins) {
        auto* insert = dynamic_cast<te::InsertPlugin*>(plugin);
        if (insert == nullptr || !insert->isEnabled())
            continue;
        if (insert->inputDevice.get().isNotEmpty())
            usedInputs.insert(insert->inputDevice.get());
        if (insert->outputDevice.get().isNotEmpty())
            usedOutputs.insert(insert->outputDevice.get());
    }

    bool changed = false;
    bool setsChanged = false;

    const auto reconcile = [&](auto* device, std::set<juce::String>& autoSet,
                               const std::set<juce::String>& used) {
        const auto name = device->getName();
        const auto action =
            reconcilePort(used.count(name) > 0, device->isEnabled(), autoSet.count(name) > 0);
        if (action.changeEnabled) {
            device->setEnabled(action.enabled);
            changed = true;
        }
        if (action.trackAsAuto)
            setsChanged = autoSet.insert(name).second || setsChanged;
        else
            setsChanged = (autoSet.erase(name) > 0) || setsChanged;
    };

    for (int i = 0; i < dm.getNumInputDevices(); ++i)
        if (auto* in = dm.getInputDevice(i))
            reconcile(in, autoEnabledInputs_, usedInputs);

    for (int i = 0; i < dm.getNumOutputDevices(); ++i)
        if (auto* out = dm.getOutputDeviceAt(i))
            reconcile(out, autoEnabledOutputs_, usedOutputs);

    if (setsChanged)
        persistAutoEnabledSets();

    if (changed) {
        // A just-enabled device only resolves in the inserts after
        // updateDeviceTypes() re-runs against the new device lists.
        for (auto plugin : allPlugins)
            if (auto* insert = dynamic_cast<te::InsertPlugin*>(plugin))
                insert->updateDeviceTypes();
    }

    return changed;
}

}  // namespace magda
