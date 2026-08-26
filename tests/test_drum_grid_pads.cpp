#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cctype>
#include <cstring>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "magda/daw/core/DeviceInfo.hpp"
#include "magda/daw/core/DeviceState.hpp"
#include "magda/daw/core/DrumGridPads.hpp"
#include "magda/daw/core/RackInfo.hpp"

// Reading a Drum Grid's pads out of its saved state into the model (#2192).
//
// Both shapes a project on disk can carry are covered: the v2 device state
// document this build writes, and the pre-v2 engine XML that older projects
// still hold.

namespace {

namespace ds = magda::device_state;

ds::Node padNode(int index, int lowNote, int highNote, int rootNote) {
    ds::Node node;
    node.type = "CHAIN";
    node.props.set("index", index);
    node.props.set("name", "Pad " + juce::String(index));
    node.props.set("lowNote", lowNote);
    node.props.set("highNote", highNote);
    node.props.set("rootNote", rootNote);
    node.props.set("padLevel", -3.0f);
    node.props.set("padPan", 0.25f);
    node.props.set("padMute", true);
    node.props.set("padSolo", false);
    node.props.set("padBypassed", true);
    node.props.set("busOutput", 2);

    ds::Node plugin;
    plugin.type = "PLUGIN";
    plugin.props.set("type", "magdasampler");
    plugin.props.set("magdaDeviceId", 900 + index);
    plugin.props.set("enabled", false);
    node.children.push_back(plugin);

    return node;
}

juce::String encodedDrumGridWithPads() {
    ds::Doc doc;
    doc.deviceType = "drumgrid";
    doc.root.type = "PLUGIN";
    doc.root.children.push_back(padNode(0, 36, 36, 36));
    doc.root.children.push_back(padNode(1, 38, 40, 39));
    return ds::encode(doc);
}

}  // namespace

TEST_CASE("A Drum Grid's pads are read out of a v2 document", "[drumgrid][pads]") {
    const auto rack = magda::readPadRack("drumgrid", encodedDrumGridWithPads());

    REQUIRE(rack != nullptr);
    REQUIRE(rack->chains.size() == 2);

    const auto& second = rack->chains[1];
    CHECK(second.name == "Pad 1");
    CHECK(second.lowNote == 38);
    CHECK(second.highNote == 40);
    CHECK(second.rootNote == 39);
    CHECK_FALSE(second.answersToEveryNote());

    CHECK(second.volume == -3.0f);
    CHECK(second.pan == 0.25f);
    CHECK(second.muted);
    CHECK_FALSE(second.solo);
    CHECK(second.bypassed);
    CHECK(second.outputIndex == 2);

    REQUIRE(second.elements.size() == 1);
    REQUIRE(magda::isDevice(second.elements[0]));

    // A projected device has to be a real one. The plan keys an op on the
    // DeviceId and routes on the instrument flag, so a pad device that kept the
    // defaults would collide with every other pad and route nowhere.
    const auto& padDevice = magda::getDevice(second.elements[0]);
    CHECK(padDevice.pluginId == "magdasampler");
    CHECK(padDevice.id == 901);
    CHECK(padDevice.isInstrument);
    CHECK(padDevice.deviceType == magda::DeviceType::Instrument);
    CHECK(padDevice.bypassed);
    CHECK(padDevice.name == "Sampler");
}

TEST_CASE("Each pad's device keeps its own id", "[drumgrid][pads]") {
    const auto rack = magda::readPadRack("drumgrid", encodedDrumGridWithPads());
    REQUIRE(rack != nullptr);
    REQUIRE(rack->chains.size() == 2);

    std::set<magda::DeviceId> ids;
    for (const auto& chain : rack->chains) {
        REQUIRE(chain.elements.size() == 1);
        ids.insert(magda::getDevice(chain.elements[0]).id);
    }

    CHECK(ids.size() == 2);
    CHECK(ids.count(magda::INVALID_DEVICE_ID) == 0);
}

TEST_CASE("A pad device saved with no id stays invalid rather than inventing one",
          "[drumgrid][pads]") {
    ds::Doc doc;
    doc.deviceType = "drumgrid";
    doc.root.type = "PLUGIN";

    auto pad = padNode(0, 36, 36, 36);
    pad.children[0].props.remove("magdaDeviceId");
    doc.root.children.push_back(pad);

    const auto rack = magda::readPadRack("drumgrid", ds::encode(doc));
    REQUIRE(rack != nullptr);
    REQUIRE(rack->chains[0].elements.size() == 1);
    CHECK(magda::getDevice(rack->chains[0].elements[0]).id == magda::INVALID_DEVICE_ID);
}

TEST_CASE("A Drum Grid's pads are read out of pre-v2 engine XML", "[drumgrid][pads]") {
    const juce::String legacy = R"(<PLUGIN type="drumgrid" id="1234">
  <CHAIN index="0" name="Kick" lowNote="36" highNote="36" rootNote="36"
         padLevel="-6.0" padPan="-0.5" padMute="0" padSolo="1"
         padBypassed="0" busOutput="1">
    <PLUGIN type="magdasampler" id="5678"/>
  </CHAIN>
</PLUGIN>)";

    const auto rack = magda::readPadRack("drumgrid", legacy);

    REQUIRE(rack != nullptr);
    REQUIRE(rack->chains.size() == 1);

    const auto& pad = rack->chains[0];
    CHECK(pad.name == "Kick");
    CHECK(pad.lowNote == 36);
    CHECK(pad.highNote == 36);
    CHECK(pad.rootNote == 36);
    CHECK(pad.volume == -6.0f);
    CHECK(pad.pan == -0.5f);
    CHECK_FALSE(pad.muted);
    CHECK(pad.solo);
    CHECK(pad.outputIndex == 1);

    REQUIRE(pad.elements.size() == 1);
    CHECK(magda::getDevice(pad.elements[0]).pluginId == "magdasampler");
}

TEST_CASE("A device that is not a Drum Grid has no pads", "[drumgrid][pads]") {
    CHECK(magda::readPadRack("magda_reverb", encodedDrumGridWithPads()) == nullptr);
    CHECK(magda::readPadRack("drumgrid", "") == nullptr);
    CHECK(magda::readPadRack("drumgrid", "not a document at all") == nullptr);
    CHECK_FALSE(magda::isPadRackDevice("magda_reverb"));
    CHECK(magda::isPadRackDevice("drumgrid"));
}

TEST_CASE("A Drum Grid with no chains saved yields no pad rack", "[drumgrid][pads]") {
    ds::Doc doc;
    doc.deviceType = "drumgrid";
    doc.root.type = "PLUGIN";
    CHECK(magda::readPadRack("drumgrid", ds::encode(doc)) == nullptr);
}

TEST_CASE("refreshPadRack projects and clears a device's pads", "[drumgrid][pads]") {
    magda::DeviceInfo device;
    device.pluginId = "drumgrid";
    device.pluginState = encodedDrumGridWithPads();

    magda::refreshPadRack(device);
    REQUIRE(static_cast<bool>(device.padRack));
    CHECK(device.padRack->chains.size() == 2);

    // A device that stops being a Drum Grid must not keep the pads it had.
    device.pluginId = "magda_reverb";
    magda::refreshPadRack(device);
    CHECK_FALSE(static_cast<bool>(device.padRack));
}

TEST_CASE("A copied device does not share its pads", "[drumgrid][pads]") {
    magda::DeviceInfo device;
    device.pluginId = "drumgrid";
    device.pluginState = encodedDrumGridWithPads();
    magda::refreshPadRack(device);

    magda::DeviceInfo copy = device;
    REQUIRE(static_cast<bool>(copy.padRack));
    CHECK(copy.padRack.get() != device.padRack.get());

    // The note range has to survive the copy: a pad that came back answering to
    // every note would silently claim the whole keyboard.
    REQUIRE(copy.padRack->chains.size() == 2);
    CHECK(copy.padRack->chains[1].lowNote == 38);
    CHECK(copy.padRack->chains[1].highNote == 40);
    CHECK_FALSE(copy.padRack->chains[1].answersToEveryNote());
}

TEST_CASE("A projected internal pad device is internal", "[drumgrid][pads]") {
    const auto rack = magda::readPadRack("drumgrid", encodedDrumGridWithPads());
    REQUIRE(rack != nullptr);

    const auto& device = magda::getDevice(rack->chains[0].elements[0]);

    // Left at PluginFormat's VST3 default, an internal device claims a floating
    // editor window it does not have and the creation paths try to instantiate
    // it from external identifiers it has never had.
    CHECK(device.format == magda::PluginFormat::Internal);
    CHECK_FALSE(device.hasEditorWindow());
    CHECK(device.getFormatString() == "Internal");
}

TEST_CASE("An external pad plugin keeps its real identity", "[drumgrid][pads]") {
    ds::Doc doc;
    doc.deviceType = "drumgrid";
    doc.root.type = "PLUGIN";

    auto pad = padNode(0, 36, 36, 36);
    pad.children.clear();

    // What Tracktion actually saves: one type name for every external plugin,
    // with the format and the identity in the properties beside it.
    ds::Node external;
    external.type = "PLUGIN";
    external.props.set("type", "vst");
    external.props.set("name", "Kick 2");
    external.props.set("manufacturer", "Sonic Academy");
    external.props.set("format", "VST3");
    external.props.set("filename", "/Plug-Ins/VST3/Kick 2.vst3");
    external.props.set("uniqueId", "db742358");
    external.props.set("magdaDeviceId", 1020);
    external.props.set("magdaIsInstrument", true);
    pad.children.push_back(external);
    doc.root.children.push_back(pad);

    const auto rack = magda::readPadRack("drumgrid", ds::encode(doc));
    REQUIRE(rack != nullptr);
    REQUIRE(rack->chains[0].elements.size() == 1);

    const auto& device = magda::getDevice(rack->chains[0].elements[0]);
    CHECK(device.name == "Kick 2");
    CHECK(device.manufacturer == "Sonic Academy");
    CHECK(device.format == magda::PluginFormat::VST3);
    CHECK(device.fileOrIdentifier == "/Plug-Ins/VST3/Kick 2.vst3");
    CHECK(device.id == 1020);

    // The type name identifies nothing: every external plugin in the project
    // carries the same one.
    CHECK(device.pluginId != "vst");
    CHECK(device.pluginId.isNotEmpty());

    // Tracktion's own hash is not the JUCE identifier DeviceInfo::uniqueId
    // means, so it must not be copied into it.
    CHECK(device.uniqueId != "db742358");

    CHECK(device.isInstrument);
    CHECK(device.deviceType == magda::DeviceType::Instrument);
}

TEST_CASE("An external effect in a pad is not an instrument", "[drumgrid][pads]") {
    ds::Doc doc;
    doc.deviceType = "drumgrid";
    doc.root.type = "PLUGIN";

    auto pad = padNode(0, 36, 36, 36);
    ds::Node fx;
    fx.type = "PLUGIN";
    fx.props.set("type", "vst");
    fx.props.set("name", "Pro-Q 4");
    fx.props.set("format", "VST3");
    fx.props.set("filename", "/Plug-Ins/VST3/FabFilter Pro-Q 4.vst3");
    pad.children.push_back(fx);
    doc.root.children.push_back(pad);

    const auto rack = magda::readPadRack("drumgrid", ds::encode(doc));
    REQUIRE(rack != nullptr);
    REQUIRE(rack->chains[0].elements.size() == 2);

    const auto& effect = magda::getDevice(rack->chains[0].elements[1]);
    CHECK(effect.name == "Pro-Q 4");
    CHECK_FALSE(effect.isInstrument);
    CHECK(effect.deviceType == magda::DeviceType::Effect);
}

TEST_CASE("A pad's instrument is found by its flag, not its position", "[drumgrid][pads]") {
    // The model lets an effect be inserted at index 0, and lets plugins be moved
    // and removed. Reading position as the instrument would have this EQ
    // consuming the pad's MIDI and the sampler under it treated as an effect.
    ds::Doc doc;
    doc.deviceType = "drumgrid";
    doc.root.type = "PLUGIN";

    auto pad = padNode(0, 36, 36, 36);
    pad.children.clear();

    ds::Node effect;
    effect.type = "PLUGIN";
    effect.props.set("type", "vst");
    effect.props.set("name", "Pro-Q 4");
    effect.props.set("format", "VST3");
    effect.props.set("filename", "/Plug-Ins/VST3/Pro-Q 4.vst3");
    effect.props.set("magdaIsInstrument", false);
    pad.children.push_back(effect);

    ds::Node instrument;
    instrument.type = "PLUGIN";
    instrument.props.set("type", "vst");
    instrument.props.set("name", "Kick 2");
    instrument.props.set("format", "VST3");
    instrument.props.set("filename", "/Plug-Ins/VST3/Kick 2.vst3");
    instrument.props.set("magdaIsInstrument", true);
    pad.children.push_back(instrument);

    doc.root.children.push_back(pad);

    const auto rack = magda::readPadRack("drumgrid", ds::encode(doc));
    REQUIRE(rack != nullptr);
    REQUIRE(rack->chains[0].elements.size() == 2);

    const auto& first = magda::getDevice(rack->chains[0].elements[0]);
    CHECK(first.name == "Pro-Q 4");
    CHECK_FALSE(first.isInstrument);
    CHECK(first.deviceType == magda::DeviceType::Effect);

    const auto& second = magda::getDevice(rack->chains[0].elements[1]);
    CHECK(second.name == "Kick 2");
    CHECK(second.isInstrument);
    CHECK(second.deviceType == magda::DeviceType::Instrument);
}

TEST_CASE("An external pad plugin with no saved flag is not called an instrument",
          "[drumgrid][pads]") {
    // A wrong answer misroutes the pad; a missing one is a device the compiler
    // can report.
    ds::Doc doc;
    doc.deviceType = "drumgrid";
    doc.root.type = "PLUGIN";

    auto pad = padNode(0, 36, 36, 36);
    pad.children.clear();

    ds::Node unknown;
    unknown.type = "PLUGIN";
    unknown.props.set("type", "vst");
    unknown.props.set("name", "Kick 2");
    unknown.props.set("filename", "/Plug-Ins/VST3/Kick 2.vst3");
    pad.children.push_back(unknown);
    doc.root.children.push_back(pad);

    const auto rack = magda::readPadRack("drumgrid", ds::encode(doc));
    REQUIRE(rack != nullptr);
    CHECK_FALSE(magda::getDevice(rack->chains[0].elements[0]).isInstrument);
}

// The projection's field contract (#2205).
//
// A pad plugin has no DeviceInfo anywhere but this projection, so the fields
// `deviceFromNode()` fills are the whole of what a pad device is. Nothing in
// the type system says which those are: add a field to DeviceInfo and it simply
// arrives on a projected pad device at its default, and whatever was attached
// to it does nothing, with no crash and no diagnostic to say so.
//
// So the contract is written out in DrumGridPads.hpp and classified here.
// Reading DeviceInfo's members out of the header is unusual and is the point:
// the fact being checked is which fields exist, which no runtime value reports.

namespace {

/// Where a projected pad device's value for a field comes from, or why it has
/// none. The reasons are the ones DrumGridPads.hpp gives.
enum class Carriage {
    FromState,     ///< Read out of the Drum Grid's saved state.
    FromLiveGrid,  ///< Refilled from the live plugin by populatePadDeviceParameters().
    CannotOwn,     ///< Written only through a path that reaches the chain model.
    StateSilent,   ///< Only the live plugin could answer it, and it is not asked.
    UiOnly,        ///< UI or session state a pad device has no surface for.
};

struct FieldContract {
    const char* field;
    Carriage carriage;
};

/// Every field of DeviceInfo, and what a projected pad device does with it.
constexpr FieldContract kPadDeviceContract[] = {
    {"id", Carriage::FromState},
    {"name", Carriage::FromState},
    {"pluginId", Carriage::FromState},
    {"manufacturer", Carriage::FromState},
    {"format", Carriage::FromState},
    {"isInstrument", Carriage::FromState},
    {"deviceType", Carriage::FromState},
    {"fileOrIdentifier", Carriage::FromState},
    {"bypassed", Carriage::FromState},
    {"pluginState", Carriage::FromState},
    {"canReceiveMidi", Carriage::FromState},
    {"producesMidi", Carriage::FromState},

    {"parameters", Carriage::FromLiveGrid},
    {"wrapperParameters", Carriage::FromLiveGrid},
    {"meters", Carriage::FromLiveGrid},

    {"macros", Carriage::CannotOwn},
    {"mods", Carriage::CannotOwn},
    {"sidechain", Carriage::CannotOwn},
    {"multiOut", Carriage::CannotOwn},
    {"deltaSolo", Carriage::CannotOwn},
    {"midiInThru", Carriage::CannotOwn},
    {"kitRows", Carriage::CannotOwn},
    {"gainValue", Carriage::CannotOwn},
    {"gainDb", Carriage::CannotOwn},

    {"audioInputChannels", Carriage::StateSilent},
    {"audioOutputChannels", Carriage::StateSilent},
    {"canSidechain", Carriage::StateSilent},
    {"uniqueId", Carriage::StateSilent},
    {"vst3ClassId", Carriage::StateSilent},
    {"vst3Preset", Carriage::StateSilent},

    {"browserCategoryOverride", Carriage::UiOnly},
    {"expanded", Carriage::UiOnly},
    {"modPanelOpen", Carriage::UiOnly},
    {"gainPanelOpen", Carriage::UiOnly},
    {"paramPanelOpen", Carriage::UiOnly},
    {"aiPanelOpen", Carriage::UiOnly},
    {"aiPanelOutput", Carriage::UiOnly},
    {"aiConversation", Carriage::UiOnly},
    {"visibleParameters", Carriage::UiOnly},
    {"miniMixerParameters", Carriage::UiOnly},
    {"aiSoundDesignerParameters", Carriage::UiOnly},
    {"aiSoundDesignerPrompt", Carriage::UiOnly},
    {"currentParameterPage", Carriage::UiOnly},
    {"loadState", Carriage::UiOnly},
    {"padRack", Carriage::UiOnly},
};

/// The source with its comments, string and character literals removed, so a
/// brace or a semicolon inside one cannot be read as structure.
std::string withoutCommentsAndLiterals(const std::string& text) {
    const auto starts = [&text](std::size_t at, const char* token) {
        return text.compare(at, std::strlen(token), token) == 0;
    };

    std::string out;
    out.reserve(text.size());

    for (std::size_t i = 0; i < text.size();) {
        if (starts(i, "//")) {
            while (i < text.size() && text[i] != '\n')
                ++i;
        } else if (starts(i, "/*")) {
            i += 2;
            while (i < text.size() && !starts(i, "*/"))
                ++i;
            i = std::min(i + 2, text.size());
        } else if (text[i] == '"' || text[i] == '\'') {
            const auto quote = text[i++];
            while (i < text.size() && text[i] != quote)
                i += text[i] == '\\' ? 2 : 1;
            ++i;
        } else {
            out += text[i++];
        }
    }

    return out;
}

/// The declared name in `juce::String name` or `int channels = 2`: the last
/// identifier before the initialiser, whatever the type in front of it is.
std::string lastIdentifier(const std::string& declarator) {
    std::string current, last;

    for (const auto c : declarator) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_') {
            current += c;
            continue;
        }
        if (!current.empty())
            last = current;
        current.clear();
    }

    return current.empty() ? last : current;
}

/// Every data member declared directly in `struct name`, in declaration order.
///
/// Member functions are not members here, and neither is anything a body
/// contains: only the depth the struct's own declarations sit at is read.
std::vector<std::string> fieldsOfStruct(const std::string& source, const std::string& name) {
    std::vector<std::string> fields;
    const auto declaration = "struct " + name;

    // Past any forward declaration: the body is the one an opening brace
    // follows.
    std::size_t body = std::string::npos;
    for (auto at = source.find(declaration); at != std::string::npos;
         at = source.find(declaration, at + 1)) {
        auto after = at + declaration.size();
        while (after < source.size() &&
               std::isspace(static_cast<unsigned char>(source[after])) != 0)
            ++after;
        if (after < source.size() && source[after] == '{') {
            body = after;
            break;
        }
    }

    if (body == std::string::npos)
        return fields;

    int depth = 0;
    std::string statement, beforeBrace;

    for (auto i = body; i < source.size(); ++i) {
        const auto c = source[i];

        if (c == '{') {
            if (depth == 1)
                beforeBrace = statement;
            ++depth;
            continue;
        }

        if (c == '}') {
            if (--depth == 0)
                break;
            // A brace after a member function's parameter list opens its body,
            // and the declaration ends with it. Anywhere else it is a member's
            // initialiser, and the declaration runs on to its semicolon.
            if (depth == 1)
                statement =
                    beforeBrace.find('(') == std::string::npos ? beforeBrace : std::string();
            continue;
        }

        if (depth != 1)
            continue;

        if (c != ';') {
            statement += c;
            continue;
        }

        const auto declarator = statement.substr(0, statement.find('='));
        statement.clear();

        // A '(' ahead of any initialiser makes it a function declaration.
        if (declarator.find('(') != std::string::npos)
            continue;

        if (auto field = lastIdentifier(declarator); !field.empty())
            fields.push_back(std::move(field));
    }

    return fields;
}

std::string headerText(const juce::String& path) {
    const auto file = juce::File(MAGDA_REPO_ROOT).getChildFile(path);
    REQUIRE(file.existsAsFile());
    return file.loadFileAsString().toStdString();
}

/// The stretch of the contract in DrumGridPads.hpp that lists one carriage's
/// fields: from the sentence introducing it to whatever comes next.
std::string contractSection(const std::string& header, Carriage carriage) {
    // In the order the header states them, so each section ends where the next
    // begins and the last one runs to the end of the block.
    constexpr struct {
        Carriage carriage;
        const char* opening;
    } kSections[] = {
        {Carriage::FromState, "Carried, out of the saved state:"},
        {Carriage::FromLiveGrid, "Carried, out of the live grid:"},
        {Carriage::CannotOwn, "Absent because a pad device cannot own it:"},
        {Carriage::StateSilent, "Absent because the saved state does not say:"},
        {Carriage::UiOnly, "Absent because it is UI or session state"},
    };

    for (std::size_t i = 0; i < std::size(kSections); ++i) {
        if (kSections[i].carriage != carriage)
            continue;

        const auto from = header.find(kSections[i].opening);
        if (from == std::string::npos)
            return {};

        const auto to = i + 1 < std::size(kSections) ? header.find(kSections[i + 1].opening, from)
                                                     : header.find("*/", from);
        return header.substr(from, to == std::string::npos ? std::string::npos : to - from);
    }

    return {};
}

bool classified(const std::string& field) {
    return std::any_of(std::begin(kPadDeviceContract), std::end(kPadDeviceContract),
                       [&field](const FieldContract& entry) { return field == entry.field; });
}

}  // namespace

TEST_CASE("Every DeviceInfo field is classified by the pad projection's contract",
          "[drumgrid][pads][contract]") {
    const auto fields = fieldsOfStruct(
        withoutCommentsAndLiterals(headerText("magda/daw/core/DeviceInfo.hpp")), "DeviceInfo");

    // If this trips, the scanner stopped finding the struct rather than the
    // struct losing its fields.
    REQUIRE(fields.size() > 20);

    std::vector<std::string> unclassified;
    for (const auto& field : fields)
        if (!classified(field))
            unclassified.push_back(field);

    if (!unclassified.empty()) {
        std::string report =
            "DeviceInfo has fields the Drum Grid pad projection has not been taught about:\n";
        for (const auto& field : unclassified)
            report += "  " + field + "\n";
        report +=
            "\nDecide whether a projected pad device carries each one, say so in the contract "
            "in DrumGridPads.hpp, and add it to kPadDeviceContract. Carrying one also means "
            "teaching every walk that collects it to descend into padRack.";
        UNSCOPED_INFO(report);
    }
    CHECK(unclassified.empty());

    std::vector<std::string> stale;
    for (const auto& entry : kPadDeviceContract)
        if (std::find(fields.begin(), fields.end(), entry.field) == fields.end())
            stale.push_back(entry.field);

    if (!stale.empty()) {
        std::string report =
            "The pad projection's contract names fields DeviceInfo no longer has:\n";
        for (const auto& field : stale)
            report += "  " + field + "\n";
        UNSCOPED_INFO(report);
    }
    CHECK(stale.empty());
}

TEST_CASE("The contract in DrumGridPads.hpp names every field where it classifies it",
          "[drumgrid][pads][contract]") {
    // The table above is what fails the build; the header is where someone
    // reads why. A field classified in one and missing from the other leaves
    // the reason unwritten, which is the state this contract exists to end.
    const auto header = headerText("magda/daw/core/DrumGridPads.hpp");

    std::vector<std::string> undocumented;
    for (const auto& entry : kPadDeviceContract) {
        const auto section = contractSection(header, entry.carriage);
        if (section.find("`" + std::string(entry.field) + "`") == std::string::npos)
            undocumented.push_back(entry.field);
    }

    if (!undocumented.empty()) {
        std::string report =
            "Fields the table classifies that the matching paragraph of DrumGridPads.hpp does "
            "not name:\n";
        for (const auto& field : undocumented)
            report += "  " + field + "\n";
        UNSCOPED_INFO(report);
    }
    CHECK(undocumented.empty());
}

TEST_CASE("The field scanner reads declarations and not bodies", "[drumgrid][pads][contract]") {
    // Without this the guard above is worth nothing: a scanner that counted a
    // member function's locals would demand they be classified, and one that
    // stopped at the first function body would never see the fields after it.
    const std::string source = R"(
struct DeviceInfo {
    int declared = 0;
    juce::String name;  // int inAComment;
    std::vector<int> braceInitialised{};
    MacroArray macros = createDefaultMacros();

    int findParameterByIndex(int index) {
        int local = 0;
        return local;
    }

    juce::String getFormatString() const { return "}; int inALiteral;"; }

    int afterABody = 1;
};
)";

    const auto fields = fieldsOfStruct(withoutCommentsAndLiterals(source), "DeviceInfo");
    CHECK(fields ==
          std::vector<std::string>{"declared", "name", "braceInitialised", "macros", "afterABody"});
}

TEST_CASE("The field scanner skips a forward declaration", "[drumgrid][pads][contract]") {
    const std::string source = R"(
struct DeviceInfo;
struct Other { int notMine = 0; };
struct DeviceInfo {
    int mine = 0;
};
)";

    CHECK(fieldsOfStruct(withoutCommentsAndLiterals(source), "DeviceInfo") ==
          std::vector<std::string>{"mine"});
}
