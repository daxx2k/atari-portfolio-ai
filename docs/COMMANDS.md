# Terminal commands

| Command or key | Behavior |
| --- | --- |
| Enter | Send typed input |
| `/help` | Show the firmware's command list |
| `/test` or `/ping` | Serial link diagnostic |
| `/status` | Device diagnostics |
| `/config` | Edit the card configuration; O opens saved audio/radio options |
| `/reload` | Apply AISETUP.TXT |
| `/mute`, `/unmute` | Disable/enable speech for replies |
| `/robot on`, `/robot off` | Enable/disable voice DSP effect |
| `/vol N` | Set shared speaker level, 0-100 |
| Shift+Up/Down | Shared voice/radio level +/-5 |
| `/sound` | Play classic science-fiction signal tones |
| `/voice` | Speak the voice check phrase |
| `/talk` | Automatic microphone conversation; Esc exits |
| `/cam` | Detailed camera preview; Esc exits |
| `/live` | Coarse camera preview; Esc exits |
| `/look` | Capture an image and describe it |
| “What's that?” / “What am I looking at?” | Ask about a current camera photograph |
| `/radio` | Radio controls |
| Radio Left/Right | Previous/next station |
| Radio Up/Down | Shared speaker volume +/-5 |
| Radio Esc | Leave radio screen, keep music playing |
| `/radio off` | Stop background music |
| `/time`, `/location` | Current configured context |
| `/weather`, `/news` | Live reports |
| `/sources` | Sources from the last report |

Speech is enabled by default. Explicit mute and volume choices persist through card settings and ESP32 storage. Radio pauses for spoken answers and resumes afterwards. `/talk` keeps radio paused until conversation ends. An explicit `/radio off` cancels automatic resumption.

Stations: Nightride FM, Darksynth FM, Chillsynth FM, Datawave FM and Nightwave Plaza. Stations/stream availability may change independently of this release.

Volume keys may queue behind an in-progress AI response or camera packet so the packet can finish safely. Radio control acknowledgements are compact; a station's actual music still needs reconnection and buffering.
