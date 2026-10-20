# 11 — Audio

## 1. Goals

- **Auditory parity:** per-entity channels, attenuation, pitch, volume, room DSP
  (`env_sound`/`room_type`), sentences (`sentences.txt`) and per-material footsteps identical
  to the original. Sound is competitive information in CS.
- **Robust real time:** mixing on a dedicated thread, with no allocations or locks in the
  callback.
- **No fixed voice ceiling:** a configurable pool with priority and documented voice
  stealing.
- **Extensible:** modern formats, streaming and new effects for plugins, all optional.
- **Quality and reliability fixed at the cause:** none of the original's audio limits and
  failures ([§2.1](#21-the-originals-audio-problems-and-their-causes)).

## 2. Architecture

```mermaid
flowchart LR
    G[Simulation / plugins] -->|sound commands<br/>SPSC queue| A[Audio thread · mixer]
    V[Voice decoding<br/>Opus jobs] -->|PCM buffers| A
    S[Streaming<br/>I/O + decoding] -->|blocks| A
    A --> D[SDL3 device<br/>48 kHz stereo or surround]
```

- The game never calls the mixer directly: it sends **commands** (`PlaySound`, `StopSound`,
  `UpdateListener`, `SetRoom`) through a lock-free queue.
- The audio thread mixes in small blocks (configurable size, low-latency default),
  resampling every source, whatever its rate, to the device rate.

### 2.1 The original's audio problems and their causes

| Problem in the original | Cause | Here |
|-------------------------|-------|------|
| Sounds limited to 8/16-bit PCM, mostly mono, at 11/22/44 kHz; other files rejected or played at the wrong speed | Fixed formats in the loader and the mixer | WAV of any common PCM layout (8, 16, 24 and 32-bit integer, 32-bit float, extensible) at any rate, plus FLAC, Vorbis and Opus ([§6](#6-formats)); every source resampled with a high-quality resampler; mixing in 32-bit float with headroom and a limiter, never clipping |
| MP3 music that stops by itself, after a map change, an alt-tab or a device change, with its own volume | Music played by a second player (Miles), outside the mixer, with its own device and state | **One mixer for everything:** effects, sentences, voice and music (`media/*.mp3` included) are voices of the same mixer on the same device; music is a streamed voice and `bgmvolume` a gain of the mixer |
| A stream that stalls or ends early | Decoding on demand, with no margin, and failures without a report | Streams decode ahead in `Low` jobs into a ring of blocks with a prefetch margin. An underrun plays silence for the missing block, is counted (`snd_stats`) and reported once, and the stream goes on; only its end or a command stops it. A decoding failure is a typed `SoundError` in the log, never a silent stop |
| Sound that depends on the frame, the loading screen or the window focus | Mixing driven by the game loop | The audio thread runs on its own clock: frame rate, map loads and losing the focus do not touch it (`s_play_unfocused` decides whether an unfocused window is heard) |
| Audio gone after unplugging headphones | The device opened once | Losing or changing the device reopens the system default and resumes every voice and stream where it was |

Every decoder refuses malformed input with a typed error and is fuzzed
([18](18-testing-and-quality.md#sanitizers-and-analysis)). Streaming is tested with slow,
simulated I/O (underruns are survived and counted), and the device with a fake one that
disappears and returns.

## 3. Original semantics to preserve

| Concept | Behavior |
|---------|----------|
| Per-entity channels | `Auto`, `Weapon`, `Voice`, `Item`, `Body`, `Stream`, `Static`. A new sound on the same channel and entity **replaces** the previous one (except `Auto`) |
| Attenuation | `None`, `Normal`, `Idle`, `Static`, with the original curves |
| Pitch and volume | Per event, with the same ranges |
| Positional sounds | Follow the emitting entity while it exists |
| PAS | Only plays what the server sent (PAS relevance, [08](08-networking.md#62-interest-management)) |
| Room DSP | `room_type` presets (reverb and echo) triggered by `env_sound` or by water |
| Sentences | Word composition from `sentences.txt` with pitch, volume and cut parameters |
| Footsteps | Sound per material (`materials.txt`) and per surface (ladder, water, grate) with `mp_footsteps` |
| Music | The `mp3`/`cd` command for `media/*.mp3` (menu and maps) |

## 4. Voices and priorities

- A voice pool with a configurable size (`s_max_voices`), with no format ceiling.
- When it runs out, a new voice steals the voice with the **lowest effective priority**
  (channel priority × perceived volume). Stealing produces a metric, never a silent failure.
- Gameplay-critical sounds (footsteps, gunshots, bomb, radio) have high priority through
  data.

## 5. Spatialization

| Mode | Description |
|------|-------------|
| `s_spatial legacy` (default) | Stereo panning and attenuation as in the original |
| `s_spatial hrtf` | Optional binaural HRTF for headphones |
| `s_spatial surround` | 5.1/7.1 output when the device supports it |

Geometry occlusion (`s_occlusion 1`) is **optional and off by default**, because it changes
the competitive information relative to the original.

## 6. Formats

| Format | Use | Decoder |
|--------|-----|---------|
| PCM WAV: 8, 16, 24 and 32-bit integer, 32-bit float, extensible | Original content and mods | In-house (`nx.assets:wav`) |
| MP3 | Original music (`media/`), streamed through the mixer like any other source | `minimp3` |
| OGG Vorbis | Mods | `stb_vorbis` |
| Opus | Voice and mods | `libopus` |
| FLAC | Mods | `dr_flac` |

Short sounds stay decoded in memory. Music and long sounds (above `s_stream_threshold`) are
streamed.

## 7. Voice

- Capture via SDL3, Opus encoding in a job and sending on the unreliable channel.
- Per-sender decoding in jobs and mixing as positional or non-positional voice, according
  to the server and plugin rules.
- `voice_enable`, `voice_scale`, `voice_loopback`, per-player *mute* in the scoreboard.

## 8. Cvars

| Cvar | Original | Role |
|------|----------|------|
| `volume` | ✅ | Master volume |
| `bgmvolume` | ✅ | Music |
| `suitvolume` | ✅ | HUD speech |
| `voice_scale` | ✅ | Volume of received voice |
| `room_type`, `room_off` | ✅ | DSP debugging and disabling |
| `s_max_voices` | new | Voice pool size |
| `s_spatial`, `s_occlusion` | new | Spatialization and occlusion |
| `s_device` | new | Output device |
| `s_play_unfocused` | new | Whether the game is heard while its window is not focused |

## 9. Typed errors

| Enum | Examples |
|------|----------|
| `AudioDeviceError` | `NoDevice`, `FormatUnsupported`, `DeviceLost` |
| `SoundError` | `DecodeFailed`, `UnsupportedEncoding`, `CuePointInvalid` |
| `SentenceError` | `UnknownSentence`, `MissingWord` |

Losing the device (e.g. headphones unplugged) makes the mixer try to reopen the system
default and emits a diagnostic event. The game keeps running without audio meanwhile.
