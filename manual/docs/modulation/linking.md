# Linking Parameters

Linking connects modulation sources (modulators or macros) to target parameters on devices.

![Link Mode](../assets/images/modulation/link-mode.png)

## Link Mode

Link mode is the workflow for creating modulation connections:

1. **Enter link mode** — Click the link button in the toolbar or press the link mode shortcut
2. **Select a source** — Click a modulator or macro knob
3. **Click a target** — Click any device parameter knob or slider
4. **Adjust the link** — Set the modulation amount and polarity
5. **Exit link mode** — Press ++escape++ or click the link button again

## Drag and Drop

You can also create links by dragging from a modulation source directly onto a target parameter. This is a shortcut that skips the link-mode workflow.

## Link Settings

Each link has the following properties:

- **Amount** — How much the source affects the target (0–100%)
- **Bipolar** — When enabled, modulation swings both above and below the parameter's current value. When disabled, modulation only adds to the current value.

## Modulation Matrix

The modulation matrix provides an overview of all active links on a track:

- **Rows** represent sources (modulators and macros)
- **Columns** represent target parameters
- **Cells** show the link amount — click to edit or remove

## Visual Indicators

When modulation is active:

- **Animated rings** appear around modulated parameter knobs, showing the modulation range in real time
- **Color coding** distinguishes between different modulation sources
- Parameters with active modulation links display a small indicator dot
