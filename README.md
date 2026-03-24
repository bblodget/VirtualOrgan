# VirtualOrgan

A custom, config-driven virtual pipe organ engine written in C for Linux. Designed as a dedicated headless appliance for playing sampled pipe organ sounds through a multi-channel speaker system, controlled via an iPad web interface over WiFi.

> **Status: Active Development**
> The core engine works — MIDI input, stereo sample playback with sustain looping and release tails, divisions with stop controls and expression pedals, coupling, multi-channel output routing, and computer keyboard mode. Not yet implemented: web interface, presets. See [`docs/todo.md`](docs/todo.md) for current progress.

## Building

### Dependencies

```bash
sudo apt install jackd2 libjack-jackd2-dev pkg-config libsndfile1-dev libasound2-dev libsdl2-dev
```

Say **yes** when asked to enable real-time process priority during the jackd2 install. Your user must be in the `audio` group:

```bash
groups  # verify 'audio' is listed
```

### Compile

```bash
make        # build the organ engine
make help   # show all targets
```

## Sample Files

The engine plays WAV files organized into directories by rank. Samples are not included in this repo — they go in the `samples/` directory.

### Generate test samples

```bash
make gen-samples   # creates sine wave samples in samples/test/
```

### Use real organ samples

Download a GrandOrgue-format sample set (e.g., from [Lars Palo](https://familjenpalo.se/vpo/download/)) and extract it into `samples/`. Then create or edit a TOML config pointing to the sample directories with the appropriate `filename_pattern`.

## Running

### Start JACK

The organ engine requires a running JACK audio server. If your system uses **PipeWire** (most modern Linux desktops), use `pw-jack` — no separate JACK server needed:

```bash
# PipeWire (recommended on desktop Linux)
pw-jack ./organ-engine test/burea_config.toml --fake-midi

# With standalone JACK and real audio hardware
jackd -d alsa -d hw:0 -r 48000 -p 128 &
./organ-engine test/burea_config.toml --fake-midi

# Without audio hardware (silent, for testing)
jackd -d dummy -r 48000 -p 128 &
./organ-engine test/burea_config.toml --fake-midi
```

### Start the engine

```bash
# Auto-play a C major scale (no keyboard needed)
pw-jack ./organ-engine test/test_config.toml --fake-midi

# Play with computer keyboard (SDL2 window for input)
pw-jack ./organ-engine test/burea_config.toml --keyboard

# Real MIDI keyboard with console controls for stops/gain
pw-jack ./organ-engine test/burea_config.toml --console

# Real MIDI keyboard input only (requires connecting a MIDI device, see below)
pw-jack ./organ-engine test/burea_config.toml
```

Press `Ctrl+C` to stop (or `Esc`/`Q` in keyboard/console mode).

### Computer Keyboard Mode

With `--keyboard`, an SDL2 window opens for input. The QWERTY layout maps to a piano:

```
 2 3   5 6 7   9 0        (black keys)
Q W E R T Y U I O P       (white keys: C D E F G A B C D E)
```

- `[` / `]` — octave down/up
- `Z X C V B N M` — toggle stops 1-7
- `Space` — all stops off
- `H` — print help
- `Esc` — quit

### Console Mode

With `--console`, the terminal provides stop and volume controls while you play a real MIDI keyboard. Works over SSH — no GUI needed.

- `Z X C V B N M` — toggle stops 1-7 (in active division)
- `Tab` — cycle active division
- `[` / `]` — division expression volume down/up
- `-` / `=` — master gain down/up
- `` ` `` — toggle coupler(s)
- `Space` — all stops off (active division)
- `H` — print help
- `Q` or `Esc` — quit

### Connecting a MIDI Keyboard

When running without `--fake-midi`, the engine opens an ALSA sequencer port and listens for MIDI input. You need to connect your USB MIDI keyboard to the engine manually using `aconnect`:

```bash
# List available MIDI ports
aconnect -l

# Example output:
#   client 20: 'USB MIDI Keyboard' [type=kernel]
#       0 'USB MIDI Keyboard MIDI 1'
#   client 128: 'organ-engine' [type=user]
#       0 'midi_in'

# Connect the keyboard to the engine
aconnect 20:0 128:0
```

The first number is the source (your keyboard), the second is the destination (the organ engine). The client numbers may vary — check `aconnect -l` each time.

**Tip:** If the engine is not yet running when you run `aconnect -l`, the organ-engine port won't appear. Start the engine first, then connect.

In a full organ setup with multiple keyboards (manuals) and a pedalboard, each physical MIDI device would be connected separately and assigned to different MIDI channels. The engine routes notes to divisions based on the `midi_channel` setting in each division's config.

## Configuration

The engine is configured via a TOML file. See [`docs/config-reference.md`](docs/config-reference.md) for the complete reference. Brief example:

```toml
[audio]
sample_rate = 48000
buffer_size = 1024
jack_client_name = "organ"
num_outputs = 2

[ranks.gedackt8]
sample_dir = "samples/Burea_Funeral_Chapel/Gedackt8"
filename_pattern = "{note:03d}-{name}.wav"

[routing.default]
source = { perspective = 1 }
output_channels = [1, 2]

[divisions.manual]
midi_channel = 2
expression_cc = 11

[divisions.manual.stops]
gedackt8 = { rank = "gedackt8", engage_cc = 39 }
```

### Filename pattern placeholders

| Placeholder   | Expands To                        | Example (middle C) |
|---------------|-----------------------------------|--------------------|
| `{note:03d}`  | Zero-padded 3-digit MIDI note     | `060`              |
| `{note:02d}`  | Zero-padded 2-digit MIDI note     | `60`               |
| `{note}`      | Unpadded MIDI note number         | `60`               |
| `{name}`      | Note name with # for sharps       | `C`                |
| `{octave}`    | Octave number                     | `4`                |

## Documentation

- [`docs/config-reference.md`](docs/config-reference.md) — Complete TOML configuration reference
- [`docs/organ-engine-project.md`](docs/organ-engine-project.md) — Project summary and architecture
- [`docs/virtual-organ-handbook.md`](docs/virtual-organ-handbook.md) — Technical handbook covering concepts, theory, and technologies
- [`docs/todo.md`](docs/todo.md) — Project roadmap and progress tracking

## License

This project is licensed under the [GNU General Public License v2.0](LICENSE).
