# VirtualOrgan Configuration Reference

The organ engine is configured via a TOML file.
This document describes all available sections and fields.

---

## `[audio]` — Audio Engine Settings

```toml
[audio]
sample_rate = 48000        # expected sample rate (informational)
buffer_size = 1024         # expected buffer size (informational)
jack_client_name = "organ" # JACK client name
num_outputs = 2            # number of JACK output ports (default 2)
```

| Field              | Type   | Default | Description                    |
|--------------------|--------|---------|--------------------------------|
| `sample_rate`      | int    | 48000   | Expected sample rate.          |
| `buffer_size`      | int    | 128     | Expected buffer size.          |
| `jack_client_name` | string | "organ" | JACK client name.              |
| `num_outputs`      | int    | 2       | Number of JACK output ports.   |

- `sample_rate` and `buffer_size` are informational — JACK
  determines the actual values.
- Set `num_outputs` to 8 for 7.1 surround via HDMI.

---

## `[ranks.*]` — Sample Rank Definitions

Each rank is a set of WAV files for one pipe type
(e.g. Gedackt 8'). Ranks are pure sample data — they have
no stop controls or division assignments.

```toml
[ranks.gedackt8]
sample_dir = "samples/Burea_Funeral_Chapel/Gedackt8"
filename_pattern = "{note:03d}-{name}.wav"
num_perspectives = 1
```

| Field              | Type   | Default          | Description              |
|--------------------|--------|------------------|--------------------------|
| `sample_dir`       | string | (required)       | Directory of WAV files.  |
| `filename_pattern` | string | `{note:03d}.wav` | WAV filename pattern.    |
| `num_perspectives` | int    | 1                | Mic perspectives count.  |

- `filename_pattern` — see placeholders below.
- `num_perspectives` — used with routing to determine channels
  per perspective: `sample_channels / num_perspectives`.

### Filename Pattern Placeholders

| Placeholder  | Expands To              | Example (C4) |
|--------------|-------------------------|--------------|
| `{note:03d}` | Zero-padded 3-digit MIDI | `060`       |
| `{note:02d}` | Zero-padded 2-digit MIDI | `60`        |
| `{note}`     | Unpadded MIDI note       | `60`        |
| `{name}`     | Note name (# for sharps) | `C`         |
| `{octave}`   | Octave number            | `4`         |

---

## `[routing.*]` — Output Channel Routing

Routes audio sources to physical output speakers. Each
routing entry maps a source to one or more output channels.

The `source` field determines what is being routed. It is
an inline table with exactly one key identifying the type:

| Source Key    | Value  | Routes...                  |
|---------------|--------|----------------------------|
| `perspective` | int    | A mic perspective (1+).    |
| `division`    | string | All stops in a division.   |
| `rank`        | string | A specific rank by name.   |

### Source Precedence

More specific routes override more general ones:

1. `rank` — most specific, overrides division/perspective
2. `division` — overrides perspective
3. `perspective` — default for all ranks

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

# Division routing — all manual stops to front
[routing.manual_front]
source = { division = "manual" }
output_channels = [1, 2]

# Division routing — pedal to subwoofer
[routing.pedal_sub]
source = { division = "pedal" }
output_channels = [3, 4]

# Rank routing — override for a specific rank
[routing.subbas_sub]
source = { rank = "subbas16" }
output_channels = [3, 4]
```

### Fields

| Field             | Type         | Description              |
|-------------------|--------------|--------------------------|
| `source`          | inline table | What to route (see above). |
| `output_channels` | int array    | JACK port numbers (1+).  |

- If no `[routing]` section is present, all audio defaults
  to stereo output on channels 1-2.

### Perspectives

A perspective represents a microphone position used when
recording the organ. Most sample sets have one perspective
(one stereo pair). Professional sets may have multiple
perspectives (close, rear, ambient).

`perspective` maps to sample channels based on
`num_perspectives` in the rank config.

Multiple perspectives can come from:

- **Multi-channel WAV files** — a 4-channel WAV with
  `num_perspectives = 2` gives two stereo perspectives
  (channels 1-2 = close, channels 3-4 = rear).
- **Separate directories** — (future) each perspective in
  its own sample directory.

### Future Routing Features

Per-note-range routing is planned:

```toml
# Route low pedal notes to subwoofer (future)
[routing.pedal_bass]
source = { rank = "subbas16" }
note_range = [36, 48]
output_channels = [5, 6]
```

---

## `[divisions.*]` — Organ Divisions

A division groups stops under one keyboard/pedalboard with a
dedicated MIDI channel. Each division can have an expression
pedal for volume control.

```toml
[divisions.manual]
midi_channel = 2
expression_cc = 11
```

| Field           | Type | Default    | Description               |
|-----------------|------|------------|---------------------------|
| `midi_channel`  | int  | (required) | MIDI channel (1-16).      |
| `expression_cc` | int  | (none)     | CC for expression pedal.  |

- `midi_channel` — notes on this channel trigger this
  division's engaged stops.
- `expression_cc` — CC value 0-127 maps to gain 0.0-1.0.
- If no `[divisions]` section is present, all ranks are
  triggered by any MIDI channel (legacy mode).

### `[divisions.*.stops]` — Stop Definitions

Stops belong to a division. Each stop references one or more
ranks and has a MIDI CC for engage/disengage.

```toml
[divisions.manual.stops]
gedackt8 = { rank = "gedackt8", engage_cc = 39 }
salicional8 = { rank = "salicional8", engage_cc = 37 }
combo = { rank = ["gedackt8", "salicional8"], engage_cc = 48 }
```

| Field       | Type            | Description               |
|-------------|-----------------|---------------------------|
| `rank`      | string or array | Rank name(s) to control.  |
| `engage_cc` | int             | CC to toggle on/off.      |
| `engaged`   | bool (optional) | Initial state. Default: `false`. |

- `rank` — use a string for a single rank, or an array for
  a multi-rank stop (e.g. Mixture).
- `engage_cc` — CC value >= 64 = engaged, < 64 = disengaged.
- `engaged` — set to `true` to have this stop on at startup.
  Useful for testing or default registrations.
- Stops default to disengaged. They are toggled via MIDI CC
  events (from a controller, console mode, or keyboard mode).

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

| Field       | Type   | Description                  |
|-------------|--------|------------------------------|
| `from`      | string | Source division name.        |
| `to`        | string | Destination division name.   |
| `engage_cc` | int    | CC to toggle on/off.         |

- When engaged, notes on `from` also trigger `to`'s stops.
- Naming convention: `from_to_to` (e.g. `pedal_to_manual`).
- All couplers start disengaged.

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
source = { perspective = 1 }
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

[midi_devices.CH345]
channel = 1

[midi_devices."Digital Piano"]
channel = 2
```

---

## Backward Compatibility

All sections except `[audio]` and `[ranks]` are optional:

- **No `[routing]`** — defaults to stereo on channels 1-2.
- **No `[divisions]`** — all ranks triggered by any channel,
  all ranks always play (no stop controls).
- **No `[couplers]`** — no inter-division coupling.
- **No `[midi_devices]`** — MIDI channels pass through unchanged.

---

## `[midi_devices.*]` — MIDI Device Channel Mapping

Maps physical MIDI keyboards to MIDI channels based on their ALSA
sequencer client name. This allows multiple keyboards on the same
default channel (usually 1) to control different divisions without
changing any keyboard settings.

```toml
[midi_devices.CH345]
channel = 1

[midi_devices."Digital Piano"]
channel = 2
```

| Field     | Type | Description                              |
|-----------|------|------------------------------------------|
| `channel` | int  | MIDI channel to remap this device to (1-indexed). |

- The table key (e.g. `CH345`, `"Digital Piano"`) is matched as a
  **substring** against the ALSA sequencer client name. Use
  `aconnect -l` to see device names.
- Quote the key if the device name contains spaces.
- At startup the engine scans all ALSA clients and prints which
  devices were matched and remapped.
- If no `[midi_devices]` section is present, MIDI channels pass
  through from the keyboard unchanged.
