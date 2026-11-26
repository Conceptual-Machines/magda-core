// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file JsonReader.hpp
 * @brief Checked reads of MAGDA's own JSON documents (#2323).
 *
 * Every field is declared either required or optional at the read. A field that is
 * missing when required, or present with the wrong type, is logged with its path and
 * fails the decode instead of loading as a default.
 */

#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace magda::ser {

/** @brief One field a decode could not use, at its path in the document. */
struct DecodeIssue {
    juce::String path;
    juce::String message;
};

/** @brief The issues one decode collected. */
class DecodeLog {
  public:
    void add(const juce::String& path, const juce::String& message);

    bool ok() const noexcept {
        return issues_.empty();
    }

    const std::vector<DecodeIssue>& issues() const noexcept {
        return issues_;
    }

    /** @brief The first issue as "path: message", and how many more there are. */
    juce::String summary() const;

  private:
    std::vector<DecodeIssue> issues_;
};

/** @brief Convert @p value to @p out when its JSON type fits; false leaves @p out alone. */
bool decodeValue(const juce::var& value, int& out);
bool decodeValue(const juce::var& value, juce::int64& out);
bool decodeValue(const juce::var& value, double& out);
bool decodeValue(const juce::var& value, float& out);
bool decodeValue(const juce::var& value, bool& out);
bool decodeValue(const juce::var& value, juce::String& out);
bool decodeValue(const juce::var& value, std::string& out);
/// Hex ARGB, as `Colour::toDisplayString(true)` writes it.
bool decodeValue(const juce::var& value, juce::Colour& out);

template <typename E>
    requires std::is_enum_v<E>
bool decodeValue(const juce::var& value, E& out) {
    int raw = 0;
    if (!decodeValue(value, raw))
        return false;
    out = static_cast<E>(raw);
    return true;
}

/** @brief The JSON type name of @p value, for an issue's message. */
juce::String jsonTypeName(const juce::var& value);

/** @brief A JSON object being decoded, at its path in the document. */
class ObjectReader {
  public:
    /// Logs an issue at @p path when @p value is not an object; every read is then a no-op.
    ObjectReader(const juce::var& value, juce::String path, DecodeLog& log);

    bool isValid() const noexcept {
        return object_ != nullptr;
    }

    /// The same place in the document over @p replacement: what a migration hands the reader.
    ObjectReader rebased(const juce::var& replacement) const {
        return {replacement, path_, *log_};
    }

    const juce::DynamicObject* dynamicObject() const noexcept {
        return object_.get();
    }

    /// Present and not void.
    bool has(const juce::Identifier& key) const;
    /// The raw value, for fields this reader passes through untouched.
    juce::var get(const juce::Identifier& key) const;

    const juce::String& path() const noexcept {
        return path_;
    }
    juce::String pathOf(const juce::Identifier& key) const;
    DecodeLog& log() const noexcept {
        return *log_;
    }

    /// Log an issue at this object, or at @p key inside it.
    void fail(const juce::String& message) const;
    void fail(const juce::Identifier& key, const juce::String& message) const;

    /**
     * @brief A field every version writes: absent or the wrong type is an issue.
     * @return Whether the field was read.
     */
    template <typename T> bool required(const juce::Identifier& key, T& out) const {
        if (!isValid())
            return false;
        const auto value = object_->getProperty(key);
        if (value.isVoid()) {
            fail(key, "missing");
            return false;
        }
        if (decodeValue(value, out))
            return true;
        fail(key, mismatch(out, value));
        return false;
    }

    /**
     * @brief A field with a declared default: absent leaves @p out as it is.
     * @return Whether the field was read.
     */
    template <typename T> bool optional(const juce::Identifier& key, T& out) const {
        if (!isValid())
            return false;
        const auto value = object_->getProperty(key);
        if (value.isVoid())
            return false;
        if (decodeValue(value, out))
            return true;
        fail(key, mismatch(out, value));
        return false;
    }

    /// A child object that must be there.
    ObjectReader object(const juce::Identifier& key) const;
    /// A child object that may be absent; present and not an object is an issue.
    std::optional<ObjectReader> optionalObject(const juce::Identifier& key) const;

    /**
     * @brief Visit each element of an array field with its path.
     *
     * Absent is an empty array unless @p required; present and not an array is an issue.
     */
    template <typename Visit>
    void forEach(const juce::Identifier& key, Visit&& visit, bool required = false) const {
        const auto* array = arrayAt(key, required);
        if (array == nullptr)
            return;
        for (int i = 0; i < array->size(); ++i)
            visit((*array)[i], pathOf(key) + "[" + juce::String(i) + "]");
    }

    /** @brief Visit each element of an array of objects; an element that is not one is an issue. */
    template <typename Visit>
    void forEachObject(const juce::Identifier& key, Visit&& visit, bool required = false) const {
        forEach(
            key,
            [this, &visit](const juce::var& element, const juce::String& elementPath) {
                ObjectReader item(element, elementPath, *log_);
                if (item.isValid())
                    visit(item);
            },
            required);
    }

    /** @brief Each element of an array of plain values, decoded into @p out. */
    template <typename T>
    void readArray(const juce::Identifier& key, std::vector<T>& out, bool required = false) const {
        forEach(
            key,
            [this, &out](const juce::var& element, const juce::String& elementPath) {
                T value{};
                if (decodeValue(element, value))
                    out.push_back(std::move(value));
                else
                    log_->add(elementPath, mismatch(value, element));
            },
            required);
    }

  private:
    /// Over nothing, logging nothing: what a read below an already-logged issue gets.
    ObjectReader(juce::String path, DecodeLog& log);

    const juce::Array<juce::var>* arrayAt(const juce::Identifier& key, bool required) const;

    template <typename T> static juce::String mismatch(const T&, const juce::var& found) {
        juce::String expected;
        if constexpr (std::is_same_v<T, bool>)
            expected = "a boolean";
        else if constexpr (std::is_same_v<T, juce::String> || std::is_same_v<T, std::string>)
            expected = "a string";
        else if constexpr (std::is_same_v<T, juce::Colour>)
            expected = "a hex colour";
        else if constexpr (std::is_floating_point_v<T>)
            expected = "a number";
        else
            expected = "an integer";
        return "expected " + expected + ", found " + jsonTypeName(found);
    }

    /// Owned: a migration's rewritten copy lives only as long as its reader.
    juce::DynamicObject::Ptr object_;
    juce::String path_;
    DecodeLog* log_;
};

/**
 * @brief Decode one document with @p read.
 * @return False with @p error set to the first issue when any field could not be used.
 */
template <typename Read>
bool decodeDocument(const juce::var& json, const juce::String& rootPath, juce::String& error,
                    Read&& read) {
    DecodeLog log;
    const ObjectReader in(json, rootPath, log);
    if (in.isValid())
        read(in);
    if (log.ok())
        return true;
    error = log.summary();
    return false;
}

}  // namespace magda::ser
