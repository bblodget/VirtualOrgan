# VirtualOrgan Configuration Reference

The organ engine is configured via a TOML file. This document describes all available sections and fields.

---

## `[audio]` — Audio Engine Settings

```toml
[audio]
sample_rate = 48000        # expected sample rate (informational)
buffer_size = 1024         # expected buffer size (informational)
jack_client_name = "organ" # JACK client name
num_outputs = 2            # number of JACK output ports (default 2)
```

| Field              | Type   | Default | Description                                                                |
|--------------------|--------|---------|----------------------------------------------------------------------------|
| `sample_rate`      | int    | 48000   | Expected sample rate. JACK determines the actual rate.                     |
| `buffer_size`      | int    | 128     | Expected buffer size. JACK determines the actual size.                     |
| `jack_client_name` | string | "organ" | Name used when connecting to the JACK server.                              |
| `num_outputs`      | int    | 2       | Number of JACK output ports to register. Set to 8 for 7.1 surround via HDMI. |

---

## `[ranks.*]` — Sample Rank Definitions

Each rank is a set of WAV files for one pipe type (e.g. Gedackt 8'). Ranks are pure sample data — they have no stop controls or division assignments.

```toml
[ranks.gedackt8]
sample_dir = "samples/Burea_Funeral_Chapel/Gedackt8"
filename_pattern = "{note:03d}-{name}.wav"
num_perspectives = 1
```

| Field              | Type   | Default          | Description                                                                                                                                    |
|--------------------|--------|------------------|------------------------------------------------------------------------------------------------------------------------------------------------|
| `sample_dir`       | string | (required)       | Directory containing WAV files for this rank.                                                                                                  |
| `filename_pattern` | string | `{note:03d}.wav` | Pattern for WAV filenames. See placeholders below.                                                                                             |
| `num_perspectives` | int    | 1                | Number of mic perspectives in the sample data. Used with routing to determine channels per perspective: `sample_channels / num_perspectives`. |

### Filename Pattern Placeholders

| Placeholder  | Expands To                    | Example (middle C, MIDI 60) |
|--------------|-------------------------------|-----------------------------|
| `{note:03d}` | Zero-padded 3-digit MIDI note | `060`                       |
| `{note:02d}` | Zero-padded 2-digit MIDI note | `60`                        |
| `{note}`     | Unpadded MIDI note number     | `60`                        |
| `{name}`     | Note name with # for sharps   | `C`                         |
| `{octave}`   | Octave number                 | `4`                         |

---

## `[routing.*]` — Output Channel Routing

Routes audio perspectives to physical output speakers. Each routing entry maps a perspective (mic position) to one or more output channels.

```toml
# Single perspective to stereo speakers
[routing.default]
perspective = 1
output_channels = [1, 2]

# Second perspective to rear speakers (multi-perspective sample set)
[routing.rear]
perspective = 2
output_channels = [3, 4]
```

| Field              | Type      | Default    | Description                                                                               |
|--------------------|-----------|------------|-------------------------------------------------------------------------------------------|
| `perspective`      | int       | 1          | Which perspective to route (1-indexed). Maps to sample channels based on `num_perspectives`. |
| `output_channels`  | int array | (required) | Output JACK port numbers (1-indexed).                                                     |

If no `[routing]` section is present, all audio defaults to stereo output on channels 1-2.

### Perspectives

A perspective represents a microphone position used when recording the organ. Most sample sets have one perspective (one stereo pair). Some professional sets have multiple perspectives (close, rear, ambient), each providing a different stereo recording of the same pipes.

Multiple perspectives can come from:
- **Multi-channel WAV files** — a 4-channel WAV with `num_perspectives = 2` gives two stereo perspectives (channels 1-2 = close, channels 3-4 = rear)
- **Separate directories** — (future) each perspective in its own sample directory

### Future Routing Features

Per-rank and per-note-range routing overrides are planned:

```toml
# Per-rank override (future)
[routing.subbas_sub]
rank = "subbas16"
output_channels = [3, 4]

# Per-note-range override (future)
[routing.pedal_bass]
rank = "subbas16"
note_range = [36, 48]
output_channels = [5, 6]
```

---

## `[divisions.*]` — Organ Divisions

A division groups stops under one keyboard/pedalboard with a dedicated MIDI channel. Each division can have an expression pedal for volume control.

```toml
[divisions.manual]
midi_channel = 2
expression_cc = 11
```

| Field           | Type | Default    | Description                                                                                                        |
|-----------------|------|------------|--------------------------------------------------------------------------------------------------------------------|
| `midi_channel`  | int  | (required) | MIDI channel for this division (1-16). Notes on this channel trigger this division's engaged stops.                |
| `expression_cc` | int  | (none)     | MIDI CC number for the expression/swell pedal. Controls per-division volume (CC value 0-127 maps to gain 0.0-1.0). |

If no `[divisions]` section is present, all ranks are triggered by any MIDI channel (legacy mode).

### `[divisions.*.stops]` — Stop Definitions

Stops belong to a division. Each stop references one or more ranks and has a MIDI CC for engage/disengage.

```toml
[divisions.manual.stops]
gedackt8 = { rank = "gedackt8", engage_cc = 39 }
salicional8 = { rank = "salicional8", engage_cc = 37 }
combo = { rank = ["gedackt8", "salicional8"], engage_cc = 48 }
```

| Field       | Type                  | Description                                                                                                        |
|-------------|-----------------------|--------------------------------------------------------------------------------------------------------------------|
| `rank`      | string or string array | Rank name(s) this stop controls. Use a string for a single rank, or an array for a multi-rank stop (e.g. Mixture). |
| `engage_cc` | int                   | MIDI CC number to toggle this stop. CC value >= 64 = engaged, < 64 = disengaged.                                  |

All stops start disengaged. They are toggled via MIDI CC events (from a MIDI controller, the web interface, or keyboard mode).

---

## `[couplers.*]` — Division Couplers

A coupler links two divisions so that playing one also triggers another. Couplers are directional.

```toml
[couplers.pedal_to_manual]
from = "pedal"
to = "manual"
engage_cc = 49
```

| Field       | Type   | Description                                                                     |
|-------------|--------|---------------------------------------------------------------------------------|
| `from`      | string | Source division name. When keys are played on this division...                  |
| `to`        | string | Destination division name. ...this division's engaged stops are also triggered. |
| `engage_cc` | int    | MIDI CC number to toggle this coupler on/off.                                   |

Naming convention: `from_to_to` (e.g. `pedal_to_manual`).

All couplers start disengaged.

---

## Complete Example

```toml
[audio]
sample_rate = 48000
buffer_size = 1024
jack_client_name = "organ"
num_outputs = 2

[ranks.gedackt8]
sample_dir = "samples/Burea_Funeral_Chapel/Gedackt8"
filename_pattern = "{note:03d}-{name}.wav"

[ranks.subbas16]
sample_dir = "samples/Burea_Funeral_Chapel/Subbas16"
filename_pattern = "{note:03d}-{name}.wav"

[routing.default]
perspective = 1
output_channels = [1, 2]

[divisions.manual]
midi_channel = 2
expression_cc = 11

[divisions.manual.stops]
gedackt8 = { rank = "gedackt8", engage_cc = 39 }

[divisions.pedal]
midi_channel = 1

[divisions.pedal.stops]
subbas16 = { rank = "subbas16", engage_cc = 36 }

[couplers.pedal_to_manual]
from = "pedal"
to = "manual"
engage_cc = 49
```

---

## Backward Compatibility

All sections except `[audio]` and `[ranks]` are optional:

- **No `[routing]`** — defaults to stereo output on channels 1-2
- **No `[divisions]`** — all ranks triggered by any MIDI channel, all ranks always play (no stop controls)
- **No `[couplers]`** — no inter-division coupling
