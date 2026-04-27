#include "ControllerProfile.hpp"

#include <set>
#include <unordered_map>

#include "../aliases/ResolverRegistry.hpp"

namespace magda {

// ============================================================================
// ControllerProfile
// ============================================================================

bool ControllerProfile::isValid() const {
    return id.isNotEmpty() && name.isNotEmpty() && !controls.empty();
}

// ============================================================================
// JSON encoding
// ============================================================================

juce::var encodeControllerProfile(const ControllerProfile& p) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("id", p.id);
    obj->setProperty("vendor", p.vendor);
    obj->setProperty("name", p.name);

    juce::Array<juce::var> controlsArr;
    for (const auto& ctrl : p.controls) {
        auto* c = new juce::DynamicObject();
        c->setProperty("controlId", ctrl.controlId);
        c->setProperty("kind", ctrl.kind);
        c->setProperty("cc", ctrl.cc);
        c->setProperty("channel", ctrl.channel);
        if (ctrl.feedbackCc >= 0)
            c->setProperty("feedbackCc", ctrl.feedbackCc);
        controlsArr.add(juce::var(c));
    }
    obj->setProperty("controls", controlsArr);

    juce::Array<juce::var> bindingsArr;
    for (const auto& db : p.defaultBindings) {
        auto* b = new juce::DynamicObject();
        b->setProperty("controlId", db.controlId);
        b->setProperty("resolverKind", db.resolverKind);
        auto* argsObj = new juce::DynamicObject();
        for (int i = 0; i < db.args.size(); ++i)
            argsObj->setProperty(db.args.getAllKeys()[i], db.args.getAllValues()[i]);
        b->setProperty("args", juce::var(argsObj));
        bindingsArr.add(juce::var(b));
    }
    obj->setProperty("defaultBindings", bindingsArr);

    return juce::var(obj);
}

// ============================================================================
// JSON decoding
// ============================================================================

std::optional<ControllerProfile> decodeControllerProfile(const juce::var& v) {
    if (!v.isObject())
        return std::nullopt;

    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return std::nullopt;

    // Required fields
    if (!obj->hasProperty("id") || !obj->hasProperty("name"))
        return std::nullopt;

    ControllerProfile p;
    p.id = obj->getProperty("id").toString();
    p.name = obj->getProperty("name").toString();

    if (p.id.isEmpty() || p.name.isEmpty())
        return std::nullopt;

    p.vendor = obj->getProperty("vendor").toString();

    // Decode controls — skip entries with missing required fields or out-of-range
    // MIDI values. Without this, a profile with partial fields loads as "cc=0 on
    // channel 'any'" and silently captures all CC0 traffic.
    auto controlsVar = obj->getProperty("controls");
    if (controlsVar.isArray()) {
        for (int i = 0; i < controlsVar.size(); ++i) {
            const auto& cv = controlsVar[i];
            auto* co = cv.getDynamicObject();
            if (co == nullptr)
                continue;
            ControllerProfileControl ctrl;
            ctrl.controlId = co->getProperty("controlId").toString();
            ctrl.kind = co->getProperty("kind").toString();

            if (ctrl.controlId.isEmpty() || ctrl.kind.isEmpty()) {
                DBG("ControllerProfile: skipping control missing controlId or kind");
                continue;
            }
            if (!co->hasProperty("cc") || !co->hasProperty("channel")) {
                DBG("ControllerProfile: skipping control '" << ctrl.controlId
                                                            << "' missing cc or channel");
                continue;
            }

            ctrl.cc = static_cast<int>(co->getProperty("cc"));
            ctrl.channel = static_cast<int>(co->getProperty("channel"));
            if (ctrl.cc < 0 || ctrl.cc > 127) {
                DBG("ControllerProfile: skipping control '" << ctrl.controlId
                                                            << "' cc out of range: " << ctrl.cc);
                continue;
            }
            if (ctrl.channel != -1 && (ctrl.channel < 1 || ctrl.channel > 16)) {
                DBG("ControllerProfile: skipping control '"
                    << ctrl.controlId << "' channel out of range: " << ctrl.channel);
                continue;
            }

            if (co->hasProperty("feedbackCc"))
                ctrl.feedbackCc = static_cast<int>(co->getProperty("feedbackCc"));
            p.controls.push_back(ctrl);
        }
    }

    if (p.controls.empty())
        return std::nullopt;

    // Decode defaultBindings (skip malformed entries individually)
    auto bindingsVar = obj->getProperty("defaultBindings");
    if (bindingsVar.isArray()) {
        for (int i = 0; i < bindingsVar.size(); ++i) {
            const auto& bv = bindingsVar[i];
            auto* bo = bv.getDynamicObject();
            if (bo == nullptr) {
                DBG("ControllerProfile: skipping malformed defaultBinding entry (not an object)");
                continue;
            }
            if (!bo->hasProperty("controlId") || !bo->hasProperty("resolverKind")) {
                DBG("ControllerProfile: skipping defaultBinding missing controlId or resolverKind");
                continue;
            }
            ControllerProfileDefaultBinding db;
            db.controlId = bo->getProperty("controlId").toString();
            db.resolverKind = bo->getProperty("resolverKind").toString();

            if (db.controlId.isEmpty() || db.resolverKind.isEmpty()) {
                DBG("ControllerProfile: skipping defaultBinding with empty controlId or "
                    "resolverKind");
                continue;
            }

            auto argsVar = bo->getProperty("args");
            if (argsVar.isObject()) {
                auto* argsObj = argsVar.getDynamicObject();
                if (argsObj != nullptr) {
                    for (const auto& prop : argsObj->getProperties())
                        db.args.set(prop.name.toString(), prop.value.toString());
                }
            }

            p.defaultBindings.push_back(db);
        }
    }

    return p;
}

// ============================================================================
// Cross-field validation
// ============================================================================

std::vector<ProfileValidationIssue> validateControllerProfile(const ControllerProfile& p) {
    std::vector<ProfileValidationIssue> issues;

    // Duplicate controlIds — surface every offender exactly once.
    std::set<juce::String> seen;
    std::set<juce::String> reported;
    for (const auto& c : p.controls) {
        if (!seen.insert(c.controlId).second && reported.insert(c.controlId).second)
            issues.push_back({"controllers.validation.duplicate_control_id", c.controlId});
    }

    // defaultBindings must reference an existing control.
    for (const auto& db : p.defaultBindings) {
        bool found = false;
        for (const auto& c : p.controls) {
            if (c.controlId == db.controlId) {
                found = true;
                break;
            }
        }
        if (!found) {
            issues.push_back(
                {"controllers.validation.unknown_default_binding_control_id", db.controlId});
        }
    }

    return issues;
}

// ============================================================================
// Materialisation
// ============================================================================

MaterialisedController materialiseControllerFromProfile(const ControllerProfile& profile,
                                                        const juce::String& inputPort,
                                                        const juce::String& outputPort,
                                                        const juce::String& inputPortName) {
    MaterialisedController result;

    result.controller.id = juce::Uuid();
    result.controller.name = profile.name;
    result.controller.vendor = profile.vendor;
    result.controller.inputPort = inputPort;
    result.controller.inputPortName = inputPortName;
    result.controller.outputPort = outputPort;
    result.controller.profileId = profile.id;

    // Build a lookup map from controlId -> control
    std::unordered_map<juce::String, const ControllerProfileControl*> controlMap;
    for (const auto& ctrl : profile.controls)
        controlMap[ctrl.controlId] = &ctrl;

    auto& resolverReg = ResolverRegistry::getInstance();

    for (const auto& db : profile.defaultBindings) {
        // Check controlId exists
        auto it = controlMap.find(db.controlId);
        if (it == controlMap.end()) {
            DBG("materialiseControllerFromProfile: skipping binding for unknown controlId '"
                << db.controlId << "'");
            continue;
        }

        // Check resolverKind is registered
        if (resolverReg.findResolver(db.resolverKind) == nullptr) {
            DBG("materialiseControllerFromProfile: skipping binding with unregistered resolverKind "
                "'"
                << db.resolverKind << "'");
            continue;
        }

        const auto* ctrl = it->second;

        Binding binding;
        binding.id = juce::Uuid();
        binding.source.controllerId = result.controller.id;
        binding.source.msgType = BindingMsgType::CC;
        binding.source.channel = (ctrl->channel < 0) ? 0 : ctrl->channel;
        binding.source.number = ctrl->cc;
        binding.target = ResolverRef{db.resolverKind, db.args};
        binding.mode = BindingMode::Absolute;
        // range defaults (0.0 - 1.0 linear)

        result.bindings.push_back(binding);
    }

    return result;
}

}  // namespace magda
