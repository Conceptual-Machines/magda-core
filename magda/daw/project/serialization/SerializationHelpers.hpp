#pragma once
#include <juce_core/juce_core.h>

#include <type_traits>

namespace magda::ser {

// -- Write overloads ----------------------------------------------------------

inline void writeField(juce::DynamicObject* o, const char* k, int v) {
    o->setProperty(k, v);
}
inline void writeField(juce::DynamicObject* o, const char* k, double v) {
    o->setProperty(k, v);
}
inline void writeField(juce::DynamicObject* o, const char* k, float v) {
    o->setProperty(k, static_cast<double>(v));
}
inline void writeField(juce::DynamicObject* o, const char* k, bool v) {
    o->setProperty(k, v);
}
inline void writeField(juce::DynamicObject* o, const char* k, const juce::String& v) {
    o->setProperty(k, v);
}

// Colour -- uses toDisplayString(true) format (ARGB hex, no '#' prefix)
// to match existing ProjectSerializer::colourToString behaviour.
inline void writeField(juce::DynamicObject* o, const char* k, juce::Colour v) {
    o->setProperty(k, v.toDisplayString(true));
}

// Enums -> int
template <typename E>
    requires std::is_enum_v<E>
void writeField(juce::DynamicObject* o, const char* k, E v) {
    o->setProperty(k, static_cast<int>(v));
}

// -- Read overloads -----------------------------------------------------------

inline void readField(const juce::DynamicObject* o, const char* k, int& v) {
    v = o->getProperty(k);
}
inline void readField(const juce::DynamicObject* o, const char* k, double& v) {
    v = o->getProperty(k);
}
inline void readField(const juce::DynamicObject* o, const char* k, float& v) {
    v = static_cast<float>(static_cast<double>(o->getProperty(k)));
}
inline void readField(const juce::DynamicObject* o, const char* k, bool& v) {
    v = static_cast<bool>(o->getProperty(k));
}
inline void readField(const juce::DynamicObject* o, const char* k, juce::String& v) {
    v = o->getProperty(k).toString();
}

inline void readField(const juce::DynamicObject* o, const char* k, juce::Colour& v) {
    v = juce::Colour::fromString(o->getProperty(k).toString());
}

template <typename E>
    requires std::is_enum_v<E>
void readField(const juce::DynamicObject* o, const char* k, E& v) {
    v = static_cast<E>(static_cast<int>(o->getProperty(k)));
}

}  // namespace magda::ser

// -- Macros (use inside serialize/deserialize functions) ----------------------
// Expects local variables: `obj` (DynamicObject*) and `data` (the struct ref)
#define SER(field) magda::ser::writeField(obj, #field, data.field)
#define DESER(field) magda::ser::readField(obj, #field, data.field)
