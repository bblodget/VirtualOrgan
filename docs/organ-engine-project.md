# Virtual Pipe Organ Engine — Project Summary

## Overview

A custom, config-driven virtual pipe organ engine written in C for Linux. Designed as a dedicated appliance for playing sampled pipe organ sounds through a multi-channel speaker system. Controlled via an iPad/iPhone web interface over WiFi. No monitor, keyboard, or mouse required — the Linux box boots and runs silently as a headless appliance.

## Background & Motivation

- Dad has a nice amplifier and 18-speaker setup
- Long-time Apple user, frustrated with forced updates and dropped support
- Wants to move to Linux with a dedicated, stable organ machine
- Commercial option is Hauptwerk (Windows/macOS only, no Linux)
- Open-source alternative GrandOrgue exists for Linux but we're building custom
- Goal: minimal, config-driven, appliance-like — boots and just works

## Hardware Plan

### Dedicated Mini PC

**Minix Z350-0dB (512GB SSD)**
- Fanless, completely silent
- Dual-boot configuration:
    - **Windows 11 (~300GB)** — Runs Hauptwerk
    - **Debian Trixie (~200GB)** — Runs custom organ engine
- 16GB RAM (upgradeable)
- 512GB PCIe 3.0 NVMe SSD
- Plenty of power for both Hauptwerk and our lean C engine

**Peripherals:**
- Multi-channel USB audio interface (Behringer UMC1820 or similar, 18+ outputs)
- USB MIDI interface
- MIDI organ console with keyboards and pedalboard

### Why This Machine

- Fanless = zero noise in music room
- x86 Linux = full driver compatibility for USB audio interfaces
- 16GB RAM = enough for large sample sets, upgradeable to 32GB
- Small, tuckable behind console or on a shelf
- Single machine handles both commercial and custom software via dual-boot

### Login

- Username: brandonb
- Machine Name: debian
- Network Name: debian.local (via Avahi/mDNS)
- SSH access from Pop_OS laptop for remote maintenance
- ssh debian.local ( ssh key-based auth, no password)

## Software Architecture

### Data Flow

```
MIDI device (USB)
    → MIDI thread (ALSA sequencer)
    → lock-free ring buffer
    → JACK process callback
        → read MIDI events from ring buffer
        → update voice pool (note on/off/CC)
        → for each active voice: read samples, apply gain
        → sum into per-channel output buffers
        → JACK writes to hardware outputs
        → hardware outputs → amplifier → 18 speakers

iPad/iPhone (Safari over WiFi)
    ↔ WebSocket
    ↔ embedded HTTP server in organ engine
    → stop on/off, preset recall, gain changes, coupling
    ← state updates, voice count, level meters
```

### Core Modules

- **`main.c`** — Loads config, loads samples, opens JACK, opens MIDI, starts web server, runs until killed
- **`config.c`** — Parses TOML config at startup; maps ranks to sample dirs, stops to MIDI CCs, divisions to output channels
- **`midi.c`** — Listens on ALSA sequencer for note on/off and CC events, pushes into lock-free ring buffer
- **`sampler.c`** — Loads WAV files into memory at startup, organized by rank and note number; stores loop points, release offsets, base pitch metadata
- **`voice.c`** — Voice pool manager; note-on allocates voice, note-off transitions to release phase, finished voices return to pool
- **`mixer.c`** — Sums active voices into per-channel output buffers based on routing config; applies per-division gain from expression pedals
- **`jack_engine.c`** — JACK client setup and process callback; callback polls ring buffer, updates voices, calls mixer
- **`web.c`** — Embedded HTTP server (using microhttpd or mongoose), serves single-page web app, handles WebSocket connections, translates JSON commands into engine state changes

### Real-Time Boundary (Critical)

Everything inside the JACK process callback must be real-time safe:
- No `malloc` / `free`
- No `printf` or file I/O
- No mutex locks
- No system calls that can block
- Only: read ring buffer, update voice state, mix samples into buffers

All heavy work (sample loading, config parsing, MIDI thread reads, web server) happens outside the callback on separate threads.

### Sample Playback

Each organ pipe sample has three phases:
1. **Attack** — initial transient when pipe speaks
2. **Sustain loop** — seamless loop while key is held
3. **Release** — decay after key is released

Crossfades at loop boundaries are important for realism. Each rank (set of pipes for one stop) has one sample per note, stored as WAV/FLAC files in directories.

### Organ-Specific Features

- **Stops** — engage/disengage ranks via MIDI CC or web interface; each stop maps to a rank
- **Divisions** — groups of stops (Great, Swell, Pedal, Choir) routed to different output channels
- **Coupling** — playing one division's keys triggers another division's pipes (duplicate note events)
- **Expression pedals** — MIDI CC controls per-division volume (swell shutters)
- **Wind simulation** — subtle random pitch/amplitude LFO per voice for realism
- **Presets/Registrations** — saved combinations of stops, coupling, and gain settings

### Multi-Channel Audio Routing

Different organ divisions route to different speaker groups to simulate spatial layout of a real pipe organ:
- Pedal division → speakers below/behind
- Great division → speakers in front
- Swell division → speakers to the sides
- Ambient/reverb → distant speakers for room simulation

Requires a multi-channel USB audio interface with enough outputs for 18 speakers.

### Example Config (TOML)

```toml
[organ]
name = "Home Cathedral"

[audio]
sample_rate = 48000
buffer_size = 128
jack_client_name = "organ"

[web]
port = 80
hostname = "organ"

[ranks.great_principal_8]
sample_dir = "/organs/cathedral/great/principal8"
midi_channel = 1
output_channels = [1, 2]

[ranks.great_flute_4]
sample_dir = "/organs/cathedral/great/flute4"
midi_channel = 1
output_channels = [1, 2]

[ranks.swell_strings_8]
sample_dir = "/organs/cathedral/swell/strings8"
midi_channel = 2
output_channels = [5, 6]

[ranks.pedal_subbass_16]
sample_dir = "/organs/cathedral/pedal/subbass16"
midi_channel = 3
output_channels = [15, 16]

[stops]
great_principal = { rank = "great_principal_8", engage_cc = 36 }
great_flute = { rank = "great_flute_4", engage_cc = 37 }
swell_strings = { rank = "swell_strings_8", engage_cc = 40 }
pedal_subbass = { rank = "pedal_subbass_16", engage_cc = 48 }

[divisions]
great = { stops = ["great_principal", "great_flute"], expression_cc = 11 }
swell = { stops = ["swell_strings"], expression_cc = 12 }
pedal = { stops = ["pedal_subbass"] }

[presets.sunday_morning]
stops = ["great_principal", "great_flute", "swell_strings", "pedal_subbass"]
couplers = [{ from = "swell", to = "great" }]

[presets.bach_fugue]
stops = ["great_principal", "pedal_subbass"]
couplers = []

[presets.full_organ]
stops = ["great_principal", "great_flute", "swell_strings", "pedal_subbass"]
couplers = [{ from = "swell", to = "great" }, { from = "great", to = "pedal" }]
```

## iPad/iPhone Web Interface

### How It Works

The organ engine includes a small embedded HTTP server. Dad opens Safari on his iPad, navigates to `http://organ.local`, and bookmarks it to the home screen. It launches full-screen with no browser chrome — looks and feels like a native app.

Communication is via WebSocket for real-time two-way updates. The iPad sends commands (stop on/off, preset recall), the engine pushes state back (active stops, voice count, levels).

### Network Discovery

Avahi (mDNS) runs on the Linux box and advertises the organ engine as `organ.local` on the local network. No IP addresses to remember. The iPad finds it automatically.

### Interface Layout

The web interface is a single self-contained HTML file with embedded CSS and JavaScript. No build tools, no frameworks, no npm.

**Main screen — Stop Console:**
- Organized by division (Great, Swell, Pedal, Choir)
- Large toggle buttons for each stop — illuminated when engaged
- Touch-friendly, large targets for use during performance
- Division labels as headers

**Preset Bar:**
- Row of preset buttons along the bottom or top
- Tap to recall a full registration instantly
- Long-press or edit mode to save current state as a preset
- Named presets: "Sunday Morning", "Bach Fugue", "Full Organ", etc.

**Expression/Levels Panel (secondary screen or slide-out):**
- Per-division volume sliders
- Per-channel output level meters
- Master volume

**Settings Panel (rarely used):**
- Sample set selection
- Output routing configuration
- MIDI channel assignments
- Engine status (voice count, CPU usage, latency)

### Interface Design Principles

- Big, obvious controls — usable by someone who is not tech-savvy
- Minimal screens — main stop console covers 95% of use
- Responsive — works on iPad, iPhone, or any browser
- Dark theme option for low-light performance settings
- No login, no authentication (trusted home network)
- Instant response — WebSocket, no page reloads

## Linux System Configuration

### Distribution

**Debian Trixie (Testing)**
- Provides modern packages and kernel for hardware support
- Minimal install: JACK, ALSA, Avahi, organ engine, SSH server
- No desktop environment needed (headless appliance)
- SSH access for remote maintenance

### systemd Service

```ini
[Unit]
Description=Pipe Organ Engine
After=sound.target network.target avahi-daemon.service

[Service]
ExecStart=/usr/local/bin/organ-engine /etc/organ/config.toml
User=organ
Group=audio
CPUSchedulingPolicy=fifo
CPUSchedulingPriority=80
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
```

### System Packages

```bash
apt install jackd2 libjack-jackd2-dev avahi-daemon libavahi-client-dev \
    libsndfile1-dev libmicrohttpd-dev alsa-utils openssh-server
```

### Real-Time Audio Configuration

- Add `organ` user to `audio` group
- Configure `/etc/security/limits.d/audio.conf` for real-time priority
- Consider `PREEMPT_RT` kernel patches or `linux-lowlatency` package
- JACK configured for low latency (128 or 256 frames at 48kHz)

## The User Experience

1. Dad walks up to the organ console
2. The Linux box is already running (always on, low power, silent)
3. He taps the "Organ" icon on his iPad home screen
4. The stop console appears instantly
5. He taps "Sunday Morning" preset — stops illuminate
6. He plays
7. When done, he just walks away — nothing to shut down

## Sample Sets

- Free organ sample sets available as directories of WAV files (one per pipe)
- GrandOrgue ODF format sets can potentially be parsed
- Some Hauptwerk sets may be convertible (check licensing)
- Major producers: Sonus Paradisi, Pipeloops, Inspired Acoustics
- Experience with Hauptwerk on the Windows machine will inform which sample sets and stops to prioritize for the custom engine

## Related / Inspiration

- **GrandOrgue** — open-source virtual organ, Linux native, uses ODF format
- **Hauptwerk** — commercial virtual organ (Windows/macOS only), gold standard for realism
- **Linus Torvalds' AudioNoise** — hobby C project for digital audio effects (phasers, delays, filters using IIR and delay lines). Same real-time ALSA/Linux audio stack. Filter implementations could be useful for tone shaping. GitHub: torvalds/AudioNoise

## Development Plan

1. **Phase 1** — Prototype on available hardware (laptop or any Linux box)
2. **Phase 2** — Core C engine: JACK client, MIDI input, basic sample playback, single-channel output
3. **Phase 3** — Voice management: polyphony, attack/sustain/release, crossfades
4. **Phase 4** — Multi-channel routing: per-division output assignment
5. **Phase 5** — Organ features: stops, coupling, expression pedals
6. **Phase 6** — Embedded web server + iPad interface
7. **Phase 7** — Presets, wind simulation, acoustic refinements
8. **Phase 8** — Dedicated hardware deployment (mini PCs, audio interface, MIDI console)

## Estimated Complexity

- Core C engine: ~3,000-4,000 lines
- Web server integration: ~500-800 lines of C
- Web interface: ~500-1,000 lines (single HTML file with CSS/JS)
- Config parser: ~300-500 lines
- **Total: ~4,500-6,000 lines of C + one HTML file**
