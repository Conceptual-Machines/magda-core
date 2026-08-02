#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/DeviceState.hpp"

namespace ds = magda::device_state;

namespace {

ds::Doc makeDoc() {
    ds::Doc doc;
    doc.deviceType = "magdaSampler";
    doc.params = {{0, "attack", 0.25f}, {1, "decay", 0.5f}, {2, "level", 1.0f}};
    doc.root.props.set("samplePath", "/tmp/kick.wav");
    doc.root.props.set("rootNote", 60);
    doc.root.props.set("loopEnabled", true);

    ds::Node step;
    step.type = "STEP";
    step.props.set("note", 36);
    step.props.set("velocity", 0.8);
    doc.root.children.push_back(step);

    return doc;
}

}  // namespace

TEST_CASE("device state v2 round-trips through JSON", "[device-state]") {
    const auto original = makeDoc();
    const auto text = ds::encode(original);

    const auto decoded = ds::decode(text);
    REQUIRE(decoded.has_value());

    CHECK(decoded->version == ds::kSchemaVersion);
    CHECK(decoded->deviceType == "magdaSampler");

    REQUIRE(decoded->params.size() == 3);
    CHECK(decoded->params[1].index == 1);
    CHECK(decoded->params[1].id == "decay");
    CHECK(decoded->params[1].value == 0.5f);

    CHECK(decoded->root.props["samplePath"].toString() == "/tmp/kick.wav");
    CHECK(static_cast<int>(decoded->root.props["rootNote"]) == 60);
    CHECK(static_cast<bool>(decoded->root.props["loopEnabled"]));

    REQUIRE(decoded->root.children.size() == 1);
    CHECK(decoded->root.children[0].type == "STEP");
    CHECK(static_cast<int>(decoded->root.children[0].props["note"]) == 36);
}

TEST_CASE("device state v2 carries no engine container", "[device-state]") {
    const auto text = ds::encode(makeDoc());

    // The acceptance test for #1887: a newly written device state is a MAGDA
    // document, not the engine's plugin ValueTree.
    CHECK_FALSE(ds::looksLikeLegacyEngineState(text));
    CHECK_FALSE(text.contains("<PLUGIN"));
    CHECK(ds::isDeviceStateV2(text));
}

TEST_CASE("legacy engine state is recognised, never decoded as v2", "[device-state]") {
    const juce::String legacy =
        R"(<PLUGIN type="magdaSampler" id="1042" enabled="1" samplePath="/tmp/kick.wav"/>)";

    CHECK(ds::looksLikeLegacyEngineState(legacy));
    CHECK_FALSE(ds::decode(legacy).has_value());
    CHECK_FALSE(ds::isDeviceStateV2(legacy));
}

TEST_CASE("device state decode rejects unrelated payloads", "[device-state]") {
    CHECK_FALSE(ds::decode({}).has_value());
    CHECK_FALSE(ds::decode("not json at all").has_value());
    CHECK_FALSE(ds::decode(R"({"schema":1,"device":"magdaSampler"})").has_value());
    CHECK_FALSE(ds::decode(R"({"schema":2})").has_value());
    CHECK_FALSE(ds::decode(R"({"device":"magdaSampler"})").has_value());
}

TEST_CASE("device state refuses a newer schema", "[device-state]") {
    // A newer schema may redefine an existing field, so reading it as v2 would
    // misinterpret the values and then write the misreading back on save.
    const juce::String future =
        R"({"schema":3,"device":"magdaSampler","props":{"samplePath":"/tmp/kick.wav"}})";

    CHECK_FALSE(ds::decode(future).has_value());
    CHECK_FALSE(ds::isDeviceStateV2(future));
    CHECK_FALSE(ds::looksLikeLegacyEngineState(future));

    // Refusing to read it is only half the job: capture has to recognise it too,
    // or the next save replaces the newer document with a v2 one and the
    // downgrade happens anyway.
    CHECK(ds::isFutureDeviceState(future));
    CHECK(ds::schemaVersionOf(future) == 3);
}

TEST_CASE("only a newer schema counts as future state", "[device-state]") {
    // Anything capture is allowed to overwrite must NOT be reported as future,
    // or ordinary saves would stop updating device state.
    CHECK_FALSE(ds::isFutureDeviceState({}));
    CHECK_FALSE(ds::isFutureDeviceState("not json at all"));
    CHECK_FALSE(ds::isFutureDeviceState(ds::encode(makeDoc())));
    CHECK_FALSE(ds::isFutureDeviceState(
        R"(<PLUGIN type="magdaSampler" id="1042" samplePath="/tmp/kick.wav"/>)"));
    CHECK_FALSE(ds::isFutureDeviceState("VVNUM0Jhc2U2NENodW5r"));  // external plugin chunk

    CHECK(ds::schemaVersionOf(ds::encode(makeDoc())) == ds::kSchemaVersion);
    CHECK_FALSE(ds::schemaVersionOf("not json at all").has_value());
}

TEST_CASE("device state preserves binary properties", "[device-state]") {
    // A device that stores audio in a property (impulse response, wavetable).
    // JUCE's JSON writer has no binary case and emits unquoted base64, so
    // without the tagged wrapper the whole document stops parsing and the
    // device loses everything, not just the blob.
    juce::MemoryBlock payload;
    for (int i = 0; i < 512; ++i)
        payload.append(&i, 1);

    ds::Doc doc;
    doc.deviceType = "impulseResponse";
    doc.root.props.set("irFileData", juce::var(payload));
    doc.root.props.set("normalise", true);

    const auto text = ds::encode(doc);

    const auto decoded = ds::decode(text);
    REQUIRE(decoded.has_value());

    const auto* restored = decoded->root.props["irFileData"].getBinaryData();
    REQUIRE(restored != nullptr);
    CHECK(*restored == payload);
    CHECK(static_cast<bool>(decoded->root.props["normalise"]));
}

TEST_CASE("device state preserves binary inside array properties", "[device-state]") {
    juce::MemoryBlock payload;
    payload.append("abc", 3);

    ds::Doc doc;
    doc.deviceType = "someDevice";
    doc.root.props.set("blobs",
                       juce::var(juce::Array<juce::var>{juce::var(payload), juce::var(7)}));

    const auto decoded = ds::decode(ds::encode(doc));
    REQUIRE(decoded.has_value());

    const auto* array = decoded->root.props["blobs"].getArray();
    REQUIRE(array != nullptr);
    REQUIRE(array->size() == 2);

    // getReference, not operator[]: juce::Array returns elements BY VALUE, so
    // getBinaryData() on the result would point into a destroyed temporary.
    const auto* restored = array->getReference(0).getBinaryData();
    REQUIRE(restored != nullptr);
    CHECK(*restored == payload);
    CHECK(static_cast<int>(array->getReference(1)) == 7);
}

TEST_CASE("device state walks nested nodes", "[device-state]") {
    ds::Doc doc;
    doc.deviceType = "drumGrid";

    ds::Node pad;
    pad.type = "PAD";
    pad.props.set("magdaDeviceId", 71);

    ds::Node inner;
    inner.type = "PLUGIN";
    inner.props.set("magdaDeviceId", 72);
    pad.children.push_back(inner);
    doc.root.children.push_back(pad);

    const auto decoded = ds::decode(ds::encode(doc));
    REQUIRE(decoded.has_value());

    int maxDeviceId = 0;
    ds::forEachNode(decoded->root, [&](const ds::Node& node) {
        if (const auto* value = node.props.getVarPointer(juce::Identifier("magdaDeviceId")))
            maxDeviceId = std::max(maxDeviceId, static_cast<int>(*value));
    });
    CHECK(maxDeviceId == 72);
}
