#include "Controller.hpp"

namespace magda {

bool Controller::isValid() const {
    return !id.isNull() && inputPort.isNotEmpty();
}

bool Controller::operator==(const Controller& other) const {
    return id == other.id && name == other.name && vendor == other.vendor &&
           inputPort == other.inputPort && inputPortName == other.inputPortName &&
           outputPort == other.outputPort && script == other.script && profileId == other.profileId;
}

// ============================================================================
// JSON encoding
// ============================================================================

juce::var encodeController(const Controller& c) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("id", c.id.toDashedString());
    obj->setProperty("name", c.name);
    obj->setProperty("vendor", c.vendor);
    obj->setProperty("inputPort", c.inputPort);
    if (c.inputPortName.isNotEmpty())
        obj->setProperty("inputPortName", c.inputPortName);
    obj->setProperty("outputPort", c.outputPort);
    obj->setProperty("script", c.script);
    if (c.profileId.isNotEmpty())
        obj->setProperty("profileId", c.profileId);
    return juce::var(obj);
}

// ============================================================================
// JSON decoding
// ============================================================================

std::optional<Controller> decodeController(const juce::var& v) {
    if (!v.isObject())
        return std::nullopt;

    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return std::nullopt;

    if (!obj->hasProperty("id") || !obj->hasProperty("inputPort"))
        return std::nullopt;

    Controller c;
    c.id = juce::Uuid(obj->getProperty("id").toString());
    c.name = obj->getProperty("name").toString();
    c.vendor = obj->getProperty("vendor").toString();
    c.inputPort = obj->getProperty("inputPort").toString();
    c.inputPortName = obj->getProperty("inputPortName").toString();  // optional
    c.outputPort = obj->getProperty("outputPort").toString();
    c.script = obj->getProperty("script").toString();
    c.profileId = obj->getProperty("profileId").toString();  // optional, defaults to empty

    return c;
}

}  // namespace magda
