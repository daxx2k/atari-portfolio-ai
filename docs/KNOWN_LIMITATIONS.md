# Known limitations

- This prerelease is tested on one Portfolio/Serial Interface and one Waveshare ESP32-S3-CAM-OV5640 V1.1 assembly.
- Camera preview is a low-bandwidth monochrome serial view, not real-time video. Coarse `/live` trades detail for speed.
- AI generation, speech startup and radio reconnection depend on Wi-Fi and service response times. Fast control acknowledgement does not mean the new radio audio is already buffered.
- Source level matching has synthetic waveform tests and Operator listening feedback. No acoustic SPL calibration was performed; tracks and TTS phrases can still differ perceptually.
- Credentials are stored in a plain card text file and ESP32 NVS. They are not included in the repository or binary defaults.
- A first-run migration enables voice once. Later intentional mute settings are preserved; startup does not forcibly unmute every time.
- Configuration transfer is checksum-checked and read back. A storage failure can still require a retry; it is not an encrypted credential-management system.
- Other ESP32 boards, power circuitry, speakers and firmware cores are not validated by this release.
- The ESP32 microSD cannot be used with the selected serial pins in this build.
