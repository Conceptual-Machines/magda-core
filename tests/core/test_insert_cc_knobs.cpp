// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "core/InsertCcKnobs.hpp"

/// @file An External Insert's CC knobs, as the model holds them (#3164).

using namespace magda;

TEST_CASE("A new CC knob takes the lowest CC the insert does not send yet",
          "[core][insert][3164]") {
    DeviceInfo device;
    const int first = insert_cc::addKnob(device);
    const int second = insert_cc::addKnob(device);

    REQUIRE(device.insert.ccSlots.size() == 2);
    CHECK(insert_cc::findKnob(device, first)->ccNumber == 1);
    CHECK(insert_cc::findKnob(device, second)->ccNumber == 2);

    const auto* param = device.findParameterByIndex(second);
    REQUIRE(param != nullptr);
    CHECK(param->name == "CC 2");
    CHECK(param->maxValue == 127.0f);

    insert_cc::setKnobCc(device, second, 74);
    CHECK(insert_cc::findKnob(device, second)->ccNumber == 74);
    CHECK(device.findParameterByIndex(second)->name == "CC 74");
    CHECK(insert_cc::addKnob(device) >= 0);
    CHECK(device.insert.ccSlots.back().ccNumber == 2);
}

TEST_CASE("A removed knob's parameter index is never handed out again", "[core][insert][3164]") {
    DeviceInfo device;
    insert_cc::addKnob(device);
    const int last = insert_cc::addKnob(device);
    insert_cc::removeKnob(device, last);

    CHECK(insert_cc::findKnob(device, last) == nullptr);
    CHECK(device.findParameterByIndex(last) == nullptr);
    CHECK(insert_cc::addKnob(device) > last);
}

TEST_CASE("An insert holds at most four pages of knobs", "[core][insert][3164]") {
    DeviceInfo device;
    for (int i = 0; i < insert_cc::kMaxKnobs; ++i)
        REQUIRE(insert_cc::addKnob(device) >= 0);
    CHECK(insert_cc::addKnob(device) == -1);
    CHECK(device.insert.ccSlots.size() == insert_cc::kMaxKnobs);
}
