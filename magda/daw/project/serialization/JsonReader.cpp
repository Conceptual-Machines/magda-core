// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "JsonReader.hpp"

#include <cmath>
#include <limits>

namespace magda::ser {

void DecodeLog::add(const juce::String& path, const juce::String& message) {
    issues_.push_back({path, message});
}

juce::String DecodeLog::summary() const {
    if (issues_.empty())
        return {};
    const auto& first = issues_.front();
    auto text =
        (first.path.isEmpty() ? juce::String("document") : first.path) + ": " + first.message;
    if (issues_.size() > 1)
        text << " (and " << juce::String(static_cast<int>(issues_.size() - 1)) << " more)";
    return text;
}

juce::String jsonTypeName(const juce::var& value) {
    if (value.isVoid() || value.isUndefined())
        return "nothing";
    if (value.isBool())
        return "a boolean";
    if (value.isInt() || value.isInt64())
        return "an integer";
    if (value.isDouble())
        return "a number";
    if (value.isString())
        return "a string";
    if (value.isArray())
        return "an array";
    if (value.isObject())
        return "an object";
    return "an unsupported value";
}

namespace {

/// JSON has one number type; JUCE parses one without a fraction as an integer.
std::optional<double> numberOf(const juce::var& value) {
    if (value.isInt() || value.isInt64() || value.isDouble())
        return static_cast<double>(value);
    return std::nullopt;
}

}  // namespace

bool decodeValue(const juce::var& value, int& out) {
    if (value.isInt()) {
        out = static_cast<int>(value);
        return true;
    }
    const auto number = numberOf(value);
    if (!number || *number != std::floor(*number) ||
        *number < static_cast<double>(std::numeric_limits<int>::min()) ||
        *number > static_cast<double>(std::numeric_limits<int>::max()))
        return false;
    out = static_cast<int>(*number);
    return true;
}

bool decodeValue(const juce::var& value, juce::int64& out) {
    if (value.isInt() || value.isInt64()) {
        out = static_cast<juce::int64>(value);
        return true;
    }
    const auto number = numberOf(value);
    if (!number || *number != std::floor(*number))
        return false;
    out = static_cast<juce::int64>(*number);
    return true;
}

bool decodeValue(const juce::var& value, double& out) {
    const auto number = numberOf(value);
    if (!number)
        return false;
    out = *number;
    return true;
}

bool decodeValue(const juce::var& value, float& out) {
    const auto number = numberOf(value);
    if (!number)
        return false;
    out = static_cast<float>(*number);
    return true;
}

bool decodeValue(const juce::var& value, bool& out) {
    // Integers too: a flag some builds wrote from an int member.
    if (!value.isBool() && !value.isInt() && !value.isInt64())
        return false;
    out = static_cast<bool>(value);
    return true;
}

bool decodeValue(const juce::var& value, juce::String& out) {
    if (!value.isString())
        return false;
    out = value.toString();
    return true;
}

bool decodeValue(const juce::var& value, std::string& out) {
    if (!value.isString())
        return false;
    out = value.toString().toStdString();
    return true;
}

bool decodeValue(const juce::var& value, juce::Colour& out) {
    if (!value.isString())
        return false;
    // A leading # is what JUCE's own parser skips.
    const auto text = value.toString().trimCharactersAtStart("#");
    if (text.isEmpty() || text.length() > 8 || !text.containsOnly("0123456789abcdefABCDEF"))
        return false;
    out = juce::Colour::fromString(text);
    return true;
}

ObjectReader::ObjectReader(const juce::var& value, juce::String path, DecodeLog& log)
    : path_(std::move(path)), log_(&log) {
    if (value.isObject())
        object_ = value.getDynamicObject();
    if (object_ == nullptr)
        log.add(path_, "expected an object, found " + jsonTypeName(value));
}

ObjectReader::ObjectReader(juce::String path, DecodeLog& log)
    : path_(std::move(path)), log_(&log) {}

bool ObjectReader::has(const juce::Identifier& key) const {
    return isValid() && !object_->getProperty(key).isVoid();
}

juce::var ObjectReader::get(const juce::Identifier& key) const {
    return isValid() ? object_->getProperty(key) : juce::var();
}

juce::String ObjectReader::pathOf(const juce::Identifier& key) const {
    return path_.isEmpty() ? key.toString() : path_ + "." + key.toString();
}

void ObjectReader::fail(const juce::String& message) const {
    log_->add(path_, message);
}

void ObjectReader::fail(const juce::Identifier& key, const juce::String& message) const {
    log_->add(pathOf(key), message);
}

ObjectReader ObjectReader::object(const juce::Identifier& key) const {
    if (!isValid())
        return ObjectReader(pathOf(key), *log_);
    if (!has(key)) {
        fail(key, "missing");
        return ObjectReader(pathOf(key), *log_);
    }
    return ObjectReader(get(key), pathOf(key), *log_);
}

std::optional<ObjectReader> ObjectReader::optionalObject(const juce::Identifier& key) const {
    if (!has(key))
        return std::nullopt;
    ObjectReader child(get(key), pathOf(key), *log_);
    if (!child.isValid())
        return std::nullopt;
    return child;
}

const juce::Array<juce::var>* ObjectReader::arrayAt(const juce::Identifier& key,
                                                    bool required) const {
    if (!isValid())
        return nullptr;
    const auto value = object_->getProperty(key);
    if (value.isVoid()) {
        if (required)
            fail(key, "missing");
        return nullptr;
    }
    if (!value.isArray()) {
        fail(key, "expected an array, found " + jsonTypeName(value));
        return nullptr;
    }
    return value.getArray();
}

}  // namespace magda::ser
