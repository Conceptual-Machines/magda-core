// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "alias_api.hpp"

namespace magda {

/// Forwards every AliasApi call to the live alias / resolver registry singletons.
class AliasApiLive : public AliasApi {
  public:
    AliasRegistry& aliasRegistry() override;
    ResolverRegistry& resolverRegistry() override;
};

}  // namespace magda
