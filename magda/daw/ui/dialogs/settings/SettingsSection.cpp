// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SettingsSection.hpp"

#include "core/Config.hpp"

namespace magda {

namespace {
// var::operator== compares objects by pointer, so two toVar() trees never match.
bool sameValue(const juce::var& a, const juce::var& b) {
    if (a.isArray() && b.isArray()) {
        if (a.size() != b.size())
            return false;
        for (int i = 0; i < a.size(); ++i)
            if (!sameValue(a[i], b[i]))
                return false;
        return true;
    }
    return SettingsSection::countDifferences(a, b) == 0;
}
}  // namespace

SettingsSection::SettingsSection(SettingsSectionId id) : id_(id) {}

SettingsSection::~SettingsSection() = default;

void SettingsSection::captureBaseline() {
    if (appliesImmediately())
        return;
    snapshot_ = Config::getInstance().clone();
    auto written = snapshot_->clone();
    writeTo(*written);
    baseline_ = written->toVar();
}

int SettingsSection::countChanges() {
    if (snapshot_ == nullptr)
        return 0;
    auto written = snapshot_->clone();
    writeTo(*written);
    return countDifferences(baseline_, written->toVar());
}

void SettingsSection::restoreDefaults() {
    if (appliesImmediately())
        return;
    // load() reads only this section's values, so a default Config resets just them.
    load(*Config::createDetached());
}

int SettingsSection::countDifferences(const juce::var& a, const juce::var& b) {
    auto* objA = a.getDynamicObject();
    auto* objB = b.getDynamicObject();
    if (a.isArray() || b.isArray())
        return sameValue(a, b) ? 0 : 1;
    if (objA == nullptr || objB == nullptr)
        return a == b ? 0 : 1;

    int count = 0;
    for (const auto& property : objA->getProperties())
        count += countDifferences(property.value, objB->getProperty(property.name));
    for (const auto& property : objB->getProperties())
        if (!objA->hasProperty(property.name))
            ++count;
    return count;
}

}  // namespace magda
