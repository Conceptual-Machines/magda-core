// Single owner of Faust's GUI base statics.
//
// faust/dsp/poly-dsp.h pulls in faust/gui/GUI.h (GroupUI : public GUI), which
// declares `GUI::fGuiList` and `GUI::gTimedZoneMap` but does not define them —
// each program that links the poly runtime must provide exactly one definition.
// Several TUs include poly-dsp.h (the interpreter Faust instrument and any
// compiled Faust instruments), so the definitions live here in one place to
// avoid both "undefined symbol" (zero definitions) and "duplicate symbol"
// (one per TU).

#include "faust/gui/GUI.h"

std::list<GUI*> GUI::fGuiList;
ztimedmap GUI::gTimedZoneMap;
