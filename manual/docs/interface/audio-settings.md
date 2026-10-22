# Audio Settings

Open audio settings from **Settings > Audio Settings**. The dialog is organised into the sections below.

## Audio Device

- **Driver** — The audio system MAGDA talks to (Core Audio on macOS; ASIO, WASAPI or DirectSound on Windows; ALSA or JACK on Linux)
- **Output device** — Select your audio interface or built-in output
- **Input device** — Select the audio input for recording
- **Sample rate** — 44100 Hz and 48000 Hz are common choices
- **Buffer size** — Lower values reduce latency but increase CPU load

!!! tip
    MAGDA automatically optimizes buffer sizes when switching views: Live mode uses the lowest latency, while Mix and Arrange modes use larger buffers for stability.

!!! note "Silence after changing the sample rate"
    Many audio interfaces mute their outputs for a few seconds after a sample rate change while they re-sync their clock, even after the new rate is shown. Anything played in that window is lost, the metronome included, and the same happens with any other app on that interface. Wait a few seconds after switching before you press play or record.

## Linux Drivers

Most Linux desktops run PipeWire, which accepts JACK, PulseAudio and ALSA clients. Pick the driver for how you want to work:

| Driver | Device | Use it for |
|---|---|---|
| **JACK** | Your interface | Recording and production. Lowest latency through PipeWire (`pipewire-jack`) or a JACK server |
| **ALSA** | **Pulseaudio output** | Hearing MAGDA alongside the browser and other apps. Higher latency |
| **ALSA** | A `hw:` device | Direct hardware access. Other apps can't play while MAGDA holds the device |

There is no separate PulseAudio driver: the **Pulseaudio output** device under ALSA goes through PulseAudio, or through `pipewire-pulse` on PipeWire. It's listed when the ALSA PulseAudio plugin is installed (`libasound2-plugins` on Debian/Ubuntu, `alsa-plugins-pulseaudio` on Fedora/Arch).

!!! note "64 inputs and outputs under ALSA"
    On PipeWire, ALSA's default device can show up as 64 inputs and 64 outputs instead of your interface, and playback may break. Switch the driver to **JACK** to see the real interface.

## Channel Configuration

- **Active output channels** — Choose which output channels to use (stereo pair or multi-channel)
- **Active input channels** — Choose which input channels are available for recording
- **Preview** - the toggle beside each output stereo pair picks where browser auditioning is sent (see [Preview](../panels/browsers.md#preview)). Exactly one pair is always the preview destination, so selecting a new pair moves it rather than adding a second. Point it at a cue or headphone output to audition without going through your main monitors

## MIDI

- **MIDI input devices** — Enable or disable connected MIDI controllers and keyboards
- **MIDI output devices** — Enable or disable MIDI output to hardware synthesizers

## Preferred Device

- **Use as preferred device** — Check this box to remember your device selection and restore it automatically on startup. If the preferred device is unavailable, MAGDA falls back to the system default.
