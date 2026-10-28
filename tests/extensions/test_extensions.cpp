// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/extensions/Extension.hpp"

namespace ext = magda::extensions;

TEST_CASE("Extensions are created and destroyed without being started", "[extensions]") {
    ext::createExtensions();
    const auto ids = ext::loadedExtensionIds();
#if defined(MAGDA_STUB_EXTENSION) && MAGDA_STUB_EXTENSION
    CHECK(std::find(ids.begin(), ids.end(), juce::String("stub")) != ids.end());
#endif
    CHECK(ext::currentEdition().productName.isNotEmpty());

    ext::destroyExtensions();
    CHECK(ext::loadedExtensionIds().empty());
    CHECK(ext::currentEdition().productName == "MAGDA");
}

TEST_CASE("A null factory is refused", "[extensions]") {
    CHECK_FALSE(ext::registerExtension(nullptr));
}
