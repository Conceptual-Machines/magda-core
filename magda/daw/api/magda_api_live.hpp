#pragma once

#include "alias_api_live.hpp"
#include "automation_api_live.hpp"
#include "clip_api_live.hpp"
#include "magda_api.hpp"
#include "project_api_live.hpp"
#include "selection_api_live.hpp"
#include "track_api_live.hpp"
#include "undo_api_live.hpp"

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
    TrackApi& tracks() override {
        return tracks_;
    }
    ClipApi& clips() override {
        return clips_;
    }
    ProjectApi& project() override {
        return project_;
    }
    UndoApi& undo() override {
        return undo_;
    }

  private:
    SelectionApiLive selection_;
    AutomationApiLive automation_;
    AliasApiLive aliases_;
    TrackApiLive tracks_;
    ClipApiLive clips_;
    ProjectApiLive project_;
    UndoApiLive undo_;
};

}  // namespace magda
