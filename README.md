# ChatGPT on Atari Portfolio

[![Buy me a coffee](https://img.shields.io/badge/Buy_me_a_coffee-Support_my_projects-FFDD00?logo=buymeacoffee&logoColor=000000)](https://buymeacoffee.com/daxx2k)

**Bring a 1989 handheld computer into the AI era.**

Type a question on the Atari Portfolio's original keyboard, read the reply on its LCD, and hear it spoken through an ESP32 speaker. You can also talk through a microphone, ask the AI to describe what the camera sees, or listen to synthwave and vaporwave radio.

The interface is entirely in English, with a Terminator-inspired SKYNET theme that addresses you as **Operator**.

## How it works

The Portfolio runs a small DOS client, `AI.COM`, and connects to an external ESP32 module through its original Serial Interface. The ESP32 handles Wi-Fi, requests to the OpenAI API, the camera, microphone and speaker.

| Part | What it does |
| --- | --- |
| Atari Portfolio | Keyboard input, terminal display and command controls |
| Original Serial Interface + MAX3232/SP3232 | RS-232 communication between the Portfolio and ESP32 |
| Waveshare ESP32-S3-CAM | Wi-Fi, camera capture, microphone recording, speech and radio playback |
| OpenAI API | AI replies, image descriptions and speech services |

The AI runs through online services; the Portfolio is the terminal. You need your own OpenAI API key and an internet connection. API usage is billed to the configured OpenAI account.

## What you can do

- **Chat:** type questions and receive text replies with spoken answers.
- **Talk:** hold a microphone conversation with automatic listening and the conversation displayed on the Portfolio.
- **Look:** capture a camera image and ask what it shows. “What's that?” and “What am I looking at?” also trigger image description.
- **Preview the camera:** use a monochrome preview on the Portfolio screen. A coarse mode trades detail for speed.
- **Listen to radio:** play synthwave, darksynth, chillsynth and vaporwave stations. Music can continue in the background, pause for a spoken reply and resume afterwards.
- **Check local information:** get time, location, weather, news and report sources using your configured location and time zone.
- **Make it yours:** edit Wi-Fi, API credentials, location and saved audio options through a card text file or the terminal's `/config` mode.

Speech and the robot voice effect are enabled by default, with speaker volume set to **40/100**. Intentional mute and volume changes are saved.

## Hardware you need

- An Atari Portfolio with its original Serial Interface.
- An apfRAM or compatible memory card for the Portfolio client and configuration.
- A Waveshare **ESP32-S3-CAM-OV5640 V1.1**.
- An **RS-232 level converter**, such as MAX3232 or SP3232.
- A speaker, the board's microphones and camera, and an optional battery.

**Do not connect RS-232 signals directly to ESP32 GPIO.** This build uses GPIO43/44 for serial communication, so the ESP32 microSD slot must remain empty.

See [Hardware and power](docs/HARDWARE.md) for wiring and assembly details.

## Get started

The current private prerelease is [v0.4.15](https://github.com/daxx2k/atari-portfolio-ai/releases/tag/v0.4.15). It pairs **ESP32 firmware 0.4.15** with **Portfolio client 0.4.14**; their version numbers are independent.

1. Follow [BUILD.md](docs/BUILD.md) to build and upload the ESP32 firmware, or use the prerelease binaries for the exact supported board.
2. Copy `portfolio/AI.COM` to the Portfolio memory card.
3. Copy `config/AISETUP.EXAMPLE.TXT` to the card as `AISETUP.TXT`. Fill in your Wi-Fi credentials, OpenAI API key, location and time zone.
4. Disconnect the card from USB before inserting it into the Portfolio. **Never connect card USB while the card is inserted in the Portfolio.**
5. Run `AI`, then use `/test` to check the serial link and `/status` to check the device.
6. Type a question to start chatting. Use `/help` for commands, `/config` to edit settings, and `/reload` to apply the card configuration.

The card configuration, its backup and ESP32 NVS store credentials in plaintext. Keep them private. Only the placeholder configuration is included in this repository.

## Everyday controls

| Command or key | Action |
| --- | --- |
| Type a question + Enter | Send a message and receive a reply |
| `/talk` | Start automatic microphone conversation |
| `/look` | Take a photograph and describe it |
| `/cam` / `/live` | Open detailed / coarse camera preview |
| `/radio` | Open radio controls |
| `/radio off` | Stop background radio |
| `/vol 40` | Set speaker volume from 0 to 100 |
| Shift + Up/Down | Adjust shared voice/radio volume by 5 |
| `/mute` / `/unmute` | Turn spoken replies off / on |
| `/weather` / `/news` | Request local reports |
| `/config` | Edit configuration and saved options |
| `/help` | Show available commands |

In the radio screen, **Left/Right** changes station and **Up/Down** changes volume. **Esc** returns to the prompt while music continues. During microphone conversation, radio stays paused.

See the [full command reference](docs/COMMANDS.md) for diagnostics, context commands and all controls.

## Project status

This is a working hardware prototype and a **private prerelease**, tested on the development assembly. It is not yet validated across other Portfolio units or ESP32 boards.

The camera preview is limited by the serial link and is **not real-time video**. AI replies, speech startup and radio reconnection also depend on Wi-Fi and service response times.

Read [Validation](docs/VALIDATION.md) for the checks and actual hardware feedback, and [Known limitations](docs/KNOWN_LIMITATIONS.md) for the remaining constraints.

## Source and development

- `firmware/PortfolioAI/`: ESP32 firmware.
- `portfolio/AI.ASM`: DOS client source.
- `config/AISETUP.EXAMPLE.TXT`: configuration template with placeholders.
- `docs/`: build instructions, wiring, commands and validation.
- `release.json`: pinned firmware build settings and tool versions.

Local checks cover the actual DOS binary in 16-bit emulation and the production audio DSP code. GitHub Actions checks the DOS build, client behavior, DSP and accidental credential patterns.

No project-wide public license has been selected for this private prerelease. Bundled third-party code retains its original notices; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
