// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <type_traits>

#include "../../core/ControlTarget.hpp"
#include "JsonReader.hpp"

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

/** @brief A macro or mod link's target, also what a pre-#1149 single `target` held. */
void readControlTarget(const ObjectReader& in, ControlTarget& target);

}  // namespace magda::ser

// -- Macros (use inside serialize/deserialize functions) ----------------------
// SER expects `obj` (DynamicObject*) and `data`; DESER and DESER_OPT expect `in`
// (ser::ObjectReader) and `data`. DESER_OPT keeps the struct's default when absent.
#define SER(field) magda::ser::writeField(obj, #field, data.field)
#define DESER(field) in.required(#field, data.field)
#define DESER_OPT(field) in.optional(#field, data.field)
