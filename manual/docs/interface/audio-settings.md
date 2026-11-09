# Audio & MIDI

The **Audio & MIDI** section of [Settings](preferences.md) chooses the audio driver and interface MAGDA plays through, which of its channels are open, and which MIDI ports MAGDA uses. Every change here takes effect as you make it: there is nothing to apply, and the window offers only **Done**.

MAGDA reopens the driver and interface you chose last time when it starts.

## Status bar

The bar at the top names the interface that is open, with its channel count (for example `2 in · 2 out`), sample rate, buffer size in samples, and the latency that buffer adds in milliseconds. Its dot is green while the interface is open.

When the driver has a settings window of its own, as ASIO drivers on Windows do, a **Control Panel…** button opens it. Use it for options the driver keeps to itself, such as its buffer size.

## Device

- **Driver** — The audio system MAGDA talks to: Core Audio on macOS; ASIO, WASAPI or DirectSound on Windows; ALSA or JACK on Linux. The driver decides which interfaces the pickers below list.
- **Input Interface** / **Output Interface** — The interface to record from and the one to play through. Drivers that open one interface for both directions show a single **Interface** picker.
- **Sample Rate** — 44100 Hz and 48000 Hz are common choices.
- **Buffer Size** — Lower values reduce latency but increase CPU load.

!!! note "Silence after changing the sample rate"
    Many audio interfaces mute their outputs for a few seconds after a sample rate change while they re-sync their clock, even after the new rate is shown. Anything played in that window is lost, the metronome included, and the same happens with any other app on that interface. Wait a few seconds after switching before you press play or record.

If MAGDA closes unexpectedly while opening an audio driver, it starts the next time with the default driver instead and tells you which one it skipped. Choose a driver again here once you have dealt with the problem.

## Audio Inputs and Audio Outputs

The two channel lists show the open interface's inputs and outputs, stereo pairs first and then single channels. Tick the ones MAGDA should use: a ticked stereo pair opens both channels as a pair, and ticking the two channels of a pair separately opens them as two mono channels.

Double-click a channel's name to rename it, or right-click it for **Rename** and **Clear Name**. MAGDA then shows your names in place of the driver's.

## Preview

- **Audition browser files on** — The output pair that browser auditioning plays through (see [Preview](../panels/browsers.md#preview)). Point it at a cue or headphone output to audition without going through your main monitors.

## MIDI Inputs and MIDI Output

- **MIDI Inputs** — Every MIDI input MAGDA can see, each with its own switch. Turn on the controllers and keyboards you play.
- **MIDI Output** — The default MIDI output, or **<< none >>**.
- **Bluetooth MIDI…** — Pairs a Bluetooth MIDI device, on systems that support it.

[MIDI clock](connections.md#midi-clock) and [controller profiles](controllers.md) choose their own ports in their sections of Settings.

## Linux drivers

Most Linux desktops run PipeWire, which accepts JACK, PulseAudio and ALSA clients. Pick the driver for how you want to work:

| Driver | Device | Use it for |
|---|---|---|
| **JACK** | Your interface | Recording and production. Lowest latency, through PipeWire (`pipewire-jack`) or a JACK server |
| **ALSA** | **Pulseaudio output** | Hearing MAGDA alongside the browser and other apps. Higher latency |
| **ALSA** | A `hw:` device | Direct hardware access. Other apps can't play while MAGDA holds the device |

Under JACK, MAGDA lists a card's own physical inputs and outputs only, named **channel 1** onwards, and picks your hardware rather than another application. On PipeWire it uses PipeWire's JACK support directly, so you don't need to start MAGDA through `pw-jack`.

There is no separate PulseAudio driver: the **Pulseaudio output** device under ALSA goes through PulseAudio, or through `pipewire-pulse` on PipeWire. It's listed when the ALSA PulseAudio plugin is installed (`libasound2-plugins` on Debian/Ubuntu, `alsa-plugins-pulseaudio` on Fedora/Arch).

!!! note "64 inputs and outputs under ALSA"
    On PipeWire, ALSA's default device can show up as 64 inputs and 64 outputs instead of your interface, and playback may break. Switch the driver to **JACK** to see the real interface.
