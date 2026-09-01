#include "remote_api.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "remote_handlers.hpp"

namespace magda::remote {
namespace {

constexpr int kDefaultMaxStringLength = 16 * 1024;
constexpr int kDefaultMaxArrayItems = 4096;

bool schemaDeclaresType(const juce::var& schema, const char* expected) {
    auto* object = schema.getDynamicObject();
    if (object == nullptr)
        return false;
    const auto type = object->getProperty("type");
    if (type.isString())
        return type.toString() == expected;
    if (auto* types = type.getArray())
        return std::any_of(types->begin(), types->end(),
                           [&](const auto& candidate) { return candidate.toString() == expected; });
    return false;
}

// Every integer field is bounded to the C++ int range so an int64 value can
// never truncate on decode; ids additionally get a non-negative floor so a
// negative int64 cannot alias a valid id through modular wrap. Explicit
// bounds are honoured (the -1 index sentinels declare minimum -1), and const
// and enum schemas already pin their values (the master track sentinel -2
// uses an explicit const/anyOf form), so those are left untouched.
void applyIntegerBounds(juce::var schema, const juce::String& propertyName = {}) {
    auto* object = schema.getDynamicObject();
    if (object == nullptr)
        return;

    if (schemaDeclaresType(schema, "integer") && !object->hasProperty("const") &&
        !object->hasProperty("enum")) {
        const bool isId =
            propertyName == "id" || propertyName.endsWith("Id") || propertyName.endsWith("Ids");
        if (!object->hasProperty("minimum"))
            object->setProperty("minimum", isId ? 0 : std::numeric_limits<int>::min());
        if (!object->hasProperty("maximum"))
            object->setProperty("maximum", std::numeric_limits<int>::max());
    }

    if (auto* properties = object->getProperty("properties").getDynamicObject())
        for (const auto& property : properties->getProperties())
            applyIntegerBounds(property.value, property.name.toString());

    const auto items = object->getProperty("items");
    if (!items.isVoid())
        applyIntegerBounds(items, propertyName);

    for (const char* keyword : {"anyOf", "oneOf"})
        if (auto* alternatives = object->getProperty(keyword).getArray())
            for (auto& alternative : *alternatives)
                applyIntegerBounds(alternative, propertyName);
}

// Blanket DoS caps for request payloads. Response schemas must stay valid for
// anything the model can hold (a dense MIDI clip easily exceeds 4096 notes),
// so these defaults are applied to operation input schemas only; output
// collections carry explicit caps where a real limit exists.
void applyRequestLimits(juce::var schema) {
    auto* object = schema.getDynamicObject();
    if (object == nullptr)
        return;

    if (schemaDeclaresType(schema, "string") && !object->hasProperty("maxLength"))
        object->setProperty("maxLength", kDefaultMaxStringLength);
    if (schemaDeclaresType(schema, "array") && !object->hasProperty("maxItems"))
        object->setProperty("maxItems", kDefaultMaxArrayItems);

    if (auto* properties = object->getProperty("properties").getDynamicObject())
        for (const auto& property : properties->getProperties())
            applyRequestLimits(property.value);

    const auto items = object->getProperty("items");
    if (!items.isVoid())
        applyRequestLimits(items);

    for (const char* keyword : {"anyOf", "oneOf"})
        if (auto* alternatives = object->getProperty(keyword).getArray())
            for (auto& alternative : *alternatives)
                applyRequestLimits(alternative);
}

juce::var parseSchema(const char* json) {
    auto schema = juce::JSON::parse(juce::String::fromUTF8(json));
    applyIntegerBounds(schema);
    return schema;
}

juce::var emptyObjectSchema() {
    return parseSchema(R"json({
        "type":"object","properties":{},"additionalProperties":false
    })json");
}

juce::var arraySchema(const juce::var& itemSchema) {
    auto schema = new juce::DynamicObject();
    schema->setProperty("type", "array");
    schema->setProperty("items", itemSchema);
    return schema;
}

const juce::var& midiNoteSchema() {
    static const auto value = parseSchema(R"json({
        "type":"object",
        "properties":{
            "note":{"type":"integer","minimum":0,"maximum":127},
            "velocity":{"type":"integer","minimum":0,"maximum":127},
            "startBeat":{"type":"number","minimum":0},
            "lengthBeats":{"type":"number","exclusiveMinimum":0}
        },
        "required":["note","velocity","startBeat","lengthBeats"],
        "additionalProperties":false
    })json");
    return value;
}

const juce::var& projectSchema() {
    static const auto value = parseSchema(R"json({
        "type":"object",
        "properties":{
            "name":{"type":"string"},
            "tempo":{"type":"number","minimum":20,"maximum":400},
            "timeSignatureNumerator":{"type":"integer","minimum":1,"maximum":32},
            "timeSignatureDenominator":{"type":"integer","enum":[1,2,4,8,16,32]},
            "sampleRate":{"type":"number","exclusiveMinimum":0},
            "timelineLengthBars":{"type":"integer","minimum":1},
            "keyRoot":{"type":"integer","minimum":-1,"maximum":11},
            "keyQuality":{"type":"string","enum":["major","minor"]},
            "loopEnabled":{"type":"boolean"},
            "loopStartBeats":{"type":"number","minimum":0},
            "loopEndBeats":{"type":"number","minimum":0}
        },
        "required":["name","tempo","timeSignatureNumerator","timeSignatureDenominator",
                    "sampleRate","timelineLengthBars","keyRoot","keyQuality","loopEnabled",
                    "loopStartBeats","loopEndBeats"],
        "additionalProperties":false
    })json");
    return value;
}

const juce::var& trackSchema() {
    static const auto value = parseSchema(R"json({
        "type":"object",
        "properties":{
            "id":{"anyOf":[{"type":"integer","const":-2},
                           {"type":"integer","minimum":0}]},
            "type":{"type":"string","enum":["audio","group","aux","master","multi_out","chord"]},
            "name":{"type":"string"},
            "colourArgb":{"type":"integer","minimum":0,"maximum":4294967295},
            "parentId":{"type":["integer","null"]},
            "childIds":{"type":"array","items":{"type":"integer","minimum":0}},
            "volume":{"type":"number","minimum":0},
            "pan":{"type":"number","minimum":-1,"maximum":1},
            "muted":{"type":"boolean"},
            "soloed":{"type":"boolean"},
            "recordArmed":{"type":"boolean"},
            "frozen":{"type":"boolean"},
            "audioInputDevice":{"type":"string"},
            "midiInputDevice":{"type":"string"},
            "audioOutputDevice":{"type":"string"},
            "midiOutputDevice":{"type":"string"}
        },
        "required":["id","type","name","colourArgb","parentId","childIds","volume","pan",
                    "muted","soloed","recordArmed","frozen","audioInputDevice","midiInputDevice",
                    "audioOutputDevice","midiOutputDevice"],
        "additionalProperties":false
    })json");
    return value;
}

const juce::var& clipSchema() {
    static auto value = [] {
        auto schema = parseSchema(R"json({
            "type":"object",
            "properties":{
                "id":{"type":"integer","minimum":0},
                "trackId":{"type":"integer","minimum":0},
                "type":{"type":"string","enum":["audio","midi"]},
                "view":{"type":"string","enum":["arrangement","session"]},
                "name":{"type":"string"},
                "colourArgb":{"type":"integer","minimum":0,"maximum":4294967295},
                "startBeat":{"type":"number","minimum":0},
                "lengthBeats":{"type":"number","minimum":0},
                "enabled":{"type":"boolean"},
                "sceneIndex":{"type":["integer","null"],"minimum":0},
                "launchMode":{"type":"string","enum":["trigger","toggle"]},
                "launchQuantize":{"type":"string","enum":["none","8_bars","4_bars","2_bars",
                    "1_bar","1/2","1/4","1/8","1/16"]},
                "followAction":{"type":"string","enum":["none","next","previous","random","stop","again"]},
                "notes":{"type":"array","maxItems":100000}
            },
            "required":["id","trackId","type","view","name","colourArgb","startBeat","lengthBeats",
                        "enabled","sceneIndex","launchMode","launchQuantize","followAction","notes"],
            "additionalProperties":false
        })json");
        schema.getDynamicObject()
            ->getProperty("properties")
            .getDynamicObject()
            ->getProperty("notes")
            .getDynamicObject()
            ->setProperty("items", midiNoteSchema());
        return schema;
    }();
    return value;
}

const juce::var& devicePathSchema() {
    static const auto value = parseSchema(R"json({
        "type":"object",
        "properties":{
            "trackId":{"anyOf":[{"type":"integer","const":-2},
                                {"type":"integer","minimum":0}]},
            "section":{"type":"string","enum":["fx","post_fx","mixer_analysis"]},
            "trackLevel":{"type":"boolean"},
            "topLevelDeviceId":{"type":["integer","null"],"minimum":0},
            "steps":{"type":"array","items":{
                "type":"object",
                "properties":{
                    "type":{"type":"string","enum":["rack","chain","device","pad_rack","pad_chain"]},
                    "id":{"type":"integer","minimum":0}
                },
                "required":["type","id"],
                "additionalProperties":false
            }}
        },
        "required":["trackId","section","trackLevel","topLevelDeviceId","steps"],
        "additionalProperties":false
    })json");
    return value;
}

const juce::var& deviceSchema() {
    static auto value = [] {
        auto schema = parseSchema(R"json({
            "type":"object",
            "properties":{
                "id":{"type":"integer","minimum":0},
                "trackId":{"type":"integer","minimum":0},
                "rackId":{"type":["integer","null"]},
                "chainId":{"type":["integer","null"]},
                "devicePath":{},
                "name":{"type":"string"},
                "type":{"type":"string","enum":["instrument","effect","midi","analysis"]},
                "format":{"type":"string","enum":["vst3","au","lv2","internal"]},
                "instrument":{"type":"boolean"},
                "bypassed":{"type":"boolean"},
                "gainDb":{"type":"number"}
            },
            "required":["id","trackId","rackId","chainId","devicePath","name","type","format",
                        "instrument","bypassed","gainDb"],
            "additionalProperties":false
        })json");
        schema["properties"].getDynamicObject()->setProperty("devicePath", devicePathSchema());
        return schema;
    }();
    return value;
}

const juce::var& chainSchema() {
    static const auto value = parseSchema(R"json({
        "type":"object",
        "properties":{
            "id":{"type":"integer","minimum":0},
            "rackId":{"type":"integer","minimum":0},
            "name":{"type":"string"},
            "outputIndex":{"type":"integer","minimum":0},
            "muted":{"type":"boolean"},
            "solo":{"type":"boolean"},
            "bypassed":{"type":"boolean"},
            "volumeDb":{"type":"number"},
            "pan":{"type":"number","minimum":-1,"maximum":1},
            "deviceIds":{"type":"array","items":{"type":"integer","minimum":0}},
            "nestedRackIds":{"type":"array","items":{"type":"integer","minimum":0}}
        },
        "required":["id","rackId","name","outputIndex","muted","solo","bypassed","volumeDb",
                    "pan","deviceIds","nestedRackIds"],
        "additionalProperties":false
    })json");
    return value;
}

const juce::var& rackSchema() {
    static const auto value = parseSchema(R"json({
        "type":"object",
        "properties":{
            "id":{"type":"integer","minimum":0},
            "trackId":{"type":"integer","minimum":0},
            "parentRackId":{"type":["integer","null"]},
            "parentChainId":{"type":["integer","null"]},
            "name":{"type":"string"},
            "bypassed":{"type":"boolean"},
            "volumeDb":{"type":"number"},
            "pan":{"type":"number","minimum":-1,"maximum":1},
            "chainIds":{"type":"array","items":{"type":"integer","minimum":0}}
        },
        "required":["id","trackId","parentRackId","parentChainId","name","bypassed",
                    "volumeDb","pan","chainIds"],
        "additionalProperties":false
    })json");
    return value;
}

const juce::var& deviceGraphSchema() {
    static auto value = [] {
        auto schema = parseSchema(R"json({
            "type":"object",
            "properties":{
                "devices":{"type":"array"},
                "racks":{"type":"array"},
                "chains":{"type":"array"}
            },
            "required":["devices","racks","chains"],
            "additionalProperties":false
        })json");
        auto* properties = schema["properties"].getDynamicObject();
        properties->getProperty("devices").getDynamicObject()->setProperty("items", deviceSchema());
        properties->getProperty("racks").getDynamicObject()->setProperty("items", rackSchema());
        properties->getProperty("chains").getDynamicObject()->setProperty("items", chainSchema());
        return schema;
    }();
    return value;
}

const juce::var& deviceParameterSchema() {
    static const auto value = parseSchema(R"json({
        "type":"object",
        "properties":{
            "index":{"type":"integer","minimum":0},
            "stableId":{"type":"string"},
            "name":{"type":"string"},
            "unit":{"type":"string"},
            "minValue":{"type":"number"},
            "maxValue":{"type":"number"},
            "defaultValue":{"type":"number"},
            "currentValue":{"type":"number"},
            "normalizedValue":{"type":"number","minimum":0,"maximum":1},
            "visible":{"type":"boolean"},
            "miniMixer":{"type":"boolean"},
            "aiAgentEnabled":{"type":"boolean"}
        },
        "required":["index","stableId","name","unit","minValue","maxValue","defaultValue",
                    "currentValue","normalizedValue","visible","miniMixer","aiAgentEnabled"],
        "additionalProperties":false
    })json");
    return value;
}

const juce::var& deviceCatalogEntrySchema() {
    static const auto value = parseSchema(R"json({
        "type":"object",
        "properties":{
            "catalogId":{"type":"string"},
            "name":{"type":"string"},
            "manufacturer":{"type":"string"},
            "category":{"type":"string"},
            "description":{"type":"string"},
            "format":{"type":"string","enum":["vst3","au","lv2","internal"]},
            "type":{"type":"string","enum":["instrument","effect","midi","analysis"]},
            "instrument":{"type":"boolean"}
        },
        "required":["catalogId","name","manufacturer","category","description","format","type",
                    "instrument"],
        "additionalProperties":false
    })json");
    return value;
}

const juce::var& selectionSchema() {
    static const auto value = parseSchema(R"json({
        "type":"object",
        "properties":{
            "trackId":{"anyOf":[{"type":"null"},{"type":"integer","const":-2},
                                {"type":"integer","minimum":0}]},
            "clipId":{"type":["integer","null"],"minimum":0},
            "clipIds":{"type":"array","items":{"type":"integer","minimum":0}},
            "automationLaneId":{"type":["integer","null"],"minimum":0},
            "automationClipId":{"type":["integer","null"],"minimum":0},
            "noteClipId":{"type":["integer","null"],"minimum":0},
            "noteIndices":{"type":"array","items":{"type":"integer","minimum":0}}
        },
        "required":["trackId","clipId","clipIds","automationLaneId","automationClipId",
                    "noteClipId","noteIndices"],
        "additionalProperties":false
    })json");
    return value;
}

const juce::var& transportSchema() {
    static const auto value = parseSchema(R"json({
        "type":"object",
        "properties":{
            "playing":{"type":"boolean"},
            "recording":{"type":"boolean"},
            "loopEnabled":{"type":"boolean"},
            "positionBeats":{"type":"number","minimum":0}
        },
        "required":["playing","recording","loopEnabled","positionBeats"],
        "additionalProperties":false
    })json");
    return value;
}

const juce::var& sessionSlotSchema() {
    static const auto value = parseSchema(R"json({
        "type":"object",
        "properties":{
            "trackId":{"type":"integer","minimum":0},
            "sceneIndex":{"type":"integer","minimum":0},
            "clipId":{"type":"integer","minimum":0},
            "state":{"type":"string","enum":["stopped","queued","playing"]}
        },
        "required":["trackId","sceneIndex","clipId","state"],
        "additionalProperties":false
    })json");
    return value;
}

const juce::var& sessionSchema() {
    static auto value = [] {
        auto schema = parseSchema(R"json({
            "type":"object","properties":{"slots":{"type":"array"}},"required":["slots"],
            "additionalProperties":false
        })json");
        schema["properties"]["slots"].getDynamicObject()->setProperty("items", sessionSlotSchema());
        return schema;
    }();
    return value;
}

const juce::var& automationTargetSchema() {
    static auto value = [] {
        auto schema = parseSchema(R"json({
            "type":"object",
            "properties":{
                "kind":{"type":"string","enum":["plugin_param","device_macro","mod_param",
                    "track_volume","track_pan","send_level","tempo"]},
                "devicePath":{},
                "parameterIndex":{"type":"integer","minimum":-1},
                "modId":{"type":"integer","minimum":-1},
                "modParameterIndex":{"type":"integer","minimum":-1},
                "sendBusIndex":{"type":"integer","minimum":-1}
            },
            "required":["kind","devicePath","parameterIndex","modId","modParameterIndex",
                        "sendBusIndex"],
            "additionalProperties":false
        })json");
        // Edit-scoped targets (tempo) carry no path, so null is permitted.
        auto* nullable = new juce::DynamicObject();
        juce::Array<juce::var> alternatives;
        alternatives.add(parseSchema(R"json({"type":"null"})json"));
        alternatives.add(devicePathSchema());
        nullable->setProperty("anyOf", juce::var(alternatives));
        schema["properties"].getDynamicObject()->setProperty("devicePath", juce::var(nullable));
        return schema;
    }();
    return value;
}

const juce::var& automationPointSchema() {
    static const auto value = parseSchema(R"json({
        "type":"object",
        "properties":{
            "id":{"type":"integer","minimum":0},
            "beatPosition":{"type":"number","minimum":0},
            "value":{"type":"number","minimum":0,"maximum":1},
            "curve":{"type":"string","enum":["linear","bezier","step","hard_corner"]}
        },
        "required":["id","beatPosition","value","curve"],
        "additionalProperties":false
    })json");
    return value;
}

const juce::var& automationLaneSchema() {
    static auto value = [] {
        auto schema = parseSchema(R"json({
            "type":"object",
            "properties":{
                "id":{"type":"integer","minimum":0},
                "type":{"type":"string","enum":["absolute","clip_based"]},
                "name":{"type":"string"},
                "target":{},
                "points":{"type":"array","maxItems":100000},
                "clipIds":{"type":"array","items":{"type":"integer","minimum":0}}
            },
            "required":["id","type","name","target","points","clipIds"],
            "additionalProperties":false
        })json");
        auto* properties = schema["properties"].getDynamicObject();
        properties->setProperty("target", automationTargetSchema());
        properties->getProperty("points").getDynamicObject()->setProperty("items",
                                                                          automationPointSchema());
        return schema;
    }();
    return value;
}

// ---------------------------------------------------------------------------
// Subscription schemas (#1857)
// ---------------------------------------------------------------------------

/// The topic names, spelled once. Kept in step with `magda::remote::Topic` by
/// the round-trip test over `parseTopic`, which fails the moment the two drift.
const char* kTopicEnumJson =
    R"json({"type":"array","minItems":1,"maxItems":10,
            "items":{"type":"string","enum":["project","tracks","clips","devices","selection",
                                             "transport","session","automation","meters",
                                             "playhead"]}})json";

juce::var topicListSchema() {
    return parseSchema(kTopicEnumJson);
}

/// `{"topics":[…]}` with topics optional — absent means every subscribed topic.
juce::var topicSelectionSchema() {
    auto schema = parseSchema(R"json({
        "type":"object","properties":{"topics":{}},"additionalProperties":false
    })json");
    schema["properties"].getDynamicObject()->setProperty("topics", topicListSchema());
    return schema;
}

/**
 * @brief `{"topics":[…], "snapshot": true}`.
 *
 * No revision to resume from. A project revision counts committed mutations,
 * and subscription events are also published for motion that commits nothing,
 * so naming a revision cannot establish that a client saw every event up to it.
 * `snapshot:false` is the deliberate opt-out — the client asserting it has state
 * and will resync itself — rather than the server guessing on its behalf.
 */
juce::var subscribeInputSchema() {
    auto schema = parseSchema(R"json({
        "type":"object",
        "properties":{
            "topics":{},
            "snapshot":{"type":"boolean"}
        },
        "required":["topics"],"additionalProperties":false
    })json");
    schema["properties"].getDynamicObject()->setProperty("topics", topicListSchema());
    return schema;
}

/**
 * @brief The pushed envelope, published so a client can generate against it.
 *
 * `payload` is deliberately unconstrained: for a snapshot it is the matching
 * read operation's output, for a delta it is `{added,updated,removed}` or that
 * same output, and for a sample it is a meter or playhead reading. Declaring one
 * shape here would be declaring a false one.
 */
const juce::var& subscriptionEventSchema() {
    static const auto value = parseSchema(R"json({
        "type":"object",
        "properties":{
            "topic":{"type":"string"},
            "type":{"type":"string","enum":["snapshot","delta","sample"]},
            "revision":{"type":"integer","minimum":0},
            "payload":{}
        },
        "required":["topic","type","revision","payload"],
        "additionalProperties":false
    })json");
    return value;
}

/// `withSnapshots` distinguishes the two methods that hand back initial state
/// in their reply from the two that only report what is subscribed.
juce::var subscriptionResultSchema(bool withSnapshots) {
    auto schema = parseSchema(R"json({
        "type":"object",
        "properties":{
            "topics":{"type":"array","items":{"type":"string"}},
            "revision":{"type":"integer","minimum":0}
        },
        "required":["topics","revision"],
        "additionalProperties":false
    })json");
    if (!withSnapshots)
        return schema;

    auto* properties = schema["properties"].getDynamicObject();
    properties->setProperty("snapshots", arraySchema(subscriptionEventSchema()));
    schema.getDynamicObject()->setProperty(
        "required", juce::var(juce::Array<juce::var>{juce::var("topics"), juce::var("revision"),
                                                     juce::var("snapshots")}));
    return schema;
}

bool matchesType(const juce::var& value, const juce::String& type) {
    if (type == "object")
        return value.getDynamicObject() != nullptr;
    if (type == "array")
        return value.isArray();
    if (type == "string")
        return value.isString();
    if (type == "boolean")
        return value.isBool();
    if (type == "integer")
        return value.isInt() || value.isInt64();
    if (type == "number")
        return value.isInt() || value.isInt64() || value.isDouble();
    if (type == "null")
        return value.isVoid();
    return true;
}

bool typeMatches(const juce::var& value, const juce::var& declaredType) {
    if (declaredType.isString())
        return matchesType(value, declaredType.toString());
    if (auto* types = declaredType.getArray()) {
        return std::any_of(types->begin(), types->end(),
                           [&](const auto& type) { return matchesType(value, type.toString()); });
    }
    return true;
}

juce::String typeDescription(const juce::var& declaredType) {
    if (declaredType.isString())
        return declaredType.toString();
    juce::StringArray types;
    if (auto* array = declaredType.getArray())
        for (const auto& type : *array)
            types.add(type.toString());
    return types.joinIntoString("|");
}

void addIssue(std::vector<ValidationIssue>& issues, const juce::String& path,
              const juce::String& code, const juce::String& message) {
    issues.push_back({path, code, message});
}

void validateValue(const juce::var& value, const juce::var& schema, const juce::String& path,
                   std::vector<ValidationIssue>& issues) {
    auto* schemaObject = schema.getDynamicObject();
    if (schemaObject == nullptr)
        return;

    if (auto* alternatives = schemaObject->getProperty("anyOf").getArray()) {
        std::vector<ValidationIssue> branchIssues;
        for (int index = 0; index < alternatives->size(); ++index) {
            std::vector<ValidationIssue> candidateIssues;
            validateValue(value, (*alternatives)[index],
                          path + "<anyOf:" + juce::String(index) + ">", candidateIssues);
            if (candidateIssues.empty())
                return;
            branchIssues.insert(branchIssues.end(), candidateIssues.begin(), candidateIssues.end());
        }
        addIssue(issues, path, "any_of", "Value does not match any allowed schema");
        issues.insert(issues.end(), branchIssues.begin(), branchIssues.end());
        return;
    }

    // Exactly one branch, where anyOf wants at least one. Implemented rather
    // than ignored: an unknown keyword is silently dropped here, so a schema
    // that declared oneOf would advertise a constraint nothing enforced, and
    // both "neither field" and "both fields" would reach the handler.
    if (auto* alternatives = schemaObject->getProperty("oneOf").getArray()) {
        int matches = 0;
        std::vector<ValidationIssue> branchIssues;

        for (int index = 0; index < alternatives->size(); ++index) {
            std::vector<ValidationIssue> candidateIssues;
            validateValue(value, (*alternatives)[index],
                          path + "<oneOf:" + juce::String(index) + ">", candidateIssues);
            if (candidateIssues.empty())
                ++matches;
            else
                branchIssues.insert(branchIssues.end(), candidateIssues.begin(),
                                    candidateIssues.end());
        }

        // Only a failure ends validation here. Unlike anyOf, whose branches
        // stand for the whole schema, oneOf sits beside properties, required
        // and additionalProperties on the same object -- returning on success
        // would skip every one of them, so the exclusivity would be enforced
        // and the field bounds silently would not.
        if (matches != 1) {
            if (matches == 0) {
                addIssue(issues, path, "one_of", "Value does not match any allowed schema");
                issues.insert(issues.end(), branchIssues.begin(), branchIssues.end());
            } else {
                addIssue(issues, path, "one_of",
                         "Value matches " + juce::String(matches) +
                             " allowed schemas, but must match exactly one");
            }
            return;
        }
    }

    const auto declaredType = schemaObject->getProperty("type");
    if (!declaredType.isVoid() && !typeMatches(value, declaredType)) {
        addIssue(issues, path, "type",
                 "Expected " + typeDescription(declaredType) + ", got a different JSON type");
        return;
    }

    const auto requiredValue = schemaObject->getProperty("const");
    if (!requiredValue.isVoid() && requiredValue != value)
        addIssue(issues, path, "const", "Value does not match the required constant");

    if (auto* allowed = schemaObject->getProperty("enum").getArray()) {
        const auto found = std::any_of(allowed->begin(), allowed->end(),
                                       [&](const auto& candidate) { return candidate == value; });
        if (!found)
            addIssue(issues, path, "enum", "Value is not one of the allowed values");
    }

    if (value.isInt() || value.isInt64() || value.isDouble()) {
        const auto number = static_cast<double>(value);
        if (!std::isfinite(number)) {
            addIssue(issues, path, "finite", "Number must be finite");
            return;
        }
        const auto minimum = schemaObject->getProperty("minimum");
        if (!minimum.isVoid() && number < static_cast<double>(minimum))
            addIssue(issues, path, "minimum", "Number is below the allowed minimum");
        const auto exclusiveMinimum = schemaObject->getProperty("exclusiveMinimum");
        if (!exclusiveMinimum.isVoid() && number <= static_cast<double>(exclusiveMinimum))
            addIssue(issues, path, "exclusive_minimum",
                     "Number must be greater than the allowed minimum");
        const auto maximum = schemaObject->getProperty("maximum");
        if (!maximum.isVoid() && number > static_cast<double>(maximum))
            addIssue(issues, path, "maximum", "Number is above the allowed maximum");
    }

    if (value.isString()) {
        const auto maxLength = schemaObject->getProperty("maxLength");
        if (!maxLength.isVoid() && value.toString().length() > static_cast<int>(maxLength))
            addIssue(issues, path, "max_length", "String exceeds the allowed length");
    }

    if (auto* array = value.getArray()) {
        const auto maxItems = schemaObject->getProperty("maxItems");
        if (!maxItems.isVoid() && array->size() > static_cast<int>(maxItems))
            addIssue(issues, path, "max_items", "Array exceeds the allowed item count");
        const auto minItems = schemaObject->getProperty("minItems");
        if (!minItems.isVoid() && array->size() < static_cast<int>(minItems))
            addIssue(issues, path, "min_items", "Array has fewer items than allowed");
        const auto itemSchema = schemaObject->getProperty("items");
        if (!itemSchema.isVoid()) {
            for (int index = 0; index < array->size(); ++index)
                validateValue((*array)[index], itemSchema, path + "[" + juce::String(index) + "]",
                              issues);
        }
    }

    auto* object = value.getDynamicObject();
    if (object == nullptr)
        return;

    const auto propertiesVar = schemaObject->getProperty("properties");
    auto* properties = propertiesVar.getDynamicObject();
    if (auto* required = schemaObject->getProperty("required").getArray()) {
        for (const auto& field : *required) {
            const juce::Identifier name(field.toString());
            if (!object->hasProperty(name))
                addIssue(issues, path + "." + field.toString(), "required",
                         "Required field is missing");
        }
    }

    for (const auto& property : object->getProperties()) {
        const auto name = property.name.toString();
        if (properties != nullptr && properties->hasProperty(property.name)) {
            validateValue(property.value, properties->getProperty(property.name), path + "." + name,
                          issues);
        } else if (schemaObject->getProperty("additionalProperties").isBool() &&
                   !static_cast<bool>(schemaObject->getProperty("additionalProperties"))) {
            addIssue(issues, path + "." + name, "unknown_field", "Unknown field");
        }
    }
}

juce::var nullableId(const std::optional<int>& id) {
    return id ? juce::var(*id) : juce::var();
}

// validateJson bounds every integer field to its C++ range before any decode
// runs; this clamp is a second line of defence so an out-of-range int64 can
// never wrap into a different valid value if a schema misses a bound.
template <typename Integer> Integer decodeBoundedInt(const juce::var& value) {
    constexpr auto lowest = static_cast<juce::int64>(std::numeric_limits<Integer>::lowest());
    constexpr auto highest = static_cast<juce::int64>(std::numeric_limits<Integer>::max());
    const auto wide = static_cast<juce::int64>(value);
    jassert(wide >= lowest && wide <= highest);
    return static_cast<Integer>(std::clamp(wide, lowest, highest));
}

int readInt(const juce::var& object, const char* property) {
    return decodeBoundedInt<int>(object[property]);
}

template <typename Id>
std::optional<Id> readNullableId(const juce::var& object, const char* property) {
    const auto value = object[property];
    if (value.isVoid())
        return std::nullopt;
    return decodeBoundedInt<Id>(value);
}

template <typename Integer>
juce::Array<juce::var> integerArray(const std::vector<Integer>& values) {
    juce::Array<juce::var> result;
    result.ensureStorageAllocated(static_cast<int>(values.size()));
    for (const auto value : values)
        result.add(static_cast<juce::int64>(value));
    return result;
}

template <typename Integer> std::vector<Integer> readIntegerArray(const juce::var& value) {
    std::vector<Integer> result;
    if (auto* array = value.getArray()) {
        result.reserve(static_cast<size_t>(array->size()));
        for (const auto& item : *array)
            result.push_back(decodeBoundedInt<Integer>(item));
    }
    return result;
}

bool prepareDecode(const juce::var& json, const juce::var& schema, Error& error) {
    auto issues = validateJson(json, schema);
    if (issues.empty())
        return true;
    error = {ErrorCode::ValidationFailed, "DTO validation failed", std::move(issues)};
    return false;
}

juce::var operationInputSchema(const char* json) {
    return parseSchema(json);
}

}  // namespace

// Shared by device listings, automation targets, and operation handlers: all
// three address a device the same way, and must not drift apart. Defined
// outside the anonymous namespace above so handlers in another translation
// unit reuse this decoder rather than growing a second one — the helpers it
// calls stay internal, which is why it lives here rather than with them.
DevicePathDto devicePathFromJson(const juce::var& json) {
    DevicePathDto dto;
    dto.trackId = readInt(json, "trackId");
    dto.section = json["section"].toString();
    dto.trackLevel = static_cast<bool>(json["trackLevel"]);
    dto.topLevelDeviceId = readNullableId<DeviceId>(json, "topLevelDeviceId");
    if (auto* steps = json["steps"].getArray()) {
        dto.steps.reserve(static_cast<size_t>(steps->size()));
        for (const auto& item : *steps)
            dto.steps.push_back({item["type"].toString(), readInt(item, "id")});
    }
    return dto;
}

juce::String toString(ErrorCode code) {
    switch (code) {
        case ErrorCode::InvalidRequest:
            return "invalid_request";
        case ErrorCode::UnknownOperation:
            return "unknown_operation";
        case ErrorCode::PermissionDenied:
            return "permission_denied";
        case ErrorCode::ValidationFailed:
            return "validation_failed";
        case ErrorCode::NotFound:
            return "not_found";
        case ErrorCode::Conflict:
            return "conflict";
        case ErrorCode::Timeout:
            return "timeout";
        case ErrorCode::Cancelled:
            return "cancelled";
        case ErrorCode::InternalError:
            return "internal_error";
    }
    return "internal_error";
}

juce::var toJson(const Error& error) {
    auto object = new juce::DynamicObject();
    object->setProperty("code", toString(error.code));
    object->setProperty("message", error.message);
    juce::Array<juce::var> issues;
    for (const auto& issue : error.issues) {
        auto issueObject = new juce::DynamicObject();
        issueObject->setProperty("path", issue.path);
        issueObject->setProperty("code", issue.code);
        issueObject->setProperty("message", issue.message);
        issues.add(issueObject);
    }
    object->setProperty("issues", issues);
    return object;
}

juce::var successEnvelope(const juce::var& result) {
    auto object = new juce::DynamicObject();
    object->setProperty("ok", true);
    object->setProperty("apiVersion", juce::String(API_VERSION.data()));
    object->setProperty("result", result);
    return object;
}

juce::var errorEnvelope(const Error& error) {
    auto object = new juce::DynamicObject();
    object->setProperty("ok", false);
    object->setProperty("apiVersion", juce::String(API_VERSION.data()));
    object->setProperty("error", toJson(error));
    return object;
}

std::vector<ValidationIssue> validateJson(const juce::var& value, const juce::var& schema,
                                          const juce::String& path) {
    std::vector<ValidationIssue> issues;
    validateValue(value, schema, path, issues);
    return issues;
}

std::optional<juce::int64> jsonInteger(const juce::var& value, juce::int64 lowest,
                                       juce::int64 highest) {
    juce::int64 result = 0;

    if (value.isInt() || value.isInt64()) {
        result = static_cast<juce::int64>(value);
    } else if (value.isDouble()) {
        const auto number = static_cast<double>(value);
        if (!std::isfinite(number) || number != std::floor(number))
            return std::nullopt;

        // Establish that the value fits an int64 at all before converting, and
        // do it against 2^63 exclusive rather than (double)INT64_MAX. That cast
        // rounds *up* to exactly 2^63, so comparing against it lets 2^63 itself
        // through as "not greater" and straight into the conversion it was meant
        // to prevent. The negative bound needs no such care: -2^63 is exactly
        // representable and is a valid int64.
        constexpr double twoPow63 = 9223372036854775808.0;
        if (number >= twoPow63 || number < -twoPow63)
            return std::nullopt;

        result = static_cast<juce::int64>(number);
    } else {
        return std::nullopt;
    }

    if (result < lowest || result > highest)
        return std::nullopt;
    return result;
}

std::optional<Error> validateOperationInput(const OperationDescriptor& operation,
                                            const juce::var& input) {
    auto issues = validateJson(input, operation.inputSchema);
    if (issues.empty())
        return std::nullopt;
    return Error{ErrorCode::ValidationFailed, "Operation input validation failed",
                 std::move(issues)};
}

juce::var toJson(const MidiNoteDto& dto) {
    auto object = new juce::DynamicObject();
    object->setProperty("note", dto.note);
    object->setProperty("velocity", dto.velocity);
    object->setProperty("startBeat", dto.startBeat);
    object->setProperty("lengthBeats", dto.lengthBeats);
    return object;
}

juce::var toJson(const ProjectDto& dto) {
    auto object = new juce::DynamicObject();
    object->setProperty("name", dto.name);
    object->setProperty("tempo", dto.tempo);
    object->setProperty("timeSignatureNumerator", dto.timeSignatureNumerator);
    object->setProperty("timeSignatureDenominator", dto.timeSignatureDenominator);
    object->setProperty("sampleRate", dto.sampleRate);
    object->setProperty("timelineLengthBars", dto.timelineLengthBars);
    object->setProperty("keyRoot", dto.keyRoot);
    object->setProperty("keyQuality", dto.keyQuality);
    object->setProperty("loopEnabled", dto.loopEnabled);
    object->setProperty("loopStartBeats", dto.loopStartBeats);
    object->setProperty("loopEndBeats", dto.loopEndBeats);
    return object;
}

juce::var toJson(const TrackDto& dto) {
    auto object = new juce::DynamicObject();
    object->setProperty("id", dto.id);
    object->setProperty("type", dto.type);
    object->setProperty("name", dto.name);
    object->setProperty("colourArgb", static_cast<juce::int64>(dto.colourArgb));
    object->setProperty("parentId", nullableId(dto.parentId));
    object->setProperty("childIds", integerArray(dto.childIds));
    object->setProperty("volume", dto.volume);
    object->setProperty("pan", dto.pan);
    object->setProperty("muted", dto.muted);
    object->setProperty("soloed", dto.soloed);
    object->setProperty("recordArmed", dto.recordArmed);
    object->setProperty("frozen", dto.frozen);
    object->setProperty("audioInputDevice", dto.audioInputDevice);
    object->setProperty("midiInputDevice", dto.midiInputDevice);
    object->setProperty("audioOutputDevice", dto.audioOutputDevice);
    object->setProperty("midiOutputDevice", dto.midiOutputDevice);
    return object;
}

juce::var toJson(const ClipDto& dto) {
    auto object = new juce::DynamicObject();
    object->setProperty("id", dto.id);
    object->setProperty("trackId", dto.trackId);
    object->setProperty("type", dto.type);
    object->setProperty("view", dto.view);
    object->setProperty("name", dto.name);
    object->setProperty("colourArgb", static_cast<juce::int64>(dto.colourArgb));
    object->setProperty("startBeat", dto.startBeat);
    object->setProperty("lengthBeats", dto.lengthBeats);
    object->setProperty("enabled", dto.enabled);
    object->setProperty("sceneIndex", nullableId(dto.sceneIndex));
    object->setProperty("launchMode", dto.launchMode);
    object->setProperty("launchQuantize", dto.launchQuantize);
    object->setProperty("followAction", dto.followAction);
    juce::Array<juce::var> notes;
    for (const auto& note : dto.notes)
        notes.add(toJson(note));
    object->setProperty("notes", notes);
    return object;
}

juce::var toJson(const DeviceDto& dto) {
    auto object = new juce::DynamicObject();
    object->setProperty("id", dto.id);
    object->setProperty("trackId", dto.trackId);
    object->setProperty("rackId", nullableId(dto.rackId));
    object->setProperty("chainId", nullableId(dto.chainId));
    object->setProperty("devicePath", toJson(dto.devicePath));
    object->setProperty("name", dto.name);
    object->setProperty("type", dto.type);
    object->setProperty("format", dto.format);
    object->setProperty("instrument", dto.instrument);
    object->setProperty("bypassed", dto.bypassed);
    object->setProperty("gainDb", dto.gainDb);
    return object;
}

juce::var toJson(const ChainDto& dto) {
    auto object = new juce::DynamicObject();
    object->setProperty("id", dto.id);
    object->setProperty("rackId", dto.rackId);
    object->setProperty("name", dto.name);
    object->setProperty("outputIndex", dto.outputIndex);
    object->setProperty("muted", dto.muted);
    object->setProperty("solo", dto.solo);
    object->setProperty("bypassed", dto.bypassed);
    object->setProperty("volumeDb", dto.volumeDb);
    object->setProperty("pan", dto.pan);
    object->setProperty("deviceIds", integerArray(dto.deviceIds));
    object->setProperty("nestedRackIds", integerArray(dto.nestedRackIds));
    return object;
}

juce::var toJson(const RackDto& dto) {
    auto object = new juce::DynamicObject();
    object->setProperty("id", dto.id);
    object->setProperty("trackId", dto.trackId);
    object->setProperty("parentRackId", nullableId(dto.parentRackId));
    object->setProperty("parentChainId", nullableId(dto.parentChainId));
    object->setProperty("name", dto.name);
    object->setProperty("bypassed", dto.bypassed);
    object->setProperty("volumeDb", dto.volumeDb);
    object->setProperty("pan", dto.pan);
    object->setProperty("chainIds", integerArray(dto.chainIds));
    return object;
}

juce::var toJson(const DeviceGraphDto& dto) {
    auto object = new juce::DynamicObject();
    juce::Array<juce::var> devices;
    juce::Array<juce::var> racks;
    juce::Array<juce::var> chains;
    for (const auto& device : dto.devices)
        devices.add(toJson(device));
    for (const auto& rack : dto.racks)
        racks.add(toJson(rack));
    for (const auto& chain : dto.chains)
        chains.add(toJson(chain));
    object->setProperty("devices", devices);
    object->setProperty("racks", racks);
    object->setProperty("chains", chains);
    return object;
}

juce::var toJson(const DeviceCatalogEntryDto& dto) {
    auto object = new juce::DynamicObject();
    object->setProperty("catalogId", dto.catalogId);
    object->setProperty("name", dto.name);
    object->setProperty("manufacturer", dto.manufacturer);
    object->setProperty("category", dto.category);
    object->setProperty("description", dto.description);
    object->setProperty("format", dto.format);
    object->setProperty("type", dto.type);
    object->setProperty("instrument", dto.instrument);
    return object;
}

juce::var toJson(const DeviceParameterDto& dto) {
    auto object = new juce::DynamicObject();
    object->setProperty("index", dto.index);
    object->setProperty("stableId", dto.stableId);
    object->setProperty("name", dto.name);
    object->setProperty("unit", dto.unit);
    object->setProperty("minValue", dto.minValue);
    object->setProperty("maxValue", dto.maxValue);
    object->setProperty("defaultValue", dto.defaultValue);
    object->setProperty("currentValue", dto.currentValue);
    object->setProperty("normalizedValue", dto.normalizedValue);
    object->setProperty("visible", dto.visible);
    object->setProperty("miniMixer", dto.miniMixer);
    object->setProperty("aiAgentEnabled", dto.aiAgentEnabled);
    return object;
}

juce::var toJson(const SelectionDto& dto) {
    auto object = new juce::DynamicObject();
    object->setProperty("trackId", nullableId(dto.trackId));
    object->setProperty("clipId", nullableId(dto.clipId));
    object->setProperty("clipIds", integerArray(dto.clipIds));
    object->setProperty("automationLaneId", nullableId(dto.automationLaneId));
    object->setProperty("automationClipId", nullableId(dto.automationClipId));
    object->setProperty("noteClipId", nullableId(dto.noteClipId));
    object->setProperty("noteIndices", integerArray(dto.noteIndices));
    return object;
}

juce::var toJson(const TransportDto& dto) {
    auto object = new juce::DynamicObject();
    object->setProperty("playing", dto.playing);
    object->setProperty("recording", dto.recording);
    object->setProperty("loopEnabled", dto.loopEnabled);
    object->setProperty("positionBeats", dto.positionBeats);
    return object;
}

juce::var toJson(const SessionSlotDto& dto) {
    auto object = new juce::DynamicObject();
    object->setProperty("trackId", dto.trackId);
    object->setProperty("sceneIndex", dto.sceneIndex);
    object->setProperty("clipId", dto.clipId);
    object->setProperty("state", dto.state);
    return object;
}

juce::var toJson(const SessionDto& dto) {
    auto object = new juce::DynamicObject();
    juce::Array<juce::var> slots;
    for (const auto& slot : dto.slots)
        slots.add(toJson(slot));
    object->setProperty("slots", slots);
    return object;
}

juce::var toJson(const AutomationPointDto& dto) {
    auto object = new juce::DynamicObject();
    object->setProperty("id", dto.id);
    object->setProperty("beatPosition", dto.beatPosition);
    object->setProperty("value", dto.value);
    object->setProperty("curve", dto.curve);
    return object;
}

juce::var toJson(const DevicePathDto& dto) {
    auto object = new juce::DynamicObject();
    object->setProperty("trackId", dto.trackId);
    object->setProperty("section", dto.section);
    object->setProperty("trackLevel", dto.trackLevel);
    object->setProperty("topLevelDeviceId", nullableId(dto.topLevelDeviceId));

    juce::Array<juce::var> steps;
    for (const auto& step : dto.steps) {
        auto* stepObject = new juce::DynamicObject();
        stepObject->setProperty("type", step.type);
        stepObject->setProperty("id", step.id);
        steps.add(juce::var(stepObject));
    }
    object->setProperty("steps", juce::var(steps));
    return object;
}

juce::var toJson(const AutomationTargetDto& dto) {
    auto object = new juce::DynamicObject();
    object->setProperty("kind", dto.kind);
    object->setProperty("devicePath",
                        dto.devicePath.has_value() ? toJson(*dto.devicePath) : juce::var());
    object->setProperty("parameterIndex", dto.parameterIndex);
    object->setProperty("modId", dto.modId);
    object->setProperty("modParameterIndex", dto.modParameterIndex);
    object->setProperty("sendBusIndex", dto.sendBusIndex);
    return object;
}

juce::var toJson(const AutomationLaneDto& dto) {
    auto object = new juce::DynamicObject();
    object->setProperty("id", dto.id);
    object->setProperty("type", dto.type);
    object->setProperty("name", dto.name);
    object->setProperty("target", toJson(dto.target));
    juce::Array<juce::var> points;
    for (const auto& point : dto.points)
        points.add(toJson(point));
    object->setProperty("points", points);
    object->setProperty("clipIds", integerArray(dto.clipIds));
    return object;
}

std::optional<MidiNoteDto> midiNoteFromJson(const juce::var& json, Error& error) {
    if (!prepareDecode(json, midiNoteSchema(), error))
        return std::nullopt;
    return MidiNoteDto{readInt(json, "note"), readInt(json, "velocity"),
                       static_cast<double>(json["startBeat"]),
                       static_cast<double>(json["lengthBeats"])};
}

std::optional<ProjectDto> projectFromJson(const juce::var& json, Error& error) {
    if (!prepareDecode(json, projectSchema(), error))
        return std::nullopt;
    ProjectDto dto;
    dto.name = json["name"].toString();
    dto.tempo = static_cast<double>(json["tempo"]);
    dto.timeSignatureNumerator = readInt(json, "timeSignatureNumerator");
    dto.timeSignatureDenominator = readInt(json, "timeSignatureDenominator");
    dto.sampleRate = static_cast<double>(json["sampleRate"]);
    dto.timelineLengthBars = readInt(json, "timelineLengthBars");
    dto.keyRoot = readInt(json, "keyRoot");
    dto.keyQuality = json["keyQuality"].toString();
    dto.loopEnabled = static_cast<bool>(json["loopEnabled"]);
    dto.loopStartBeats = static_cast<double>(json["loopStartBeats"]);
    dto.loopEndBeats = static_cast<double>(json["loopEndBeats"]);
    return dto;
}

std::optional<TrackDto> trackFromJson(const juce::var& json, Error& error) {
    if (!prepareDecode(json, trackSchema(), error))
        return std::nullopt;
    TrackDto dto;
    dto.id = readInt(json, "id");
    dto.type = json["type"].toString();
    dto.name = json["name"].toString();
    dto.colourArgb = decodeBoundedInt<std::uint32_t>(json["colourArgb"]);
    dto.parentId = readNullableId<TrackId>(json, "parentId");
    dto.childIds = readIntegerArray<TrackId>(json["childIds"]);
    dto.volume = static_cast<double>(json["volume"]);
    dto.pan = static_cast<double>(json["pan"]);
    dto.muted = static_cast<bool>(json["muted"]);
    dto.soloed = static_cast<bool>(json["soloed"]);
    dto.recordArmed = static_cast<bool>(json["recordArmed"]);
    dto.frozen = static_cast<bool>(json["frozen"]);
    dto.audioInputDevice = json["audioInputDevice"].toString();
    dto.midiInputDevice = json["midiInputDevice"].toString();
    dto.audioOutputDevice = json["audioOutputDevice"].toString();
    dto.midiOutputDevice = json["midiOutputDevice"].toString();
    return dto;
}

std::optional<ClipDto> clipFromJson(const juce::var& json, Error& error) {
    if (!prepareDecode(json, clipSchema(), error))
        return std::nullopt;
    ClipDto dto;
    dto.id = readInt(json, "id");
    dto.trackId = readInt(json, "trackId");
    dto.type = json["type"].toString();
    dto.view = json["view"].toString();
    dto.name = json["name"].toString();
    dto.colourArgb = decodeBoundedInt<std::uint32_t>(json["colourArgb"]);
    dto.startBeat = static_cast<double>(json["startBeat"]);
    dto.lengthBeats = static_cast<double>(json["lengthBeats"]);
    dto.enabled = static_cast<bool>(json["enabled"]);
    dto.sceneIndex = readNullableId<int>(json, "sceneIndex");
    dto.launchMode = json["launchMode"].toString();
    dto.launchQuantize = json["launchQuantize"].toString();
    dto.followAction = json["followAction"].toString();
    if (auto* notes = json["notes"].getArray()) {
        for (const auto& note : *notes) {
            auto decoded = midiNoteFromJson(note, error);
            if (!decoded)
                return std::nullopt;
            dto.notes.push_back(*decoded);
        }
    }
    return dto;
}

std::optional<DeviceDto> deviceFromJson(const juce::var& json, Error& error) {
    if (!prepareDecode(json, deviceSchema(), error))
        return std::nullopt;
    DeviceDto dto;
    dto.id = readInt(json, "id");
    dto.trackId = readInt(json, "trackId");
    dto.rackId = readNullableId<RackId>(json, "rackId");
    dto.chainId = readNullableId<ChainId>(json, "chainId");
    dto.devicePath = devicePathFromJson(json["devicePath"]);
    dto.name = json["name"].toString();
    dto.type = json["type"].toString();
    dto.format = json["format"].toString();
    dto.instrument = static_cast<bool>(json["instrument"]);
    dto.bypassed = static_cast<bool>(json["bypassed"]);
    dto.gainDb = static_cast<double>(json["gainDb"]);
    return dto;
}

std::optional<ChainDto> chainFromJson(const juce::var& json, Error& error) {
    if (!prepareDecode(json, chainSchema(), error))
        return std::nullopt;
    ChainDto dto;
    dto.id = readInt(json, "id");
    dto.rackId = readInt(json, "rackId");
    dto.name = json["name"].toString();
    dto.outputIndex = readInt(json, "outputIndex");
    dto.muted = static_cast<bool>(json["muted"]);
    dto.solo = static_cast<bool>(json["solo"]);
    dto.bypassed = static_cast<bool>(json["bypassed"]);
    dto.volumeDb = static_cast<double>(json["volumeDb"]);
    dto.pan = static_cast<double>(json["pan"]);
    dto.deviceIds = readIntegerArray<DeviceId>(json["deviceIds"]);
    dto.nestedRackIds = readIntegerArray<RackId>(json["nestedRackIds"]);
    return dto;
}

std::optional<RackDto> rackFromJson(const juce::var& json, Error& error) {
    if (!prepareDecode(json, rackSchema(), error))
        return std::nullopt;
    RackDto dto;
    dto.id = readInt(json, "id");
    dto.trackId = readInt(json, "trackId");
    dto.parentRackId = readNullableId<RackId>(json, "parentRackId");
    dto.parentChainId = readNullableId<ChainId>(json, "parentChainId");
    dto.name = json["name"].toString();
    dto.bypassed = static_cast<bool>(json["bypassed"]);
    dto.volumeDb = static_cast<double>(json["volumeDb"]);
    dto.pan = static_cast<double>(json["pan"]);
    dto.chainIds = readIntegerArray<ChainId>(json["chainIds"]);
    return dto;
}

std::optional<DeviceGraphDto> deviceGraphFromJson(const juce::var& json, Error& error) {
    if (!prepareDecode(json, deviceGraphSchema(), error))
        return std::nullopt;
    DeviceGraphDto dto;
    for (const auto& item : *json["devices"].getArray()) {
        auto decoded = deviceFromJson(item, error);
        if (!decoded)
            return std::nullopt;
        dto.devices.push_back(*decoded);
    }
    for (const auto& item : *json["racks"].getArray()) {
        auto decoded = rackFromJson(item, error);
        if (!decoded)
            return std::nullopt;
        dto.racks.push_back(*decoded);
    }
    for (const auto& item : *json["chains"].getArray()) {
        auto decoded = chainFromJson(item, error);
        if (!decoded)
            return std::nullopt;
        dto.chains.push_back(*decoded);
    }
    return dto;
}

std::optional<DeviceCatalogEntryDto> deviceCatalogEntryFromJson(const juce::var& json,
                                                                Error& error) {
    if (!prepareDecode(json, deviceCatalogEntrySchema(), error))
        return std::nullopt;
    DeviceCatalogEntryDto dto;
    dto.catalogId = json["catalogId"].toString();
    dto.name = json["name"].toString();
    dto.manufacturer = json["manufacturer"].toString();
    dto.category = json["category"].toString();
    dto.description = json["description"].toString();
    dto.format = json["format"].toString();
    dto.type = json["type"].toString();
    dto.instrument = static_cast<bool>(json["instrument"]);
    return dto;
}

std::optional<DeviceParameterDto> deviceParameterFromJson(const juce::var& json, Error& error) {
    if (!prepareDecode(json, deviceParameterSchema(), error))
        return std::nullopt;
    DeviceParameterDto dto;
    dto.index = readInt(json, "index");
    dto.stableId = json["stableId"].toString();
    dto.name = json["name"].toString();
    dto.unit = json["unit"].toString();
    dto.minValue = static_cast<double>(json["minValue"]);
    dto.maxValue = static_cast<double>(json["maxValue"]);
    dto.defaultValue = static_cast<double>(json["defaultValue"]);
    dto.currentValue = static_cast<double>(json["currentValue"]);
    dto.normalizedValue = static_cast<double>(json["normalizedValue"]);
    dto.visible = static_cast<bool>(json["visible"]);
    dto.miniMixer = static_cast<bool>(json["miniMixer"]);
    dto.aiAgentEnabled = static_cast<bool>(json["aiAgentEnabled"]);
    return dto;
}

std::optional<SelectionDto> selectionFromJson(const juce::var& json, Error& error) {
    if (!prepareDecode(json, selectionSchema(), error))
        return std::nullopt;
    SelectionDto dto;
    dto.trackId = readNullableId<TrackId>(json, "trackId");
    dto.clipId = readNullableId<ClipId>(json, "clipId");
    dto.clipIds = readIntegerArray<ClipId>(json["clipIds"]);
    dto.automationLaneId = readNullableId<AutomationLaneId>(json, "automationLaneId");
    dto.automationClipId = readNullableId<AutomationClipId>(json, "automationClipId");
    dto.noteClipId = readNullableId<ClipId>(json, "noteClipId");
    dto.noteIndices = readIntegerArray<std::int64_t>(json["noteIndices"]);
    return dto;
}

std::optional<TransportDto> transportFromJson(const juce::var& json, Error& error) {
    if (!prepareDecode(json, transportSchema(), error))
        return std::nullopt;
    return TransportDto{static_cast<bool>(json["playing"]), static_cast<bool>(json["recording"]),
                        static_cast<bool>(json["loopEnabled"]),
                        static_cast<double>(json["positionBeats"])};
}

std::optional<SessionDto> sessionFromJson(const juce::var& json, Error& error) {
    if (!prepareDecode(json, sessionSchema(), error))
        return std::nullopt;
    SessionDto dto;
    for (const auto& item : *json["slots"].getArray()) {
        SessionSlotDto slot;
        slot.trackId = readInt(item, "trackId");
        slot.sceneIndex = readInt(item, "sceneIndex");
        slot.clipId = readInt(item, "clipId");
        slot.state = item["state"].toString();
        dto.slots.push_back(std::move(slot));
    }
    return dto;
}

std::optional<AutomationLaneDto> automationLaneFromJson(const juce::var& json, Error& error) {
    if (!prepareDecode(json, automationLaneSchema(), error))
        return std::nullopt;
    AutomationLaneDto dto;
    dto.id = readInt(json, "id");
    dto.type = json["type"].toString();
    dto.name = json["name"].toString();
    const auto target = json["target"];
    dto.target.kind = target["kind"].toString();
    if (const auto path = target["devicePath"]; path.isObject())
        dto.target.devicePath = devicePathFromJson(path);
    dto.target.parameterIndex = readInt(target, "parameterIndex");
    dto.target.modId = readInt(target, "modId");
    dto.target.modParameterIndex = readInt(target, "modParameterIndex");
    dto.target.sendBusIndex = readInt(target, "sendBusIndex");
    for (const auto& item : *json["points"].getArray()) {
        AutomationPointDto point;
        point.id = readInt(item, "id");
        point.beatPosition = static_cast<double>(item["beatPosition"]);
        point.value = static_cast<double>(item["value"]);
        point.curve = item["curve"].toString();
        dto.points.push_back(std::move(point));
    }
    dto.clipIds = readIntegerArray<AutomationClipId>(json["clipIds"]);
    return dto;
}

OperationRegistry::OperationRegistry() {
    const auto idResult = operationInputSchema(R"json({
        "type":"object","properties":{"id":{"type":"integer","minimum":0}},
        "required":["id"],"additionalProperties":false
    })json");
    const auto okResult = operationInputSchema(R"json({
        "type":"object","properties":{"accepted":{"type":"boolean"}},
        "required":["accepted"],"additionalProperties":false
    })json");

    auto add = [this](const char* name, const char* summary, OperationAccess access,
                      OperationHandler handler, juce::var input, juce::var output) {
        // DTO schemas are shared between input and output roles, so cap a deep
        // copy: the request-only DoS limits must never leak into a response
        // schema, which has to stay valid for anything the model can hold.
        input = input.clone();
        applyRequestLimits(input);
        // `requiredScope` is deliberately left at its `read` default here and
        // set from the policy table below, not passed in. See the comment there.
        operations_.push_back({juce::String(name), juce::String(summary), access, Scope::Read,
                               std::move(input), std::move(output), handler});
    };

    add("system.describe", "List the API version and every available operation",
        OperationAccess::Read, &handlers::systemDescribe, emptyObjectSchema(), parseSchema(R"json({
            "type":"object",
            "properties":{
                "apiVersion":{"type":"string"},
                "operations":{"type":"array"}
            },
            "required":["apiVersion","operations"],
            "additionalProperties":false
        })json"));

    add("project.get", "Get safe project metadata", OperationAccess::Read, &handlers::projectGet,
        emptyObjectSchema(), projectSchema());
    add("project.setTempo", "Set the project tempo", OperationAccess::Write,
        &handlers::projectSetTempo, operationInputSchema(R"json({
            "type":"object","properties":{"tempo":{"type":"number","minimum":20,"maximum":400}},
            "required":["tempo"],"additionalProperties":false
        })json"),
        projectSchema());
    add("project.setTimeSignature", "Set the project time signature", OperationAccess::Write,
        &handlers::projectSetTimeSignature, operationInputSchema(R"json({
            "type":"object",
            "properties":{
                "numerator":{"type":"integer","minimum":1,"maximum":32},
                "denominator":{"type":"integer","enum":[1,2,4,8,16,32]}
            },
            "required":["numerator","denominator"],"additionalProperties":false
        })json"),
        projectSchema());

    add("tracks.list", "List tracks", OperationAccess::Read, &handlers::tracksList,
        emptyObjectSchema(), arraySchema(trackSchema()));
    add("tracks.get", "Get one track", OperationAccess::Read, &handlers::tracksGet,
        operationInputSchema(R"json({
            "type":"object","properties":{"trackId":{"anyOf":[
                {"type":"integer","const":-2},{"type":"integer","minimum":0}]}},
            "required":["trackId"],"additionalProperties":false
        })json"),
        trackSchema());
    add("tracks.create", "Create a track", OperationAccess::Write, &handlers::tracksCreate,
        operationInputSchema(R"json({
            "type":"object",
            "properties":{
                "name":{"type":"string"},
                "type":{"type":"string","enum":["audio","group","aux","chord"]}
            },
            "required":["name","type"],"additionalProperties":false
        })json"),
        idResult);
    add("tracks.update", "Update track mixer or display fields", OperationAccess::Write,
        &handlers::tracksUpdate, operationInputSchema(R"json({
            "type":"object",
            "properties":{
                "trackId":{"type":"integer","minimum":0},
                "name":{"type":"string"},
                "volume":{"type":"number","minimum":0},
                "pan":{"type":"number","minimum":-1,"maximum":1},
                "muted":{"type":"boolean"},
                "soloed":{"type":"boolean"}
            },
            "required":["trackId"],"additionalProperties":false
        })json"),
        trackSchema());
    add("tracks.delete", "Delete a track", OperationAccess::Write, &handlers::tracksDelete,
        operationInputSchema(R"json({
            "type":"object","properties":{"trackId":{"type":"integer","minimum":0}},
            "required":["trackId"],"additionalProperties":false
        })json"),
        okResult);

    add("clips.list", "List clips with optional track and view filters", OperationAccess::Read,
        &handlers::clipsList, operationInputSchema(R"json({
            "type":"object",
            "properties":{
                "trackId":{"type":"integer","minimum":0},
                "view":{"type":"string","enum":["arrangement","session"]}
            },
            "additionalProperties":false
        })json"),
        arraySchema(clipSchema()));
    add("clips.get", "Get one clip", OperationAccess::Read, &handlers::clipsGet,
        operationInputSchema(R"json({
            "type":"object","properties":{"clipId":{"type":"integer","minimum":0}},
            "required":["clipId"],"additionalProperties":false
        })json"),
        clipSchema());
    add("clips.createMidi", "Create a MIDI clip", OperationAccess::Write,
        &handlers::clipsCreateMidi, operationInputSchema(R"json({
            "type":"object",
            "properties":{
                "trackId":{"type":"integer","minimum":0},
                "startBeat":{"type":"number","minimum":0},
                "lengthBeats":{"type":"number","exclusiveMinimum":0},
                "view":{"type":"string","enum":["arrangement","session"]}
            },
            "required":["trackId","startBeat","lengthBeats","view"],
            "additionalProperties":false
        })json"),
        idResult);
    add("clips.addMidiNote", "Add a note to a MIDI clip", OperationAccess::Write,
        &handlers::clipsAddMidiNote, operationInputSchema(R"json({
            "type":"object",
            "properties":{
                "clipId":{"type":"integer","minimum":0},
                "note":{"type":"integer","minimum":0,"maximum":127},
                "velocity":{"type":"integer","minimum":0,"maximum":127},
                "startBeat":{"type":"number","minimum":0},
                "lengthBeats":{"type":"number","exclusiveMinimum":0}
            },
            "required":["clipId","note","velocity","startBeat","lengthBeats"],
            "additionalProperties":false
        })json"),
        clipSchema());
    add("clips.delete", "Delete a clip", OperationAccess::Write, &handlers::clipsDelete,
        operationInputSchema(R"json({
            "type":"object","properties":{"clipId":{"type":"integer","minimum":0}},
            "required":["clipId"],"additionalProperties":false
        })json"),
        okResult);

    add("devices.list", "List safe device and rack graph metadata", OperationAccess::Read,
        &handlers::devicesList, operationInputSchema(R"json({
            "type":"object","properties":{"trackId":{"type":"integer","minimum":0}},
            "additionalProperties":false
        })json"),
        deviceGraphSchema());
    // The kinds a client may add, as opposed to devices.list's instances. Read
    // rather than write, and answered from the same catalogue `addDevice` takes
    // its `catalogId` from — so what this lists is exactly what can be asked for.
    add("devices.catalog", "List devices that can be added, by catalogue id", OperationAccess::Read,
        &handlers::devicesCatalog, emptyObjectSchema(), arraySchema(deviceCatalogEntrySchema()));
    // Parameter discovery and direct control (#2274). Values are real units on
    // both sides — discovery reports what a knob shows and setParameter takes
    // the same number back — with `normalizedValue` alongside so a client can
    // compose with automation.addPoint's 0..1 domain.
    add("devices.listParameters",
        "List a device's parameters, in real units, with customization flags",
        OperationAccess::Read, &handlers::devicesListParameters, operationInputSchema(R"json({
            "type":"object","properties":{"devicePath":{}},
            "required":["devicePath"],"additionalProperties":false
        })json"),
        arraySchema(deviceParameterSchema()));
    operations_.back().inputSchema["properties"].getDynamicObject()->setProperty(
        "devicePath", devicePathSchema());
    add("devices.setParameter", "Set one device parameter, in real units", OperationAccess::Write,
        &handlers::devicesSetParameter, operationInputSchema(R"json({
            "type":"object",
            "properties":{
                "devicePath":{},
                "parameterIndex":{"type":"integer","minimum":0},
                "value":{"type":"number"}
            },
            "required":["devicePath","parameterIndex","value"],"additionalProperties":false
        })json"),
        deviceParameterSchema());
    operations_.back().inputSchema["properties"].getDynamicObject()->setProperty(
        "devicePath", devicePathSchema());
    add("devices.setParameterConfig",
        "Update a device's saved parameter customization: the visible / mini-mixer / AI-agent "
        "selections and the per-plugin AI prompt",
        OperationAccess::Write, &handlers::devicesSetParameterConfig, operationInputSchema(R"json({
            "type":"object",
            "properties":{
                "devicePath":{},
                "visibleParameters":{"type":"array","items":{"type":"integer","minimum":0}},
                "miniMixerParameters":{"type":"array","items":{"type":"integer","minimum":0}},
                "aiAgentParameters":{"type":"array","items":{"type":"integer","minimum":0}},
                "aiPrompt":{"type":"string"}
            },
            "required":["devicePath"],"additionalProperties":false
        })json"),
        arraySchema(deviceParameterSchema()));
    operations_.back().inputSchema["properties"].getDynamicObject()->setProperty(
        "devicePath", devicePathSchema());
    add("devices.openEditor", "Open a device's plugin editor window", OperationAccess::Write,
        &handlers::devicesOpenEditor, operationInputSchema(R"json({
            "type":"object","properties":{"devicePath":{}},
            "required":["devicePath"],"additionalProperties":false
        })json"),
        okResult);
    operations_.back().inputSchema["properties"].getDynamicObject()->setProperty(
        "devicePath", devicePathSchema());
    add("racks.create", "Create a top-level rack", OperationAccess::Write, &handlers::racksCreate,
        operationInputSchema(R"json({
            "type":"object",
            "properties":{"trackId":{"type":"integer","minimum":0},"name":{"type":"string"}},
            "required":["trackId","name"],"additionalProperties":false
        })json"),
        idResult);
    add("racks.remove", "Remove a top-level rack", OperationAccess::Write, &handlers::racksRemove,
        operationInputSchema(R"json({
            "type":"object",
            "properties":{
                "trackId":{"type":"integer","minimum":0},
                "rackId":{"type":"integer","minimum":0}
            },
            "required":["trackId","rackId"],"additionalProperties":false
        })json"),
        okResult);
    add("racks.setBypassed", "Set rack bypass", OperationAccess::Write, &handlers::racksSetBypassed,
        operationInputSchema(R"json({
            "type":"object",
            "properties":{
                "trackId":{"type":"integer","minimum":0},
                "rackId":{"type":"integer","minimum":0},
                "bypassed":{"type":"boolean"}
            },
            "required":["trackId","rackId","bypassed"],"additionalProperties":false
        })json"),
        rackSchema());

    add("selection.get", "Get the current selection", OperationAccess::Read,
        &handlers::selectionGet, emptyObjectSchema(), selectionSchema());
    add("selection.set", "Replace the current track, clip, or note selection",
        OperationAccess::Write, &handlers::selectionSet, selectionSchema(), selectionSchema());

    add("transport.get", "Get transport state", OperationAccess::Read, &handlers::transportGet,
        emptyObjectSchema(), transportSchema());
    add("transport.play", "Start playback", OperationAccess::Write, &handlers::transportPlay,
        emptyObjectSchema(), transportSchema());
    add("transport.stop", "Stop playback", OperationAccess::Write, &handlers::transportStop,
        emptyObjectSchema(), transportSchema());
    add("transport.setRecording", "Set recording state", OperationAccess::Write,
        &handlers::transportSetRecording, operationInputSchema(R"json({
            "type":"object","properties":{"recording":{"type":"boolean"}},
            "required":["recording"],"additionalProperties":false
        })json"),
        transportSchema());
    add("transport.setLoopEnabled", "Set transport loop state", OperationAccess::Write,
        &handlers::transportSetLoopEnabled, operationInputSchema(R"json({
            "type":"object","properties":{"enabled":{"type":"boolean"}},
            "required":["enabled"],"additionalProperties":false
        })json"),
        transportSchema());
    add("transport.seek", "Seek the transport in beats", OperationAccess::Write,
        &handlers::transportSeek, operationInputSchema(R"json({
            "type":"object","properties":{"positionBeats":{"type":"number","minimum":0}},
            "required":["positionBeats"],"additionalProperties":false
        })json"),
        transportSchema());
    add("transport.seekRelative", "Move the transport by beats or bars, clamped at zero",
        OperationAccess::Write, &handlers::transportSeekRelative, operationInputSchema(R"json({
            "type":"object",
            "properties":{
                "deltaBeats":{"type":"number"},
                "deltaBars":{"type":"integer","minimum":-1000000,"maximum":1000000}
            },
            "oneOf":[{"required":["deltaBeats"]},{"required":["deltaBars"]}],
            "additionalProperties":false
        })json"),
        transportSchema());

    add("session.get", "Get occupied session slots and play states", OperationAccess::Read,
        &handlers::sessionGet, emptyObjectSchema(), sessionSchema());
    add("session.launchClip", "Launch a session clip", OperationAccess::Write,
        &handlers::sessionLaunchClip, operationInputSchema(R"json({
            "type":"object","properties":{"clipId":{"type":"integer","minimum":0}},
            "required":["clipId"],"additionalProperties":false
        })json"),
        sessionSchema());
    add("session.stopClip", "Stop a session clip", OperationAccess::Write,
        &handlers::sessionStopClip, operationInputSchema(R"json({
            "type":"object","properties":{"clipId":{"type":"integer","minimum":0}},
            "required":["clipId"],"additionalProperties":false
        })json"),
        sessionSchema());
    add("session.stopTrack", "Stop the active session clip on a track", OperationAccess::Write,
        &handlers::sessionStopTrack, operationInputSchema(R"json({
            "type":"object","properties":{"trackId":{"type":"integer","minimum":0}},
            "required":["trackId"],"additionalProperties":false
        })json"),
        sessionSchema());
    add("session.stopAll", "Stop all session clips", OperationAccess::Write,
        &handlers::sessionStopAll, emptyObjectSchema(), sessionSchema());
    add("session.launchScene", "Launch a session scene", OperationAccess::Write,
        &handlers::sessionLaunchScene, operationInputSchema(R"json({
            "type":"object","properties":{"sceneIndex":{"type":"integer","minimum":0}},
            "required":["sceneIndex"],"additionalProperties":false
        })json"),
        sessionSchema());

    add("automation.listLanes", "List every automation lane in the project", OperationAccess::Read,
        &handlers::automationListLanes, emptyObjectSchema(), arraySchema(automationLaneSchema()));
    add("automation.getLane", "Get an automation lane", OperationAccess::Read,
        &handlers::automationGetLane, operationInputSchema(R"json({
            "type":"object","properties":{"laneId":{"type":"integer","minimum":0}},
            "required":["laneId"],"additionalProperties":false
        })json"),
        automationLaneSchema());
    add("automation.createLane", "Create an automation lane", OperationAccess::Write,
        &handlers::automationCreateLane, operationInputSchema(R"json({
            "type":"object",
            "properties":{
                "target":{},
                "type":{"type":"string","enum":["absolute","clip_based"]}
            },
            "required":["target","type"],"additionalProperties":false
        })json"),
        idResult);
    operations_.back().inputSchema["properties"].getDynamicObject()->setProperty(
        "target", automationTargetSchema());
    add("automation.addPoint", "Add a point to an automation lane", OperationAccess::Write,
        &handlers::automationAddPoint, operationInputSchema(R"json({
            "type":"object",
            "properties":{
                "laneId":{"type":"integer","minimum":0},
                "beatPosition":{"type":"number","minimum":0},
                "value":{"type":"number","minimum":0,"maximum":1},
                "curve":{"type":"string","enum":["linear","bezier","step","hard_corner"]}
            },
            "required":["laneId","beatPosition","value","curve"],
            "additionalProperties":false
        })json"),
        automationLaneSchema());
    add("automation.clearLane", "Remove all points from an automation lane", OperationAccess::Write,
        &handlers::automationClearLane, operationInputSchema(R"json({
            "type":"object","properties":{"laneId":{"type":"integer","minimum":0}},
            "required":["laneId"],"additionalProperties":false
        })json"),
        automationLaneSchema());

    // -----------------------------------------------------------------------
    // Subscriptions (#1857)
    //
    // Declared here and executed by the transport. Subscribing is per-connection
    // state and the dispatcher has no connections, but the contract — names,
    // schemas, what a snapshot looks like — has to be one thing or the WebSocket
    // and MCP adapters will grow two. `system.describe` therefore lists these
    // like any other operation, marked so a client can tell that reaching them
    // needs a transport that can push.
    // -----------------------------------------------------------------------

    auto addTransportScoped = [this](const char* name, const char* summary, juce::var input,
                                     juce::var output) {
        input = input.clone();
        applyRequestLimits(input);
        // `read`, like any other read: subscribing pushes the same projections
        // the read operations return, so it cannot need less.
        operations_.push_back({juce::String(name), juce::String(summary), OperationAccess::Read,
                               Scope::Read, std::move(input), std::move(output), nullptr, true});
    };

    addTransportScoped(
        "subscriptions.subscribe",
        "Start receiving change events for the given topics, with an initial snapshot",
        subscribeInputSchema(), subscriptionResultSchema(true));
    addTransportScoped("subscriptions.unsubscribe",
                       "Stop receiving change events; no topics means every topic",
                       topicSelectionSchema(), subscriptionResultSchema(false));
    addTransportScoped("subscriptions.list", "List the topics this connection is subscribed to",
                       emptyObjectSchema(), subscriptionResultSchema(false));
    addTransportScoped("subscriptions.resync",
                       "Re-send a complete snapshot for the subscribed topics",
                       topicSelectionSchema(), subscriptionResultSchema(true));

    // -----------------------------------------------------------------------
    // Permission policy (#1860)
    //
    // One table rather than an argument on forty multi-line `add` calls,
    // because the thing a reviewer needs to check is not "does this operation
    // declare a scope" but "is the *whole* division of the API into scopes the
    // one we meant" — and that is a question you can only answer by reading the
    // policy contiguously. `docs/remote-api-permissions.md` mirrors this list,
    // and the conformance test compares the two.
    //
    // Reads are absent by design: `read` is the descriptor default and every
    // client has it, so listing fifteen operations to say "yes, readable" would
    // bury the fifteen decisions that actually matter. Writes are never absent —
    // one that is keeps the `read` default, which the check below turns into a
    // startup failure rather than a client discovering it can edit.
    // -----------------------------------------------------------------------
    struct ScopePolicy {
        const char* operation;
        Scope scope;
    };
    static constexpr ScopePolicy kWriteScopes[] = {
        // Project content. Selection is here rather than in a scope of its own:
        // it changes what the user is looking at and what their next keystroke
        // acts on, which is not something a read-only client should reach.
        {"project.setTempo", Scope::Edit},
        {"project.setTimeSignature", Scope::Edit},
        {"tracks.create", Scope::Edit},
        {"tracks.update", Scope::Edit},
        {"tracks.delete", Scope::Edit},
        {"clips.createMidi", Scope::Edit},
        {"clips.addMidiNote", Scope::Edit},
        {"clips.delete", Scope::Edit},
        {"racks.create", Scope::Edit},
        {"racks.remove", Scope::Edit},
        {"racks.setBypassed", Scope::Edit},
        {"devices.setParameter", Scope::Edit},
        {"devices.setParameterConfig", Scope::Edit},
        // Opening a plugin editor changes no project content, but it takes
        // over part of the user's screen — an edit-grade intrusion, not
        // something a read-only client should reach.
        {"devices.openEditor", Scope::Edit},
        {"selection.set", Scope::Edit},
        {"automation.createLane", Scope::Edit},
        {"automation.addPoint", Scope::Edit},
        {"automation.clearLane", Scope::Edit},

        // The timeline. Separable from editing because a remote that only
        // starts and stops playback is a thing people actually want, and it
        // must not also be able to delete a track.
        {"transport.play", Scope::Transport},
        {"transport.stop", Scope::Transport},
        {"transport.setRecording", Scope::Transport},
        {"transport.setLoopEnabled", Scope::Transport},
        {"transport.seek", Scope::Transport},
        {"transport.seekRelative", Scope::Transport},

        // Clip launching. Neither editing nor the timeline transport: a
        // performance controller firing scenes changes no project content and
        // does not move the playhead.
        {"session.launchClip", Scope::Session},
        {"session.stopClip", Scope::Session},
        {"session.stopTrack", Scope::Session},
        {"session.stopAll", Scope::Session},
        {"session.launchScene", Scope::Session},
    };

    for (const auto& [name, scope] : kWriteScopes) {
        const auto found =
            std::find_if(operations_.begin(), operations_.end(),
                         [&](const OperationDescriptor& op) { return op.name == name; });
        // A policy entry naming an operation that does not exist is a rename
        // that updated one side. Silently ignoring it would leave the renamed
        // operation on the `read` default, which the check below catches — but
        // this says which end is wrong.
        if (found == operations_.end()) {
            jassertfalse;
            juce::Logger::writeToLog(juce::String("Remote API scope policy names an unknown "
                                                  "operation: ") +
                                     name);
            continue;
        }
        found->requiredScope = scope;
    }

    // A declared operation with no implementation is a startup failure, not a
    // runtime surprise. Before handlers lived on the descriptor there was
    // nothing to catch it, and the registry advertised 36 operations that
    // `system.describe` would happily list and no transport could execute. A
    // transport-scoped operation is the one legitimate exception: its
    // implementation lives in the adapter, so there is nothing to point at here.
    //
    // The scope check beside it is the same kind of guarantee for the same kind
    // of mistake: a write that never reached the policy table above would be
    // callable by every read-only client, and nothing else in the system would
    // notice.
    for (const auto& operation : operations_) {
        jassert(operation.transportScoped == (operation.handler == nullptr));
        if (!operation.transportScoped && operation.handler == nullptr)
            juce::Logger::writeToLog("Remote API operation has no handler: " + operation.name);

        const bool writeNeedsMoreThanRead =
            operation.access == OperationAccess::Read || operation.requiredScope != Scope::Read;
        jassert(writeNeedsMoreThanRead);
        if (!writeNeedsMoreThanRead)
            juce::Logger::writeToLog("Remote API write operation has no scope: " + operation.name);
    }
}

const OperationRegistry& OperationRegistry::instance() {
    static const OperationRegistry registry;
    return registry;
}

const std::vector<OperationDescriptor>& OperationRegistry::operations() const {
    return operations_;
}

const OperationDescriptor* OperationRegistry::find(const juce::String& name) const {
    const auto found = std::find_if(operations_.begin(), operations_.end(),
                                    [&](const auto& operation) { return operation.name == name; });
    return found == operations_.end() ? nullptr : &*found;
}

juce::var OperationRegistry::describe() const {
    auto result = new juce::DynamicObject();
    result->setProperty("apiVersion", juce::String(API_VERSION.data()));
    juce::Array<juce::var> operations;
    for (const auto& operation : operations_) {
        auto object = new juce::DynamicObject();
        object->setProperty("name", operation.name);
        object->setProperty("summary", operation.summary);
        object->setProperty("access", operation.access == OperationAccess::Read ? "read" : "write");
        // The scope a caller needs, so a client can tell "I may not do this"
        // apart from "this does not exist" before it tries, and can present the
        // user with what to grant rather than with a refusal.
        object->setProperty("requiredScope", scopeName(operation.requiredScope));
        object->setProperty("inputSchema", operation.inputSchema.clone());
        object->setProperty("outputSchema", operation.outputSchema.clone());
        // Always present rather than only when true: a client deciding whether
        // it can call something should read a field, not infer one from silence.
        object->setProperty("transportScoped", operation.transportScoped);
        operations.add(object);
    }
    result->setProperty("operations", operations);
    return result;
}

}  // namespace magda::remote
