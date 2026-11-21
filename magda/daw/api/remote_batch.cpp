// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <optional>
#include <vector>

#include "magda_api.hpp"
#include "remote_handlers.hpp"
#include "remote_scopes.hpp"
#include "undo_api.hpp"

namespace magda::remote {

const std::vector<juce::String>& batchStepOperations() {
    // Edits whose handlers record every change as an undo command, so a failed
    // batch can be rolled back in full (docs/architecture/remote-api-batch-decision.md).
    static const std::vector<juce::String> names{
        "tracks.create",
        "tracks.update",
        "clips.createMidi",
        "clips.addMidiEvents",
        "clips.replaceMidiEvents",
        "clips.updateMidiEvents",
        "clips.deleteMidiEvents",
        "clips.update",
        "clips.duplicate",
        "clips.transpose",
        "clips.move",
        "clips.resize",
        "sends.create",
        "sends.update",
        "project.setLoopRange",
    };
    return names;
}

namespace handlers {

namespace {

constexpr int kMaxBatchMidiEvents = 16384;

struct Reference {
    int step = -1;
    juce::StringArray path;
};

/// `{"$ref": "N.field"}`, the only shape a reference takes.
std::optional<juce::String> referenceText(const juce::var& value) {
    const auto* object = value.getDynamicObject();
    if (object == nullptr || object->getProperties().size() != 1 || !object->hasProperty("$ref"))
        return std::nullopt;
    return value["$ref"].toString();
}

std::optional<Reference> parseReference(const juce::String& text) {
    auto parts = juce::StringArray::fromTokens(text, ".", "");
    if (parts.size() < 2 || !parts[0].containsOnly("0123456789") || parts[0].isEmpty())
        return std::nullopt;
    Reference reference{parts[0].getIntValue(), {}};
    parts.remove(0);
    if (parts.contains(""))
        return std::nullopt;
    reference.path = parts;
    return reference;
}

/// A copy of @p value with every reference replaced by what @p resolve answers;
/// an empty var from @p resolve stops the walk and the copy is void.
template <typename Resolve> juce::var substitute(const juce::var& value, Resolve& resolve) {
    if (const auto text = referenceText(value))
        return resolve(*text);
    if (const auto* items = value.getArray()) {
        juce::Array<juce::var> copy;
        for (const auto& item : *items) {
            auto replaced = substitute(item, resolve);
            if (replaced.isVoid())
                return {};
            copy.add(replaced);
        }
        return copy;
    }
    if (const auto* object = value.getDynamicObject()) {
        auto* copy = new juce::DynamicObject();
        const juce::var owner(copy);
        for (const auto& property : object->getProperties()) {
            auto replaced = substitute(property.value, resolve);
            if (replaced.isVoid())
                return {};
            copy->setProperty(property.name, replaced);
        }
        return owner;
    }
    return value;
}

juce::var walk(juce::var node, const juce::StringArray& path, bool schema) {
    for (const auto& key : path) {
        node = schema ? node["properties"][juce::Identifier(key)] : node[juce::Identifier(key)];
        if (node.isVoid())
            return {};
    }
    return node;
}

HandlerResult stepFailure(int index, const juce::String& method, Error error) {
    error.message = "step " + juce::String(index) + " (" + method + "): " + error.message;
    for (auto& issue : error.issues)
        issue.path = "$.steps[" + juce::String(index) + "].params" + issue.path.substring(1);
    return HandlerResult::fail(std::move(error));
}

}  // namespace

HandlerResult batchApply(MagdaApi& api, const juce::var& input, const RequestContext& context) {
    const auto& registry = OperationRegistry::instance();
    const auto& steps = *input["steps"].getArray();
    std::vector<const OperationDescriptor*> operations;
    int midiEvents = 0;

    // Every step is checked as if sent alone before anything is applied.
    for (int index = 0; index < steps.size(); ++index) {
        const auto method = steps[index]["method"].toString();
        const auto* operation = registry.find(method);
        if (operation == nullptr || !std::ranges::contains(batchStepOperations(), method))
            return stepFailure(index, method,
                               {ErrorCode::ValidationFailed, "not allowed in a batch"});
        if (!context.scopes.has(operation->requiredScope))
            return stepFailure(index, method,
                               {ErrorCode::PermissionDenied,
                                "requires the '" + scopeName(operation->requiredScope) +
                                    "' permission, which this client has not been granted"});

        std::optional<Error> referenceError;
        auto placeholder = [&](const juce::String& text) -> juce::var {
            const auto reference = parseReference(text);
            if (!reference || reference->step >= index) {
                referenceError =
                    Error{ErrorCode::ValidationFailed,
                          "reference '" + text + "' must name an earlier step's field"};
                return {};
            }
            const auto type =
                walk(operations[static_cast<std::size_t>(reference->step)]->outputSchema,
                     reference->path, true)["type"]
                    .toString();
            if (type == "integer")
                return 0;
            if (type == "string")
                return "ref";
            referenceError = Error{ErrorCode::ValidationFailed,
                                   "reference '" + text + "' names no id or string field"};
            return {};
        };
        const auto params = substitute(steps[index]["params"], placeholder);
        if (referenceError)
            return stepFailure(index, method, *referenceError);
        if (auto error = validateOperationInput(*operation, params))
            return stepFailure(index, method, *error);

        if (method.endsWith("MidiEvents") && params["events"].isArray())
            midiEvents += params["events"].getArray()->size();
        if (midiEvents > kMaxBatchMidiEvents)
            return stepFailure(index, method,
                               {ErrorCode::ValidationFailed, "the batch carries more than " +
                                                                 juce::String(kMaxBatchMidiEvents) +
                                                                 " MIDI events"});
        operations.push_back(operation);
    }

    std::vector<juce::var> results;
    bool mutated = false;
    const auto rollBack = [&](int index, Error error) {
        api.undo().rollbackCompound();
        return stepFailure(index, steps[index]["method"].toString(), std::move(error));
    };
    for (int index = 0; index < steps.size(); ++index) {
        juce::String missing;
        auto resolve = [&](const juce::String& text) {
            const auto reference = parseReference(text);
            auto value =
                walk(results[static_cast<std::size_t>(reference->step)], reference->path, false);
            if (value.isVoid())
                missing = text;
            return value;
        };
        const auto params = substitute(steps[index]["params"], resolve);
        if (missing.isNotEmpty())
            return rollBack(index,
                            {ErrorCode::NotFound, "reference '" + missing + "' has no value"});

        auto result = operations[static_cast<std::size_t>(index)]->handler(api, params, context);
        if (result.failed())
            return rollBack(index, *result.error);
        mutated = mutated || result.mutated;
        results.push_back(result.value);
    }

    juce::Array<juce::var> summaries;
    for (const auto& result : results)
        summaries.add(summaryIds(result));
    auto* output = new juce::DynamicObject();
    // The dispatcher commits a mutated write as exactly one revision.
    output->setProperty("revision", static_cast<juce::int64>(context.revision + (mutated ? 1 : 0)));
    output->setProperty("results", summaries);
    return mutated ? HandlerResult::ok(output) : HandlerResult::unchanged(output);
}

}  // namespace handlers
}  // namespace magda::remote
