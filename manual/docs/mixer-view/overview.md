# Mixer View

The Mixer view provides a channel-strip interface for balancing levels, panning, routing, and applying effects. Switch to it by clicking **Mix** in the footer bar.

## Layout

```
┌──────────┬──────────┬──────────┬──────────┬─────────┐
│ Track 1  │ Track 2  │ Track 3  │ Aux 1    │ Master  │
│          │          │          │          │         │
│ [Routing]│ [Routing]│ [Routing]│ [Routing]│         │
│ [Sends]  │ [Sends]  │ [Sends]  │ [Sends]  │         │
│          │          │          │          │         │
│  ┃  ┃    │  ┃  ┃    │  ┃  ┃    │  ┃  ┃    │  ┃  ┃   │
│  ┃██┃    │  ┃█ ┃    │  ┃  ┃    │  ┃█ ┃    │  ┃██┃   │
│  ┃██┃    │  ┃██┃    │  ┃█ ┃    │  ┃██┃    │  ┃██┃   │
│  ┃██┃    │  ┃██┃    │  ┃██┃    │  ┃██┃    │  ┃██┃   │
│  [Pan]   │  [Pan]   │  [Pan]   │  [Pan]   │  [Pan]  │
│  M S R   │  M S R   │  M S R   │  M S     │         │
│ Track 1  │ Track 2  │ Track 3  │ Aux 1    │ Master  │
└──────────┴──────────┴──────────┴──────────┴─────────┘
```

- **Channel strips** — One per track, scrollable horizontally
- **Aux channel strips** — Auxiliary/bus tracks
- **Master channel strip** — Final output, fixed on the right
- **Resizable width** — Drag the edge of a channel strip to resize (80–160 px)

## Selection

Click a channel strip to select it. The selected channel is visually highlighted.

## Drag and Drop

Drag plugins onto a channel strip to add them to the track's effects chain.

## What's Next

- [Channel Strips](channel-strips.md) — Volume, pan, mute, solo, and metering
- [Routing & Sends](routing.md) — I/O routing and send configuration
