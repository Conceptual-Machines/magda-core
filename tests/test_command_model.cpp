#include <catch2/catch_test_macros.hpp>
#include <string>

#include "command_model_parity_cases.hpp"
#include "magda/agents/command_model.hpp"

// Locks the hand-written C++ command-model backend to the Python POC reference:
// every one of the committed 102 test cases must produce byte-identical DSL to
// the float32 Python forward (prototypes/command-model-poc/, issue #1827).
// Regenerate the fixture + weights via model/export_cpp.py.

using magda::CommandModel;
using namespace magda::cmdmodel::test;

TEST_CASE("Command model reproduces the Python DSL byte-for-byte", "[agents][command_model]") {
    CommandModel model;
    int mismatches = 0;
    for (int i = 0; i < kNumParityCases; ++i) {
        const auto& c = kParityCases[i];
        std::string got = model.generate(c.input);
        if (got != c.expected) {
            ++mismatches;
            UNSCOPED_INFO("input:    " << c.input);
            UNSCOPED_INFO("expected: " << c.expected);
            UNSCOPED_INFO("got:      " << got);
        }
        CHECK(got == std::string(c.expected));
    }
    CHECK(mismatches == 0);
    CHECK(kNumParityCases == 111);
}
