---
name: svg-button
description: Add SVG icon buttons to the UI. Use when creating or modifying buttons that use SVG icons from the assets directory.
---

# SVG Buttons

## SvgButton Class

Located at `magda/daw/ui/components/common/SvgButton.hpp` in namespace `magda`.

### Constructors

```cpp
// Single icon — colors icon based on hover/press/active state
SvgButton(const juce::String& buttonName, const char* svgData, size_t svgDataSize);

// Dual icon — separate off/on SVGs with pre-baked colors
SvgButton(const juce::String& buttonName, const char* offSvgData, size_t offSvgDataSize,
          const char* onSvgData, size_t onSvgDataSize);
```

### Usage Pattern

SvgButton requires constructor args, so use `std::unique_ptr`:

**Header** (include `ui/components/common/SvgButton.hpp`):
```cpp
std::unique_ptr<magda::SvgButton> myButton_;
```

**Constructor** (BinaryData generated from CMakeLists):
```cpp
myButton_ = std::make_unique<magda::SvgButton>("MyButton", BinaryData::icon_svg,
                                                BinaryData::icon_svgSize);
myButton_->setTooltip("Do something");
myButton_->onClick = [this] { /* handler */ };
addAndMakeVisible(myButton_.get());
```

**resized()** — use pointer dereference:
```cpp
myButton_->setBounds(area.removeFromLeft(22));
```

### Customization

```cpp
// Colors (single-icon mode)
myButton_->setNormalColor(juce::Colour);
myButton_->setHoverColor(juce::Colour);
myButton_->setPressedColor(juce::Colour);
myButton_->setActiveColor(juce::Colour);

// Background colors
myButton_->setNormalBackgroundColor(juce::Colour);
myButton_->setActiveBackgroundColor(juce::Colour);

// Border
myButton_->setBorderColor(juce::Colour);
myButton_->setBorderThickness(float);

// Toggle state
myButton_->setActive(true);
```

Default colors come from DarkTheme: normal=TEXT_SECONDARY, hover=TEXT_PRIMARY, pressed/active=ACCENT_BLUE.

## Adding an SVG to BinaryData

1. Place the SVG file in `assets/icons/` (or a subdirectory)
2. Add it to the asset list in `magda/daw/CMakeLists.txt`:
   ```cmake
   juce_add_binary_data(MagdaAssets SOURCES
       ...
       ../../assets/icons/my_icon.svg
       ...
   )
   ```
3. Rebuild — CMake generates BinaryData with the naming convention:
   - `my_icon.svg` → `BinaryData::my_icon_svg` / `BinaryData::my_icon_svgSize`
   - `my-icon.svg` → `BinaryData::myicon_svg` (hyphens stripped)
   - `subdir/icon.svg` → same as `icon.svg` (path not part of name)

## Available Icon Locations

- `assets/icons/` — app-specific icons (random.svg, enter.svg, delete.svg, etc.)
- `assets/icons/fontaudio/` — FontAudio music icons (fad-*.svg)
- `assets/icons/IO/` — input/output icons
- `assets/icons/browser_icons/` — browser panel icons
- `assets/icons/cloud_providers/` — cloud service logos

## Dual-Icon Buttons (Toggle)

For buttons with distinct on/off visuals (e.g. play/pause):

```cpp
myButton_ = std::make_unique<magda::SvgButton>(
    "PlayPause",
    BinaryData::play_off_svg, BinaryData::play_off_svgSize,
    BinaryData::play_on_svg, BinaryData::play_on_svgSize);
myButton_->setClickingTogglesState(true);
```

No need to set LookAndFeel — SvgButton has its own `paintButton` override.
