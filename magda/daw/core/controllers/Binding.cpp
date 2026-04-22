#include "Binding.hpp"

namespace magda {

// ============================================================================
// BindingSource
// ============================================================================

bool BindingSource::operator==(const BindingSource& other) const {
    return portKey == other.portKey && controllerId == other.controllerId &&
           msgType == other.msgType && channel == other.channel && number == other.number;
}

// ============================================================================
// Binding
// ============================================================================

bool Binding::isValid() const {
    // A source needs an addressable key: a port (Learn-created) or a
    // registered Controller (scripted-surface-created).
    const bool hasAddress = source.portKey.isNotEmpty() || !source.controllerId.isNull();
    return !id.isNull() && hasAddress;
}

// ============================================================================
// Helpers: enum <-> string
// ============================================================================

namespace {

juce::String msgTypeToString(BindingMsgType t) {
    switch (t) {
        case BindingMsgType::CC:
            return "cc";
        case BindingMsgType::Note:
            return "note";
        case BindingMsgType::PitchBend:
            return "pitchbend";
        case BindingMsgType::NRPN:
            return "nrpn";
    }
    return "cc";
}

BindingMsgType msgTypeFromString(const juce::String& s) {
    if (s == "note")
        return BindingMsgType::Note;
    if (s == "pitchbend")
        return BindingMsgType::PitchBend;
    if (s == "nrpn")
        return BindingMsgType::NRPN;
    return BindingMsgType::CC;
}

juce::String modeToString(BindingMode m) {
    switch (m) {
        case BindingMode::Absolute:
            return "absolute";
        case BindingMode::Relative2sComp:
            return "relative_2scomp";
        case BindingMode::RelativeSignMag:
            return "relative_signmag";
        case BindingMode::RelativeBinOff:
            return "relative_binoff";
        case BindingMode::Toggle:
            return "toggle";
    }
    return "absolute";
}

BindingMode modeFromString(const juce::String& s) {
    if (s == "relative_2scomp")
        return BindingMode::Relative2sComp;
    if (s == "relative_signmag")
        return BindingMode::RelativeSignMag;
    if (s == "relative_binoff")
        return BindingMode::RelativeBinOff;
    if (s == "toggle")
        return BindingMode::Toggle;
    return BindingMode::Absolute;
}

juce::String curveToString(BindingCurve c) {
    switch (c) {
        case BindingCurve::Linear:
            return "linear";
        case BindingCurve::Log:
            return "log";
        case BindingCurve::Exp:
            return "exp";
        case BindingCurve::SCurve:
            return "scurve";
    }
    return "linear";
}

BindingCurve curveFromString(const juce::String& s) {
    if (s == "log")
        return BindingCurve::Log;
    if (s == "exp")
        return BindingCurve::Exp;
    if (s == "scurve")
        return BindingCurve::SCurve;
    return BindingCurve::Linear;
}

}  // namespace

// ============================================================================
// JSON encoding
// ============================================================================

juce::var encodeBinding(const Binding& b) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("id", b.id.toDashedString());

    // Source
    auto* srcObj = new juce::DynamicObject();
    if (b.source.portKey.isNotEmpty())
        srcObj->setProperty("portKey", b.source.portKey);
    srcObj->setProperty("controllerId", b.source.controllerId.toDashedString());
    srcObj->setProperty("msgType", msgTypeToString(b.source.msgType));
    srcObj->setProperty("channel", b.source.channel);
    srcObj->setProperty("number", b.source.number);
    obj->setProperty("source", juce::var(srcObj));

    // Target (encode to JSON string, store as string)
    obj->setProperty("target", encodeTarget(b.target));

    // Mode
    obj->setProperty("mode", modeToString(b.mode));

    // Range
    auto* rangeObj = new juce::DynamicObject();
    rangeObj->setProperty("min", b.range.min);
    rangeObj->setProperty("max", b.range.max);
    rangeObj->setProperty("curve", curveToString(b.range.curve));
    obj->setProperty("range", juce::var(rangeObj));

    return juce::var(obj);
}

// ============================================================================
// JSON decoding
// ============================================================================

std::optional<Binding> decodeBinding(const juce::var& v) {
    if (!v.isObject())
        return std::nullopt;

    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return std::nullopt;

    if (!obj->hasProperty("id") || !obj->hasProperty("source") || !obj->hasProperty("target"))
        return std::nullopt;

    Binding b;
    b.id = juce::Uuid(obj->getProperty("id").toString());

    // Source
    auto srcVar = obj->getProperty("source");
    if (!srcVar.isObject())
        return std::nullopt;
    auto* srcObj = srcVar.getDynamicObject();
    if (!srcObj)
        return std::nullopt;

    if (srcObj->hasProperty("portKey"))
        b.source.portKey = srcObj->getProperty("portKey").toString();
    b.source.controllerId = juce::Uuid(srcObj->getProperty("controllerId").toString());
    b.source.msgType = msgTypeFromString(srcObj->getProperty("msgType").toString());
    b.source.channel = static_cast<int>(srcObj->getProperty("channel"));
    b.source.number = static_cast<int>(srcObj->getProperty("number"));

    // Target
    auto targetOpt = decodeTarget(obj->getProperty("target").toString());
    if (!targetOpt.has_value())
        return std::nullopt;
    b.target = *targetOpt;

    // Mode
    if (obj->hasProperty("mode"))
        b.mode = modeFromString(obj->getProperty("mode").toString());

    // Range
    if (obj->hasProperty("range")) {
        auto rangeVar = obj->getProperty("range");
        if (rangeVar.isObject()) {
            if (auto* rangeObj = rangeVar.getDynamicObject()) {
                if (rangeObj->hasProperty("min"))
                    b.range.min = static_cast<float>(rangeObj->getProperty("min"));
                if (rangeObj->hasProperty("max"))
                    b.range.max = static_cast<float>(rangeObj->getProperty("max"));
                if (rangeObj->hasProperty("curve"))
                    b.range.curve = curveFromString(rangeObj->getProperty("curve").toString());
            }
        }
    }

    return b;
}

}  // namespace magda
