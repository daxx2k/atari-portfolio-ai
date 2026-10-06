# Atari Portfolio AI

An Atari Portfolio terminal connected through its original Serial Interface to a Waveshare ESP32-S3-CAM-OV5640. The ESP32 provides online answers, synthetic speech, microphone conversation, camera images and synthwave/vaporwave radio. The English terminal uses a SKYNET theme and addresses its user as Operator.

## First private prerelease: v0.4.15

This release pairs **firmware 0.4.15** with **Portfolio client 0.4.14**. The two version numbers are independent; do not replace the working client just to match the firmware number.

- Typed answers with streamed speech and a robot effect.
- Automatic microphone conversation with text on the Portfolio screen.
- Detailed `/cam` and coarse `/live` camera previews; `/look` describes a captured photograph.
- Five radio stations, fast arrow controls, background playback and speech interruption/resume.
- Shared speaker volume 0-100, source level balancing, mute and saved preferences.
- Card-based setup and `/config`, with time zone and location for time, weather and news.

This is a tested hardware prototype, not a general-purpose finished product. See [validation](docs/VALIDATION.md) and [known limitations](docs/KNOWN_LIMITATIONS.md).

## Hardware

- Atari Portfolio and original Serial Interface.
- apfRAM or compatible Portfolio memory card for `AI.COM` and `AISETUP.TXT`.
- Waveshare ESP32-S3-CAM-OV5640 V1.1, camera, speaker and optional battery.
- MAX3232/SP3232 RS-232 level converter. **Never connect the Portfolio RS-232 signals directly to ESP32 GPIO.**

Remove any ESP32 microSD: this firmware uses GPIO43/44, which share the board's microSD wiring. See [hardware and power](docs/HARDWARE.md).

## Setup

1. Build/upload the firmware using [BUILD.md](docs/BUILD.md), or use the attached prerelease binaries for this exact board.
2. Copy `portfolio/AI.COM` to the Portfolio card.
3. Copy `config/AISETUP.EXAMPLE.TXT` as `AISETUP.TXT`, replacing the example location, Wi-Fi credentials and OpenAI key with your own values.
4. Disconnect the card from USB before inserting it in the Portfolio. Never use card USB while it is inserted in the Portfolio.
5. Run `AI`, then `/test`, `/status` and `/help`. Configure or edit values through `/config`; `/reload` applies the card file.

`AISETUP.TXT`, its backup, and ESP32 NVS hold credentials in plaintext. Keep them private. The example file is the only configuration included in Git. OpenAI requests and radio require an internet connection; OpenAI usage is billed to the configured account.

## Controls

See the full [command reference](docs/COMMANDS.md). Type a question and press Enter. Shift+Up/Down changes the shared speaker volume by 5. Defaults are voice on, robot effect on, volume 40. In `/radio`, Left/Right selects a station, Up/Down adjusts volume and Esc returns to the prompt with music continuing. `/radio off` stops it.

## Development

The source is split between `firmware/PortfolioAI/` and `portfolio/AI.ASM`. Local tests use the actual DOS binary in 16-bit emulation and the production DSP header. GitHub Actions checks the DOS build, client behavior, DSP and accidental credential patterns. Firmware build settings and tool versions are pinned in `release.json`.

No project-wide public license has been selected for this private prerelease. Bundled third-party code retains its original notices; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
