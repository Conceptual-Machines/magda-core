#pragma once

#include "alias_api_live.hpp"
#include "automation_api_live.hpp"
#include "magda_api.hpp"
#include "selection_api_live.hpp"

namespace magda {

/// Live implementation: every sub-interface forwards to the matching MAGDA singleton.
class MagdaApiLive : public MagdaApi {
  public:
    SelectionApi& selection() override {
        return selection_;
    }
    AutomationApi& automation() override {
        return automation_;
    }
    AliasApi& aliases() override {
        return aliases_;
    }

  private:
    SelectionApiLive selection_;
    AutomationApiLive automation_;
    AliasApiLive aliases_;
};

}  // namespace magda
