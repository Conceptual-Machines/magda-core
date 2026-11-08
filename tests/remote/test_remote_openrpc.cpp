// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <set>

#include "magda/daw/api/remote_api.hpp"
#include "magda/daw/api/remote_jsonrpc.hpp"
#include "magda/daw/api/remote_openrpc.hpp"

namespace {

using namespace magda::remote;

juce::var generatedDocument() {
    return openRpcDocument(OperationRegistry::instance().describe());
}

/// var compares objects by identity; the document holds clones.
bool sameJson(const juce::var& a, const juce::var& b) {
    return juce::JSON::toString(a, true) == juce::JSON::toString(b, true);
}

/// One-based number of the first line where @p a and @p b differ.
int firstDifferingLine(const juce::String& a, const juce::String& b) {
    const auto left = juce::StringArray::fromLines(a);
    const auto right = juce::StringArray::fromLines(b);
    const int common = juce::jmin(left.size(), right.size());
    for (int i = 0; i < common; ++i)
        if (left[i] != right[i])
            return i + 1;
    return common + 1;
}

}  // namespace

TEST_CASE("The committed OpenRPC document matches the operation registry",
          "[remote-api][openrpc]") {
    const auto committed =
        juce::File(MAGDA_REPO_ROOT).getChildFile(juce::String(OPENRPC_DOCUMENT_PATH));
    REQUIRE(committed.existsAsFile());

    // A Windows checkout may translate the file to CRLF.
    const auto onDisk = committed.loadFileAsString().replace("\r\n", "\n");
    const auto generated = openRpcText();
    if (onDisk != generated)
        FAIL(OPENRPC_DOCUMENT_PATH << " is stale from line "
                                   << firstDifferingLine(onDisk, generated)
                                   << ". Regenerate it with `make openrpc`.");
}

TEST_CASE("The OpenRPC document maps every registry operation to a method",
          "[remote-api][openrpc]") {
    const auto document = generatedDocument();
    const auto& operations = OperationRegistry::instance().operations();

    CHECK(document["openrpc"] == "1.3.2");
    CHECK(document["info"]["version"] == juce::String(API_VERSION.data(), API_VERSION.size()));
    CHECK(document["servers"][0]["url"] == "ws://127.0.0.1:${port}/rpc");

    const auto* methods = document["methods"].getArray();
    REQUIRE(methods != nullptr);
    REQUIRE(static_cast<size_t>(methods->size()) == operations.size());

    const auto* errors = document["components"]["errors"].getDynamicObject();
    REQUIRE(errors != nullptr);

    for (size_t index = 0; index < operations.size(); ++index) {
        const auto& operation = operations[index];
        const auto& method = methods->getReference(static_cast<int>(index));
        INFO(operation.name);

        CHECK(method["name"] == operation.name);
        CHECK(method["summary"] == operation.summary);
        CHECK(method["paramStructure"] == "by-name");
        CHECK(method["x-required-scope"] == scopeName(operation.requiredScope));
        CHECK(static_cast<bool>(method["x-transport-scoped"]) == operation.transportScoped);
        CHECK(sameJson(method["result"]["schema"], operation.outputSchema));

        // Any other top-level keyword would be lost from the params; see the generator.
        const auto& input = operation.inputSchema;
        bool hasAlternatives = false;
        for (const auto& keyword : input.getDynamicObject()->getProperties()) {
            const auto name = keyword.name.toString();
            hasAlternatives = hasAlternatives || name == "oneOf" || name == "anyOf";
            CHECK(juce::StringArray{"type", "properties", "required", "additionalProperties",
                                    "oneOf", "anyOf"}
                      .contains(name));
        }
        CHECK(sameJson(method["x-params-schema"], hasAlternatives ? input : juce::var()));

        std::set<juce::String> required;
        if (const auto* names = input["required"].getArray())
            for (const auto& name : *names)
                required.insert(name.toString());

        const auto* properties = input["properties"].getDynamicObject();
        const auto* params = method["params"].getArray();
        REQUIRE(params != nullptr);
        if (!hasAlternatives)
            CHECK(params->size() == (properties ? properties->getProperties().size() : 0));

        std::set<juce::String> seen;
        bool sawOptional = false;
        for (const auto& param : *params) {
            const auto name = param["name"].toString();
            const bool isRequired = param["required"];
            CHECK(seen.insert(name).second);
            CHECK_FALSE((isRequired && sawOptional));
            sawOptional = sawOptional || !isRequired;

            if (!hasAlternatives) {
                REQUIRE(properties != nullptr);
                CHECK(sameJson(param["schema"], properties->getProperty(name)));
                CHECK(isRequired == required.contains(name));
            }
        }

        for (const auto& error : *method["errors"].getArray()) {
            const auto ref = error["$ref"].toString();
            REQUIRE(ref.startsWith("#/components/errors/"));
            CHECK(errors->hasProperty(ref.fromLastOccurrenceOf("/", false, false)));
        }
    }
}

TEST_CASE("OpenRPC error components carry the WebSocket's codes", "[remote-api][openrpc]") {
    const auto document = generatedDocument();
    const auto& errors = document["components"]["errors"];

    const auto codeOf = [&errors](const char* name) {
        return static_cast<int>(errors[name]["code"]);
    };
    CHECK(codeOf("validation_failed") == jsonRpcCodeFor(ErrorCode::ValidationFailed));
    CHECK(errors["validation_failed"]["data"]["code"] == "validation_failed");
    CHECK(codeOf("permission_denied") == JSONRPC_PERMISSION_DENIED);
    CHECK(codeOf("too_many_requests") == JSONRPC_TOO_MANY_REQUESTS);
    CHECK(errors["parse_error"]["data"].isVoid());

    std::set<int> codes;
    for (const auto& error : errors.getDynamicObject()->getProperties())
        CHECK(codes.insert(static_cast<int>(error.value["code"])).second);
    CHECK(codes.size() == 11);
}

TEST_CASE("OpenRPC params merge top-level alternatives", "[remote-api][openrpc]") {
    const auto description = juce::JSON::parse(R"json({"apiVersion":"1.0","operations":[{
        "name":"scenes.delete","summary":"s","access":"write","requiredScope":"edit",
        "transportScoped":false,"outputSchema":{},
        "inputSchema":{"type":"object",
            "properties":{"force":{"type":"boolean"}},
            "anyOf":[{"required":["force"]},{"required":["sceneId"]}],
            "oneOf":[
                {"properties":{"sceneId":{"type":"integer"},"policy":{"const":"fail"}},
                 "required":["sceneId","policy"]},
                {"properties":{"sceneId":{"type":"integer"},"policy":{"const":"deleteClips"}},
                 "required":["sceneId","policy"]}]}}]})json");

    const auto document = openRpcDocument(description);
    const auto& method = document["methods"][0];
    const auto& params = *method["params"].getArray();
    REQUIRE(params.size() == 3);

    CHECK(params[0]["name"] == "sceneId");
    CHECK(static_cast<bool>(params[0]["required"]));
    CHECK(sameJson(params[0]["schema"], juce::JSON::parse(R"({"type":"integer"})")));

    CHECK(params[1]["name"] == "policy");
    CHECK(static_cast<bool>(params[1]["required"]));
    CHECK(sameJson(params[1]["schema"],
                   juce::JSON::parse(R"({"anyOf":[{"const":"fail"},{"const":"deleteClips"}]})")));

    CHECK(params[2]["name"] == "force");
    CHECK_FALSE(static_cast<bool>(params[2]["required"]));
    CHECK(method["x-params-schema"].isObject());
}
