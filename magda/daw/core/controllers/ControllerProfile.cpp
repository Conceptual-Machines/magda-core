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

    return {obj};
}

// ============================================================================
// JSON decoding
// ============================================================================

std::optional<ControllerProfile> decodeControllerProfile(
    const juce::var& v, std::vector<ProfileValidationIssue>* skipped) {
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

    // Entries without an id are reported by 1-based position.
    auto skip = [skipped](const char* key, const juce::String& id, int index) {
        DBG("ControllerProfile: skipping entry " << (id.isNotEmpty() ? id : juce::String(index + 1))
                                                 << ": " << key);
        if (skipped != nullptr)
            skipped->push_back({juce::String("controllers.validation.") + key,
                                id.isNotEmpty() ? id : "#" + juce::String(index + 1)});
    };
    auto isMidi7Bit = [](int n) { return n >= 0 && n <= 127; };

    // A control with partial fields would otherwise load as "cc=0 on channel 'any'"
    // and capture all CC0 traffic.
    auto controlsVar = obj->getProperty("controls");
    if (controlsVar.isArray()) {
        for (int i = 0; i < controlsVar.size(); ++i) {
            auto* co = controlsVar[i].getDynamicObject();
            if (co == nullptr) {
                skip("control_incomplete", {}, i);
                continue;
            }
            ControllerProfileControl ctrl;
            ctrl.controlId = co->getProperty("controlId").toString();
            ctrl.kind = co->getProperty("kind").toString();

            if (ctrl.controlId.isEmpty() || ctrl.kind.isEmpty() || !co->hasProperty("cc") ||
                !co->hasProperty("channel")) {
                skip("control_incomplete", ctrl.controlId, i);
                continue;
            }

            ctrl.cc = static_cast<int>(co->getProperty("cc"));
            ctrl.channel = static_cast<int>(co->getProperty("channel"));
            if (!isMidi7Bit(ctrl.cc)) {
                skip("control_cc_out_of_range", ctrl.controlId, i);
                continue;
            }
            if (ctrl.channel != -1 && (ctrl.channel < 1 || ctrl.channel > 16)) {
                skip("control_channel_out_of_range", ctrl.controlId, i);
                continue;
            }
            if (co->hasProperty("feedbackCc")) {
                ctrl.feedbackCc = static_cast<int>(co->getProperty("feedbackCc"));
                if (ctrl.feedbackCc != -1 && !isMidi7Bit(ctrl.feedbackCc)) {
                    skip("control_feedback_cc_out_of_range", ctrl.controlId, i);
                    continue;
                }
            }
            p.controls.push_back(ctrl);
        }
    }

    if (p.controls.empty())
        return std::nullopt;

    auto bindingsVar = obj->getProperty("defaultBindings");
    if (bindingsVar.isArray()) {
        for (int i = 0; i < bindingsVar.size(); ++i) {
            auto* bo = bindingsVar[i].getDynamicObject();
            if (bo == nullptr) {
                skip("binding_incomplete", {}, i);
                continue;
            }
            ControllerProfileDefaultBinding db;
            db.controlId = bo->getProperty("controlId").toString();
            db.resolverKind = bo->getProperty("resolverKind").toString();

            if (db.controlId.isEmpty() || db.resolverKind.isEmpty()) {
                skip("binding_incomplete", db.controlId, i);
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
