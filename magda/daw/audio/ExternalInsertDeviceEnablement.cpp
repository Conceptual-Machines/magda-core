#include "ExternalInsertDeviceEnablement.hpp"

#include <tracktion_engine/tracktion_engine.h>

namespace magda {

namespace te = tracktion;

ExternalInsertDeviceEnablement::ExternalInsertDeviceEnablement(te::Edit& edit) : edit_(edit) {}

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

    for (int i = 0; i < dm.getNumInputDevices(); ++i) {
        auto* in = dm.getInputDevice(i);
        if (in == nullptr)
            continue;
        const auto name = in->getName();
        if (usedInputs.count(name) > 0 && !in->isEnabled()) {
            in->setEnabled(true);
            autoEnabledInputs_.insert(name);
            changed = true;
        } else if (usedInputs.count(name) == 0 && autoEnabledInputs_.count(name) > 0) {
            if (in->isEnabled()) {
                in->setEnabled(false);
                changed = true;
            }
            autoEnabledInputs_.erase(name);
        }
    }

    for (int i = 0; i < dm.getNumOutputDevices(); ++i) {
        auto* out = dm.getOutputDeviceAt(i);
        if (out == nullptr)
            continue;
        const auto name = out->getName();
        if (usedOutputs.count(name) > 0 && !out->isEnabled()) {
            out->setEnabled(true);
            autoEnabledOutputs_.insert(name);
            changed = true;
        } else if (usedOutputs.count(name) == 0 && autoEnabledOutputs_.count(name) > 0) {
            if (out->isEnabled()) {
                out->setEnabled(false);
                changed = true;
            }
            autoEnabledOutputs_.erase(name);
        }
    }

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
