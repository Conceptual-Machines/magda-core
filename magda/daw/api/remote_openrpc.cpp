// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "remote_openrpc.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "remote_api.hpp"
#include "remote_jsonrpc.hpp"

namespace magda::remote {

namespace {

constexpr const char* kOpenRpcVersion = "1.3.2";

struct ErrorEntry {
    /// The component key, and `error.data.code` when the dispatcher sent it.
    juce::String name;
    int code = 0;
    juce::String message;
    /// Transport refusals (malformed JSON, rate limits) carry no `error.data`.
    bool hasData = false;
    /// Can answer a well-formed call to an existing method.
    bool perMethod = false;
};

ErrorEntry entryFor(ErrorCode code) {
    const auto entry = [code](const char* message, bool perMethod) {
        return ErrorEntry{toString(code), jsonRpcCodeFor(code), message, true, perMethod};
    };
    switch (code) {
        case ErrorCode::InvalidRequest:
            return entry("Invalid JSON-RPC framing or unsupported route.", false);
        case ErrorCode::UnknownOperation:
            return entry("Unknown method.", false);
        case ErrorCode::PermissionDenied:
            return entry("The client's grant does not include the method's scope.", true);
        case ErrorCode::ValidationFailed:
            return entry("params did not satisfy the method's input schema.", true);
        case ErrorCode::NotFound:
            return entry("A referenced project entity does not exist.", true);
        case ErrorCode::Conflict:
            return entry("State or revision conflicts with the request.", true);
        case ErrorCode::Timeout:
            return entry("The request missed its deadline.", true);
        case ErrorCode::Cancelled:
            return entry("Work was cancelled.", true);
        case ErrorCode::InternalError:
            break;
    }
    return entry("Unexpected server failure.", true);
}

/// Every error the WebSocket sends, in code order.
std::vector<ErrorEntry> errorEntries() {
    return {
        {"parse_error", JSONRPC_PARSE_ERROR, "Malformed JSON.", false, false},
        entryFor(ErrorCode::InvalidRequest),
        entryFor(ErrorCode::UnknownOperation),
        entryFor(ErrorCode::ValidationFailed),
        entryFor(ErrorCode::InternalError),
        entryFor(ErrorCode::NotFound),
        entryFor(ErrorCode::Conflict),
        entryFor(ErrorCode::Timeout),
        entryFor(ErrorCode::Cancelled),
        {"too_many_requests", JSONRPC_TOO_MANY_REQUESTS,
         "Connection request rate or in-flight limit exceeded.", false, true},
        entryFor(ErrorCode::PermissionDenied),
    };
}

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> properties) {
    auto* result = new juce::DynamicObject();
    for (const auto& [name, value] : properties)
        result->setProperty(name, value);
    return result;
}

juce::var errorObject(const ErrorEntry& entry) {
    auto error = object({{"code", entry.code}, {"message", entry.message}});
    if (entry.hasData)
        error.getDynamicObject()->setProperty("data", object({{"code", entry.name}}));
    return error;
}

juce::var contentDescriptor(const juce::String& name, const juce::var& schema, bool required) {
    return object({{"name", name}, {"required", required}, {"schema", schema.clone()}});
}

/// Top-level keywords whose alternatives can add or require params.
constexpr const char* kAlternativeKeywords[] = {"oneOf", "anyOf"};

bool isRequiredBy(const juce::var& schema, const juce::String& name) {
    const auto* names = schema["required"].getArray();
    return names != nullptr && names->contains(name);
}

bool requiredByEveryAlternative(const juce::var& inputSchema, const juce::String& name) {
    for (const char* keyword : kAlternativeKeywords) {
        const auto* alternatives = inputSchema[keyword].getArray();
        if (alternatives != nullptr && !alternatives->isEmpty() &&
            std::ranges::all_of(*alternatives, [&name](const juce::var& alternative) {
                return isRequiredBy(alternative, name);
            }))
            return true;
    }
    return false;
}

/**
 * @brief One descriptor per property of the input schema or of its top-level alternatives.
 *
 * A name with differing schemas across alternatives gets their `anyOf`. OpenRPC wants every
 * required param before any optional one.
 */
juce::Array<juce::var> paramsFor(const juce::var& inputSchema) {
    struct Param {
        juce::String name;
        juce::Array<juce::var> schemas;
    };
    std::vector<Param> found;
    const auto collect = [&found](const juce::var& schema) {
        const auto* properties = schema["properties"].getDynamicObject();
        if (properties == nullptr)
            return;
        for (const auto& property : properties->getProperties()) {
            const auto name = property.name.toString();
            auto param = std::ranges::find(found, name, &Param::name);
            if (param == found.end())
                param = found.insert(found.end(), Param{name, {}});
            const auto text = juce::JSON::toString(property.value, true);
            const bool known = std::ranges::any_of(param->schemas, [&text](const juce::var& seen) {
                return juce::JSON::toString(seen, true) == text;
            });
            if (!known)
                param->schemas.add(property.value);
        }
    };
    collect(inputSchema);
    for (const char* keyword : kAlternativeKeywords)
        if (const auto* alternatives = inputSchema[keyword].getArray())
            for (const auto& alternative : *alternatives)
                collect(alternative);

    juce::Array<juce::var> required;
    juce::Array<juce::var> optional;
    for (const auto& param : found) {
        const bool isRequired = isRequiredBy(inputSchema, param.name) ||
                                requiredByEveryAlternative(inputSchema, param.name);
        const auto schema =
            param.schemas.size() == 1 ? param.schemas[0] : object({{"anyOf", param.schemas}});
        (isRequired ? required : optional).add(contentDescriptor(param.name, schema, isRequired));
    }
    required.addArray(optional);
    return required;
}

/// Whether by-name descriptors say everything the input schema does.
bool fitsByName(const juce::var& inputSchema) {
    static const juce::StringArray plain{"type", "properties", "required", "additionalProperties"};
    const auto* keywords = inputSchema.getDynamicObject();
    return keywords != nullptr &&
           std::ranges::all_of(keywords->getProperties(),
                               [](const juce::NamedValueSet::NamedValue& keyword) {
                                   return plain.contains(keyword.name.toString());
                               });
}

juce::var methodFor(const juce::var& operation, const std::vector<ErrorEntry>& errors) {
    juce::Array<juce::var> errorRefs;
    for (const auto& entry : errors)
        if (entry.perMethod)
            errorRefs.add(object({{"$ref", "#/components/errors/" + entry.name}}));

    const auto& inputSchema = operation["inputSchema"];
    auto method = object({
        {"name", operation["name"]},
        {"summary", operation["summary"]},
        {"paramStructure", "by-name"},
        {"params", paramsFor(inputSchema)},
        {"result", object({{"name", "result"}, {"schema", operation["outputSchema"].clone()}})},
        {"errors", errorRefs},
        {"x-access", operation["access"]},
        {"x-required-scope", operation["requiredScope"]},
        {"x-transport-scoped", operation["transportScoped"]},
    });
    // Descriptors cannot say "this group or that one", so the exact schema rides along.
    if (!fitsByName(inputSchema))
        method.getDynamicObject()->setProperty("x-params-schema", inputSchema.clone());
    return method;
}

juce::var serverObject() {
    // The listener's port is ephemeral unless config.json pins `remoteApi.port`.
    return object({
        {"name", "MAGDA WebSocket"},
        {"url", "ws://127.0.0.1:${port}/rpc"},
        {"summary", "The loopback listener of a running MAGDA"},
        {"description",
         "Read `url` and `token` from the discovery record `remote-api-<pid>.json` in MAGDA's "
         "data directory. Name the client with `?client=<name>` and send the token as "
         "`Authorization: Bearer <token>`."},
        {"variables",
         object({{"port", object({{"default", "51734"},
                                  {"description", "The discovery record's `port`."}})}})},
    });
}

}  // namespace

juce::var openRpcDocument(const juce::var& description) {
    const auto errors = errorEntries();

    juce::Array<juce::var> methods;
    if (const auto* operations = description["operations"].getArray())
        for (const auto& operation : *operations)
            methods.add(methodFor(operation, errors));

    auto* errorComponents = new juce::DynamicObject();
    for (const auto& entry : errors)
        errorComponents->setProperty(entry.name, errorObject(entry));

    return object({
        {"openrpc", kOpenRpcVersion},
        {"info", object({
                     {"title", "MAGDA Remote API"},
                     {"version", description["apiVersion"]},
                     {"description",
                      "JSON-RPC 2.0 over a loopback WebSocket, generated from MAGDA's operation "
                      "registry; `system.describe` returns the same catalogue at runtime. "
                      "`params` objects are closed: unknown fields are rejected."},
                 })},
        {"servers", juce::Array<juce::var>{serverObject()}},
        {"methods", methods},
        {"components", object({{"errors", juce::var(errorComponents)}})},
    });
}

juce::String openRpcText() {
    const auto document = openRpcDocument(OperationRegistry::instance().describe());
    // JUCE's multi-line JSON ends lines with CRLF on every platform, and JSON escapes a CR inside a
    // string, so every raw CR is a line end. Erased in one pass: String::replace is quadratic here.
    auto text = juce::JSON::toString(document, juce::JSON::FormatOptions{}).toStdString();
    std::erase(text, '\r');
    text += '\n';
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

}  // namespace magda::remote
