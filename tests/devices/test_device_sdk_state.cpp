#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <magda/sdk/state/BinaryText.hpp>
#include <magda/sdk/state/StateCodec.hpp>
#include <string>
#include <vector>

#include "DeviceTestState.hpp"
#include "magda/daw/audio/plugins/DeviceStateDocument.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaEqCompiledPlugin.hpp"
#include "magda/daw/audio/plugins/engine/EngineDeviceFactory.hpp"
#include "magda/daw/audio/plugins/engine/EngineMagdaDevice.hpp"
#include "magda/daw/core/DeviceInfo.hpp"
#include "magda/daw/core/DeviceState.hpp"

// The SDK's state document against what MAGDA's own JUCE codec writes and reads (#2939).

namespace {

namespace audio = magda::daw::audio;
namespace ds = magda::device_state;
namespace sdk = magda::sdk;

std::vector<std::uint8_t> bytesOf(const juce::MemoryBlock& block) {
    const auto* data = static_cast<const std::uint8_t*>(block.getData());
    return {data, data + block.getSize()};
}

}  // namespace

TEST_CASE("Binary text is byte for byte what juce::MemoryBlock writes", "[device-state][sdk]") {
    for (std::size_t size = 0; size < 300; ++size) {
        juce::MemoryBlock block;
        for (std::size_t i = 0; i < size; ++i) {
            const auto byte = static_cast<std::uint8_t>((i * 131 + size * 7) & 0xFF);
            block.append(&byte, 1);
        }

        const auto juceText = block.toBase64Encoding().toStdString();
        CHECK(sdk::encodeBinaryText(bytesOf(block)) == juceText);

        const auto decoded = sdk::decodeBinaryText(juceText);
        REQUIRE(decoded.has_value());
        CHECK(*decoded == bytesOf(block));

        juce::MemoryBlock fromSdk;
        REQUIRE(fromSdk.fromBase64Encoding(sdk::encodeBinaryText(bytesOf(block))));
        CHECK(fromSdk == block);
    }
}

TEST_CASE("Typed getters coerce exactly as juce::var does", "[device-state][sdk]") {
    const std::vector<juce::var> values{
        juce::var(0),
        juce::var(1),
        juce::var(-7),
        juce::var(60),
        juce::var(2147483647),
        juce::var(juce::int64{1} << 40),
        juce::var(-juce::int64{1} << 40),
        juce::var(0.0),
        juce::var(1.5),
        juce::var(-2.5),
        juce::var(0.1),
        juce::var(1.0e-7),
        juce::var(123456.789),
        juce::var(true),
        juce::var(false),
        juce::var("60"),
        juce::var("1"),
        juce::var("0"),
        juce::var("true"),
        juce::var("false"),
        juce::var("yes"),
        juce::var("Yes "),
        juce::var("1.5"),
        juce::var("-1.5"),
        juce::var("  42 abc"),
        juce::var("+5"),
        juce::var("abc"),
        juce::var(""),
        juce::var("0.5"),
        juce::var("1e3"),
        juce::var(".5"),
        juce::var("-"),
        juce::var("007"),
        juce::var("99999999999"),
        juce::var("2.9"),
        juce::var("  -3"),
        juce::var("TRUE"),
        juce::var(" true"),
    };

    for (const auto& value : values) {
        INFO(value.toString().toStdString());

        sdk::StateNode node;
        std::vector<std::string> dropped;
        ds::Node legacy;
        legacy.props.set("k", value);
        node = audio::toSdkNode(legacy, &dropped);
        REQUIRE(dropped.empty());
        REQUIRE(node.has("k"));

        CHECK(node.getInt("k") == static_cast<int>(value));
        CHECK(node.getInt64("k") == static_cast<juce::int64>(value));
        CHECK(node.getBool("k") == static_cast<bool>(value));
        CHECK(node.getDouble("k") == static_cast<double>(value));
        // A double's text is the shortest that reads back, not JUCE's own spelling of it.
        if (value.isDouble())
            CHECK(std::strtod(node.getString("k").c_str(), nullptr) == static_cast<double>(value));
        else
            CHECK(node.getString("k") == value.toString().toStdString());
    }
}

TEST_CASE("A double reads as the string juce writes, within the digits it keeps",
          "[device-state][sdk]") {
    for (const double value : {0.1, 1.5, -2.5, 1.0e-7, 123456.789, 100.0, 1.0 / 3.0}) {
        INFO(value);
        sdk::StateNode node;
        REQUIRE(node.setDouble("k", value));
        const auto text = node.getString("k");
        CHECK(std::strtod(text.c_str(), nullptr) == value);
        CHECK(std::abs(juce::var(value).toString().getDoubleValue() - value) <=
              std::abs(value) * 1.0e-12);
    }
}

TEST_CASE("The SDK reads documents the JUCE writer writes", "[device-state][sdk]") {
    ds::Doc doc;
    doc.deviceType = "unregisteredDevice";
    doc.root.props.set("samplePath",
                       juce::String(juce::CharPointer_UTF8("/tmp/kick \"x\" \xC3\xA9.wav")));
    doc.root.props.set("rootNote", 60);
    doc.root.props.set("gain", 0.8);
    doc.root.props.set("loopEnabled", true);
    doc.root.props.set("ir", juce::var(juce::MemoryBlock("abc\0\xff", 5)));

    ds::Node step;
    step.type = "STEP";
    step.props.set("note", 36);
    step.props.set("velocity", 0.75);
    doc.root.children.push_back(step);

    const auto text = ds::encode(doc).toStdString();
    const auto decoded = sdk::decodeDocument(text);
    REQUIRE(decoded.ok());
    CHECK(decoded.document->deviceType == "unregisteredDevice");
    CHECK(decoded.document->root.getString("samplePath") == "/tmp/kick \"x\" \xC3\xA9.wav");
    CHECK(decoded.document->root.getInt("rootNote") == 60);
    CHECK(decoded.document->root.getDouble("gain") == 0.8);
    CHECK(decoded.document->root.getBool("loopEnabled"));
    REQUIRE(decoded.document->root.getBinary("ir") != nullptr);
    CHECK(decoded.document->root.getBinary("ir")->size() == 5);
    REQUIRE(decoded.document->root.children().size() == 1);
    CHECK(decoded.document->root.children()[0].type() == "STEP");

    // Both agree on what the document holds.
    const auto viaHost = audio::normaliseDeviceState(juce::String(text));
    REQUIRE(viaHost.has_value());
    CHECK(viaHost->document == *decoded.document);
}

TEST_CASE("The JUCE reader reads documents the SDK writes", "[device-state][sdk]") {
    sdk::StateDocument document;
    document.deviceType = "unregisteredDevice";
    document.root.setString("samplePath", "a/b.wav");
    document.root.setInt("rootNote", 48);
    document.root.setDouble("gain", 0.25);
    document.root.setBool("loopEnabled", true);
    document.root.setBinary("ir", {0, 1, 2, 3, 250});
    document.root.addChild(sdk::StateNode("STEP")).setInt("note", 40);

    const auto text = sdk::encodeDocument(document);
    REQUIRE(text.has_value());

    const auto read = ds::decode(juce::String(*text));
    REQUIRE(read.has_value());
    CHECK(read->deviceType == "unregisteredDevice");
    CHECK(read->root.props["samplePath"].toString() == "a/b.wav");
    CHECK(static_cast<int>(read->root.props["rootNote"]) == 48);
    CHECK(static_cast<double>(read->root.props["gain"]) == 0.25);
    CHECK(static_cast<bool>(read->root.props["loopEnabled"]));
    REQUIRE(read->root.props["ir"].getBinaryData() != nullptr);
    CHECK(read->root.props["ir"].getBinaryData()->getSize() == 5);
    REQUIRE(read->root.children.size() == 1);
    CHECK(static_cast<int>(read->root.children[0].props["note"]) == 40);
}

TEST_CASE("Normalising drops the parameter record and what the SDK cannot hold",
          "[device-state][sdk]") {
    ds::Doc doc;
    doc.deviceType = "unregisteredDevice";
    doc.params = {{0, "attack", 0.25f}};
    doc.root.props.set("samplePath", "a.wav");
    doc.root.props.set("blobs", juce::var(juce::Array<juce::var>{juce::var(1), juce::var(2)}));

    const auto state = audio::normaliseDeviceState(ds::encode(doc));
    REQUIRE(state.has_value());
    CHECK(state->document.root.has("samplePath"));
    CHECK_FALSE(state->document.root.has("blobs"));
    REQUIRE(state->dropped.size() == 1);
    CHECK(state->dropped[0] == "blobs");

    // What it hands a device is a document the strict codec accepts.
    const auto text = sdk::encodeDocument(state->document);
    REQUIRE(text.has_value());
    CHECK(sdk::decodeDocument(*text).ok());
}

TEST_CASE("Normalising reads engine XML without the engine's own vocabulary",
          "[device-state][sdk]") {
    const auto state = audio::normaliseDeviceState(
        R"(<PLUGIN type="unregisteredDevice" id="51" enabled="1" windowPos="0 0" rootNote="48" gain="0.5"><STEP id="52" note="36"/><MODIFIERASSIGNMENTS id="53"/><MACROPARAMETERS id="54"/></PLUGIN>)");
    REQUIRE(state.has_value());
    CHECK(state->document.deviceType == "unregisteredDevice");

    const auto& root = state->document.root;
    CHECK_FALSE(root.has("type"));
    CHECK_FALSE(root.has("id"));
    CHECK_FALSE(root.has("enabled"));
    CHECK_FALSE(root.has("windowPos"));
    CHECK(root.getInt("rootNote") == 48);
    CHECK(root.getDouble("gain") == 0.5);
    REQUIRE(root.children().size() == 1);
    CHECK(root.children()[0].type() == "STEP");
    CHECK_FALSE(root.children()[0].has("id"));
    CHECK(root.children()[0].getInt("note") == 36);
}

TEST_CASE("Normalising resolves a device-type alias in either format", "[device-state][sdk]") {
    ds::Doc doc;
    doc.deviceType = "tone";
    const auto current = audio::normaliseDeviceState(ds::encode(doc));
    const auto legacy = audio::normaliseDeviceState(R"(<PLUGIN type="tone"/>)");
    REQUIRE(current.has_value());
    REQUIRE(legacy.has_value());
    CHECK(current->document.deviceType == "toneGenerator");
    CHECK(legacy->document.deviceType == "toneGenerator");
}

TEST_CASE("Normalising refuses what no device could read", "[device-state][sdk]") {
    const juce::String future = R"({"schema":99,"device":"toneGenerator","props":{"a":1}})";
    CHECK_FALSE(audio::normaliseDeviceState(future).has_value());
    CHECK_FALSE(audio::normaliseDeviceState({}).has_value());
    CHECK_FALSE(audio::normaliseDeviceState("not json").has_value());
    CHECK_FALSE(audio::normaliseDeviceState("{}").has_value());

    // The one answer both sides give for "newer than I read".
    CHECK(sdk::isFutureSchema(future.toStdString()));
    CHECK(ds::isFutureDeviceState(future));
}

TEST_CASE("A state a device reports lands on the document without losing the rest",
          "[device-state][sdk]") {
    ds::Doc doc;
    doc.deviceType = "magda_eq";
    doc.root.props.set("kept", 1);
    ds::Node band;
    band.type = "BAND";
    band.props.set("old", 1);
    doc.root.children.push_back(band);
    ds::Node other;
    other.type = "OTHER";
    doc.root.children.push_back(other);

    sdk::StateNode reported;
    reported.setBool("curveCollapsed", false);
    reported.addChild(sdk::StateNode("BAND")).setInt("new", 2);
    audio::applyReportedState(doc, reported);

    CHECK(static_cast<int>(doc.root.props["kept"]) == 1);
    CHECK_FALSE(static_cast<bool>(doc.root.props["curveCollapsed"]));
    REQUIRE(doc.root.children.size() == 2);
    CHECK(doc.root.children[0].type == "OTHER");
    CHECK(doc.root.children[1].type == "BAND");
    CHECK_FALSE(doc.root.children[1].props.contains("old"));
    CHECK(static_cast<int>(doc.root.children[1].props["new"]) == 2);
}

TEST_CASE("A device's own toggle reaches the host, and restoring it reports nothing",
          "[device-state][sdk]") {
    magda::DeviceInfo model;
    model.pluginId = audio::compiled::MagdaEqCompiledPlugin::xmlTypeName;
    auto engineDevice = audio::engine_adapter::createEngineDevice(model);
    auto* hosted = dynamic_cast<audio::engine_adapter::EngineMagdaDevice*>(engineDevice.get());
    REQUIRE(hosted != nullptr);
    auto* eq = dynamic_cast<audio::compiled::MagdaEqCompiledPlugin*>(&hosted->device());
    REQUIRE(eq != nullptr);

    std::vector<sdk::StateNode> reports;
    hosted->setStateReporter(
        [&reports](sdk::StateNode state) { reports.push_back(std::move(state)); });

    CHECK(eq->isCurveCollapsed());
    eq->setCurveCollapsed(false);
    REQUIRE(reports.size() == 1);
    CHECK_FALSE(reports[0].getBool("curveCollapsed", true));

    // What the host writes back is what a restore reads, and a restore is not a report.
    ds::Doc doc;
    doc.deviceType = model.pluginId.toStdString();
    audio::applyReportedState(doc, reports[0]);
    const auto state = audio::normaliseDeviceState(ds::encode(doc));
    REQUIRE(state.has_value());

    eq->setCurveCollapsed(true);
    reports.clear();
    CHECK(eq->restoreState(state->document.root).ok);
    CHECK_FALSE(eq->isCurveCollapsed());
    CHECK(reports.empty());
}
