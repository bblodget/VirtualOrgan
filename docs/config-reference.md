# VirtualOrgan Configuration Reference

The organ engine is configured via a TOML file.
This document describes all available sections and fields.

---

## `[audio]` — Audio Engine Settings

```toml
[audio]
sample_rate = 48000
buffer_size = 1024
jack_client_name = "organ"
num_outputs = 2
release_fade_ms = 100
```

| Field              | Type   | Default | Description                    |
|--------------------|--------|---------|--------------------------------|
| `sample_rate`      | int    | 48000   | Expected sample rate.          |
| `buffer_size`      | int    | 128     | Expected buffer size.          |
| `jack_client_name` | string | "organ" | JACK client name.              |
| `num_outputs`      | int    | 2       | Number of JACK output ports.   |
| `release_fade_ms`  | int    | 250     | Release fade-out duration (ms).|

- `sample_rate` and `buffer_size` are informational — JACK
  determines the actual values.
- Set `num_outputs` to 8 for 7.1 surround via HDMI.
- `release_fade_ms` controls how quickly notes fade after
  key release. Lower values (50-100) feel more responsive.

---

## `[ranks.*]` — Sample Rank Definitions

Each rank is a set of WAV files for one pipe type
(e.g. Gedackt 8'). Ranks are pure sample data — they have
no stop controls or division assignments.

```toml
# Single directory (one perspective)
[ranks.gedackt8]
sample_dir = "samples/Burea_Funeral_Chapel/Gedackt8"
filename_pattern = "{note:03d}-{name}.wav"

# Multiple directories (two perspectives)
[ranks.montre8]
sample_dir = [
    "samples/.../goDiff/Montre8",
    "samples/.../goRear/Montre8"
]
filename_pattern = "{note:03d}-{name}.wav"
```

| Field              | Type             | Default          | Description             |
|--------------------|------------------|------------------|-------------------------|
| `sample_dir`       | string or array  | (required)       | Directory of WAV files. |
| `filename_pattern` | string           | `{note:03d}.wav` | WAV filename pattern.   |

- `sample_dir` — a string for one directory, or an array
  of strings for multiple directories (one per perspective).
  Each directory's stereo channels are concatenated:
  2 dirs × stereo = 4 channels (L1, R1, L2, R2).
- `num_perspectives` is inferred from the number of
  directories (1 for a string, N for an array of N).

### Filename Pattern Placeholders

| Placeholder  | Expands To              | Example (C4) |
|--------------|-------------------------|--------------|
| `{note:03d}` | Zero-padded 3-digit MIDI | `060`       |
| `{note:02d}` | Zero-padded 2-digit MIDI | `60`        |
| `{note}`     | Unpadded MIDI note       | `60`        |
| `{name}`     | Note name (# for sharps) | `C`         |
| `{octave}`   | Octave number            | `4`         |

If a file is not found, the sampler retries with a
lowercase variant (e.g. `024-c.wav` for low pedal notes).

---

## `[routing.*]` — Output Channel Routing

Routes audio sources to physical output speakers. Each
routing entry maps a source to one or more output channels.

The `source` field is an inline table with keys:

| Source Key    | Value  | Description                    |
|---------------|--------|--------------------------------|
| `perspective` | int    | Mic perspective (1+).          |
| `division`    | string | All stops in a division.       |
| `rank`        | string | A specific rank by name.       |
| `note_range`  | array  | [low, high] MIDI note range.   |
| `channel`     | int    | Channel within perspective (1+). |

### Source Precedence

Rank/division routes override perspective routes.
Multiple perspective routes create multiple voices
(one per perspective for multi-perspective support).

1. `rank` or `division` routes — most specific
2. `perspective` routes — default for all ranks

### Source Channel Selection

By default, all channels within a perspective are mapped
to the output channels. Use `channel` to select a single
channel and duplicate it to all outputs:

```toml
# Fix mono-in-stereo: use only right channel (2)
[routing.recit_fix]
source = { division = "swell", perspective = 1, channel = 2 }
output_channels = [1, 2]
```

### Examples

```toml
# Perspective routing — default for all ranks
[routing.close]
source = { perspective = 1 }
output_channels = [1, 2]

# Second perspective to rear speakers
[routing.rear]
source = { perspective = 2 }
output_channels = [3, 4]

# Division routing — pedal to subwoofer
[routing.pedal_sub]
source = { division = "pedal" }
output_channels = [3, 4]

# Rank routing — specific rank + perspective
[routing.subbas_close]
source = { rank = "subbas16", perspective = 1 }
output_channels = [6]

# Note-range routing — bass split
[routing.subbas_low]
source = { rank = "subbas16", note_range = [36, 48] }
output_channels = [3, 4]

[routing.subbas_high]
source = { rank = "subbas16", note_range = [49, 67] }
output_channels = [1, 2]
```

### Fields

| Field             | Type         | Description                |
|-------------------|--------------|----------------------------|
| `source`          | inline table | What to route (see above). |
| `output_channels` | int array    | JACK port numbers (1+).    |

- If no `[routing]` section is present, all audio defaults
  to stereo output on channels 1-2.

### Perspectives

A perspective represents a microphone position used when
recording the organ. Most sample sets have one perspective
(one stereo pair). Professional sets may have multiple
perspectives (close, rear, ambient).

Multiple perspectives are loaded from separate directories
via the `sample_dir` array on ranks. The routing then maps
each perspective to different output speakers.

### Note Range

`note_range` can be added to a source to route different
MIDI note ranges to different outputs (bass split). The
range is inclusive. Use a single element for one note.

```toml
[routing.subbas_low]
source = { rank = "subbas16", note_range = [36, 48] }
output_channels = [5, 6]

[routing.middle_c]
source = { rank = "gedackt8", note_range = [60] }
output_channels = [3, 4]
```

---

## `[divisions.*]` — Organ Divisions

A division groups stops under one keyboard/pedalboard with
a dedicated MIDI channel. Each division can have an
expression pedal for volume control.

```toml
[divisions.manual]
midi_channel = 2
expression_cc = 11

[divisions.pedal]
midi_channel = 1
note_range = [24, 35]
```

| Field           | Type      | Default    | Description              |
|-----------------|-----------|------------|--------------------------|
| `midi_channel`  | int       | (required) | MIDI channel (1-16).     |
| `expression_cc` | int       | (none)     | CC for expression pedal. |
| `note_range`    | int array | (none)     | MIDI note range.         |

- `midi_channel` — notes on this channel trigger this
  division's engaged stops.
- `expression_cc` — CC value 0-127 maps to gain 0.0-1.0.
- `note_range` — restricts this division to a range of
  MIDI notes. Use `[low, high]` (inclusive) or `[note]`.
  Enables **keyboard splits** — multiple divisions on the
  same MIDI channel responding to different note ranges.
- If no `[divisions]` section is present, all ranks are
  triggered by any MIDI channel (legacy mode).

### Keyboard Splits

Multiple divisions can share the same `midi_channel` with
non-overlapping `note_range` values:

```toml
[divisions.great]
midi_channel = 1
note_range = [36, 96]

[divisions.pedal]
midi_channel = 1
note_range = [24, 35]
```

### `[divisions.*.stops]` — Stop Definitions

Stops belong to a division. Each stop references one or
more ranks and has a MIDI CC for engage/disengage.

```toml
[divisions.manual.stops]
gedackt8 = { rank = "gedackt8", engage_cc = 39 }
salicional8 = { rank = "salicional8", engage_cc = 37 }
combo = { rank = ["gedackt8", "salicional8"], engage_cc = 48 }
```

| Field       | Type            | Description              |
|-------------|-----------------|--------------------------|
| `rank`      | string or array | Rank name(s) to control. |
| `engage_cc` | int             | CC to toggle on/off.     |
| `engaged`   | bool (optional) | Initial state (default: false). |

- `rank` — string for a single rank, array for a
  multi-rank stop (e.g. Mixture).
- `engage_cc` — CC value >= 64 = engaged, < 64 = off.
- `engaged` — set to `true` to start this stop on.

---

## `[couplers.*]` — Division Couplers

A coupler links two divisions so that playing one also
triggers another. Couplers are directional.

```toml
[couplers.pedal_to_manual]
from = "pedal"
to = "manual"
engage_cc = 49
```

| Field       | Type   | Description                |
|-------------|--------|----------------------------|
| `from`      | string | Source division name.       |
| `to`        | string | Destination division name. |
| `engage_cc` | int    | CC to toggle on/off.       |

- When engaged, notes on `from` also trigger `to`'s stops.
- Naming convention: `from_to_to` (e.g. `pedal_to_manual`).
- All couplers start disengaged.

---

## `[midi_devices.*]` — MIDI Device Channel Mapping

Maps physical MIDI keyboards to MIDI channels based on
their ALSA sequencer client name. Allows multiple keyboards
to control different divisions without changing keyboard
settings.

```toml
[midi_devices.CH345]
midi_channel = 1

[midi_devices."Digital Piano"]
midi_channel = 2
```

| Field          | Type | Description                     |
|----------------|------|---------------------------------|
| `midi_channel` | int  | MIDI channel to remap to (1+).  |

- The table key (e.g. `CH345`) is matched as a
  **substring** against the ALSA client name.
- Quote the key if the name contains spaces.
- Use `aconnect -l` to see device names.
- If no `[midi_devices]` section is present, MIDI
  channels pass through unchanged.

---

## Complete Example

```toml
[audio]
sample_rate = 48000
buffer_size = 1024
jack_client_name = "organ"
num_outputs = 2
release_fade_ms = 100

[ranks.gedackt8]
sample_dir = "samples/Burea_Funeral_Chapel/Gedackt8"
filename_pattern = "{note:03d}-{name}.wav"

[ranks.subbas16]
sample_dir = "samples/Burea_Funeral_Chapel/Subbas16"
filename_pattern = "{note:03d}-{name}.wav"

[routing.default]
source = { perspective = 1 }
output_channels = [1, 2]

[midi_devices.CH345]
midi_channel = 1

[divisions.manual]
midi_channel = 1
expression_cc = 11

[divisions.manual.stops]
gedackt8 = { rank = "gedackt8", engage_cc = 39, engaged = true }

[divisions.pedal]
midi_channel = 2

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

- **No `[routing]`** — defaults to stereo on channels 1-2.
- **No `[divisions]`** — all ranks triggered by any channel,
  all ranks always play (no stop controls).
- **No `[couplers]`** — no inter-division coupling.
- **No `[midi_devices]`** — MIDI channels pass through.
