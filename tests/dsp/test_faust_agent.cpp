#include <catch2/catch_test_macros.hpp>

#include "magda/agents/faust_agent.hpp"

using magda::FaustAgent;

TEST_CASE("Faust instrument validation accepts reserved voice controls", "[agents][faust]") {
    const std::string source = R"FAUST(
declare name "Test Voice";
declare description "A saw voice for validation tests.";
import("stdfaust.lib");
freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 0.5, 0, 1, 0.01);
gate = button("gate");
attack = hslider("Attack [idx:0]", 0.01, 0.001, 2, 0.001);
voice = os.sawtooth(freq) * en.adsr(attack, 0.1, 0.8, 0.2, gate) * gain;
process = voice <: _, _;
)FAUST";
    std::string error;
    CHECK(FaustAgent::validateSource(FaustAgent::Target::Instrument, source, error));
}

TEST_CASE("Faust instrument validation rejects missing or indexed reserved controls",
          "[agents][faust]") {
    std::string error;
    CHECK_FALSE(
        FaustAgent::validateSource(FaustAgent::Target::Instrument,
                                   "declare name \"Broken\";\n"
                                   "declare description \"Missing gate.\";\n"
                                   "freq = hslider(\"freq [idx:0]\", 440, 20, 20000, 0.01);\n"
                                   "gain = hslider(\"gain\", 0.5, 0, 1, 0.01);\n"
                                   "process = _;",
                                   error));
    CHECK(error.find("reserved voice control") != std::string::npos);
    CHECK(error.find("missing reserved gate") != std::string::npos);
}

TEST_CASE("Faust effect validation keeps the existing indexed control contract",
          "[agents][faust]") {
    std::string error;
    CHECK(FaustAgent::validateSource(FaustAgent::Target::Effect,
                                     "declare name \"Drive\";\n"
                                     "declare description \"A drive stage.\";\n"
                                     "drive = hslider(\"Drive [idx:0]\", 1, 0, 10, 0.1);\n"
                                     "process = _;",
                                     error));
}

// The Faust compiler accepts a patch with no declares at all, so nothing but
// this check stops a generated patch landing in the library with no name and
// no description to identify it by.
TEST_CASE("Faust validation requires the patch name and description declares", "[agents][faust]") {
    const std::string body = "drive = hslider(\"Drive [idx:0]\", 1, 0, 10, 0.1);\n"
                             "process = _;";

    std::string error;
    CHECK_FALSE(FaustAgent::validateSource(FaustAgent::Target::Effect, body, error));
    CHECK(error.find("declare name") != std::string::npos);
    CHECK(error.find("declare description") != std::string::npos);

    error.clear();
    CHECK_FALSE(FaustAgent::validateSource(FaustAgent::Target::Effect,
                                           "declare name \"Drive\";\n" + body, error));
    CHECK(error.find("declare description") != std::string::npos);

    // Present but empty is the same as absent: readPatchInfo would hand the
    // library a blank string either way.
    error.clear();
    CHECK_FALSE(FaustAgent::validateSource(FaustAgent::Target::Effect,
                                           "declare name \"Drive\";\n"
                                           "declare description \"\";\n" +
                                               body,
                                           error));
    CHECK(error.find("declare description") != std::string::npos);
}

TEST_CASE("Faust instrument validation requires the metadata declares too", "[agents][faust]") {
    std::string error;
    CHECK_FALSE(FaustAgent::validateSource(FaustAgent::Target::Instrument,
                                           "import(\"stdfaust.lib\");\n"
                                           "freq = hslider(\"freq\", 440, 20, 20000, 0.01);\n"
                                           "gain = hslider(\"gain\", 0.5, 0, 1, 0.01);\n"
                                           "gate = button(\"gate\");\n"
                                           "process = os.sawtooth(freq) * gain * gate <: _, _;",
                                           error));
    CHECK(error.find("declare name") != std::string::npos);
    CHECK(error.find("declare description") != std::string::npos);
}
