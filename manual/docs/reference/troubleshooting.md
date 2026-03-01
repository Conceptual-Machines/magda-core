# Troubleshooting

## No Audio Output

1. Check **Settings > Audio** and verify the correct output device is selected
2. Make sure your system volume is turned up
3. Check that no tracks are muted and the master fader is up
4. Try a different sample rate (e.g., 44100 Hz or 48000 Hz)

## High Latency

1. Lower the buffer size in **Settings > Audio**
2. Use an ASIO driver (Windows) or Core Audio (macOS) for best performance
3. Close other audio applications
4. Switch to Live mode for the lowest latency profile

## Crashes on Startup

1. Try deleting the configuration file (`magda_config.txt`) to reset settings
2. Check the [GitHub Issues](https://github.com/Conceptual-Machines/magda-core/issues) for known problems
3. File a bug report with your system details and crash log

## Plugin Issues

1. Verify the plugin is compatible with your OS and architecture
2. Re-scan plugins from **Settings > Plugins**
3. Try loading the plugin in a fresh project

!!! tip
    If your issue isn't listed here, check the [GitHub Issues](https://github.com/Conceptual-Machines/magda-core/issues) page or open a new issue.
