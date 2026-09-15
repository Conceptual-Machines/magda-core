# Chain selection and layout

Single-click selection of devices, racks, and Drum Grid pads must preserve collapse and panel state.
Selection listeners update selection-dependent visuals and context; they do not
open controller/modulation panels or toggle collapse. An unmodified double-click on an already-selected node background/name toggles
collapse. Child controls keep their own double-click behavior. Panel changes belong
to the panel buttons (or explicit commands). Do not add temporary
click flags to suppress selection side effects.

Run the component regression check on macOS with an existing Ninja build:

```sh
python3 tests/ui/run_chain_interaction_check.py --build-dir cmake-build-debug
```

This standalone check uses the actual DeviceSlotComponent, RackComponent and
PadDeviceSlot classes. It covers first/repeated selection, header/body clicks,
programmatic selection, modifier clicks, collapsed selection, double-click collapse/expand,
and preservation of open panels across selection changes. It checks that the
panel buttons have bounds and are not obscured by other children.

The runner builds the app libraries and UI objects needed by the test, then links
a separate test entry point. It does not link or launch MAGDA. Keep CI idle when
using the shared build directory. This check is separate from the regular
magda_juce_tests target, which does not link the app's chain UI components.
