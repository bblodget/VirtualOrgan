# The Virtual Pipe Organ — A Technical Handbook

A guide to the concepts, theory, and technologies behind building a real-time virtual pipe organ engine in C for Linux.

---

## Table of Contents

1. [What Is a Virtual Pipe Organ?](#chapter-1-what-is-a-virtual-pipe-organ)
2. [Real-Time Audio on Linux](#chapter-2-real-time-audio-on-linux)
3. [MIDI — The Language of Musical Instruments](#chapter-3-midi--the-language-of-musical-instruments)
4. [The Audio Pipeline](#chapter-4-the-audio-pipeline)
5. [Lock-Free Programming for Audio](#chapter-5-lock-free-programming-for-audio)
6. [Sample Playback and Digital Audio Fundamentals](#chapter-6-sample-playback-and-digital-audio-fundamentals)
7. [Sample Sets — Where Pipe Sounds Come From](#chapter-7-sample-sets--where-pipe-sounds-come-from)
8. [Multiple Keyboards and MIDI Device Mapping](#chapter-8-multiple-keyboards-and-midi-device-mapping)

---

## Chapter 1: What Is a Virtual Pipe Organ?

### The Real Thing

A pipe organ is one of the oldest and most complex musical instruments. A large church organ might have thousands of individual pipes, organized into groups called **ranks**. Each rank produces a distinct tone color (called a **stop**), and each pipe in a rank sounds a single pitch. When the organist presses a key, air is directed through the appropriate pipes to produce sound.

Organs are organized into **divisions** — separate sections of the instrument, each with its own keyboard (called a **manual**) and set of stops:

- **Great** — the main division, typically the loudest
- **Swell** — enclosed in a box with shutters controlled by a pedal, allowing dynamic expression
- **Pedal** — played with the feet, providing bass notes
- **Choir** — a softer accompanimental division

### Ranks and Stops

A **rank** is a complete set of pipes — one pipe for every note on the keyboard — that all share the same tone color (timbre). A large organ might have 50 or more ranks.

Ranks are named by their **tone family** and **foot pitch**. The foot pitch refers to the speaking length of the lowest pipe in the rank:

- **8'** (eight foot) — plays at normal pitch. Middle C sounds as middle C.
- **4'** (four foot) — plays one octave higher. Middle C sounds as the C above.
- **16'** (sixteen foot) — plays one octave lower. Common in pedal divisions for deep bass.
- **2'** (two foot) — plays two octaves higher. Adds brilliance and clarity.

The major tone families, based on pipe construction:

- **Principal (Diapason)** — the classic organ sound. Open cylindrical metal pipes. The foundation of the instrument.
- **Flute** — softer, rounder tone. Often stopped (capped) pipes, which sound an octave lower for their size.
- **String** — thin, bright, slightly buzzy tone from narrow-scaled pipes. Names like Viola, Salicional, Gamba.
- **Reed** — uses a vibrating metal tongue instead of just air. Brassy or buzzy character. Trumpets, oboes, clarinets.

So "Principal 8'" is a rank of principal-family pipes at normal pitch, while "Flute 4'" is a rank of flute-family pipes sounding an octave higher.

A **stop** is the control (a knob or tab on the console) that engages or disengages a rank. When the organist pulls the "Principal 8'" stop, that rank becomes active and its pipes sound when keys are pressed. Multiple stops can be engaged simultaneously — that's how the organ builds up its massive, layered sound. "Full organ" might have 20+ ranks all sounding at once.

A **registration** is a particular combination of stops — the organ equivalent of a sound preset.

### The Virtual Version

A virtual pipe organ replaces the physical pipes with high-quality audio recordings (**samples**) of real pipes. Every single pipe in the organ is individually recorded, capturing the unique character of each one. When a key is pressed, the corresponding sample is played back through speakers.

The goal is not just to play recordings — it is to faithfully recreate the experience of playing a real pipe organ, including:

- **Polyphony** — dozens of pipes sounding simultaneously
- **Spatial audio** — different divisions coming from different speaker locations
- **Expression** — real-time volume control via swell pedals
- **Low latency** — the sound must respond to key presses within milliseconds, just as real pipes do

### Commercial and Open-Source Options

**Hauptwerk** is the gold standard commercial virtual organ software. It runs on Windows and macOS, supports large sample sets with advanced features like convolution reverb and wind modeling, but has no Linux support.

**GrandOrgue** is an open-source alternative that runs on Linux. It uses its own ODF (Organ Definition File) format and supports many of the same sample sets.

This project builds a custom engine from scratch in C, optimized for a specific use case: a dedicated, headless Linux appliance controlled via an iPad.

---

## Chapter 2: Real-Time Audio on Linux

### What "Real-Time" Means

When an organist presses a key, sound must come out of the speakers almost instantly. The human ear can detect delays as short as 10–15 milliseconds, and delays above 20ms feel sluggish and unmusical. This means the entire chain — from key press to speaker output — must complete within a few milliseconds.

This is a fundamentally different requirement than most software. A web browser can pause for 100ms to garbage-collect without anyone noticing. An audio engine that pauses for even 5ms produces an audible glitch — a pop or dropout that breaks the musical experience.

### The Linux Audio Stack

Linux audio has several layers:

```
Application
    ↓
JACK (or PulseAudio/PipeWire)
    ↓
ALSA (Advanced Linux Sound Architecture)
    ↓
Kernel driver
    ↓
Hardware (USB audio interface → amplifier → speakers)
```

**ALSA** is the kernel-level audio subsystem. It provides direct access to audio hardware but requires applications to manage their own buffering and mixing.

**JACK** (JACK Audio Connection Kit) sits above ALSA and provides:

- **Low-latency audio routing** between applications
- **Sample-accurate synchronization** — all JACK clients process audio in lockstep
- **A callback model** — JACK calls your code when it needs audio, rather than you pushing audio to it
- **Real-time scheduling** — the JACK process thread runs at elevated priority so the OS doesn't interrupt it

We use **JACK2** (jackd2), the multi-threaded implementation.

### PipeWire vs JACK

Modern Linux distributions (including Debian Trixie) ship with **PipeWire**, a newer audio server that replaces both PulseAudio (desktop audio) and JACK. PipeWire handles everything — notification sounds, video calls, Bluetooth headphones, screen sharing, and pro audio — through a single unified service.

|                  | JACK                                | PipeWire                                |
|------------------|-------------------------------------|-----------------------------------------|
| **Focus**        | Professional audio only             | Everything (desktop + pro audio)        |
| **Latency**      | You control it directly (64–4096 frames) | Managed automatically, tends toward larger buffers |
| **Complexity**   | Must start/stop manually            | Runs as a system service, just works    |
| **Buffer size**  | You choose exactly                  | Negotiated by the system                |
| **First release**| 2002                                | 2017                                    |

PipeWire includes a **JACK compatibility layer** (`pipewire-jack`). Applications that use the JACK API can run under PipeWire by prefixing the command with `pw-jack`:

```bash
# Run under PipeWire's JACK compatibility
pw-jack ./organ-engine config.toml

# Run under standalone JACK (must start jackd first)
./organ-engine config.toml
```

For development, PipeWire is convenient — it runs automatically and coexists with desktop audio. For our dedicated organ appliance, standalone JACK is the right choice because:

- We don't need desktop audio, Bluetooth, or video
- We want explicit control over buffer size and latency
- The machine is dedicated to a single task
- Every millisecond of latency matters for a keyboard instrument

In practice, the difference is significant. PipeWire defaulted to a 1024-frame buffer (~21ms latency) for our engine, while standalone JACK lets us specify 128 frames (~2.7ms) — an 8× improvement.

To switch from PipeWire to standalone JACK:

```bash
# Stop PipeWire
systemctl --user stop pipewire pipewire-pulse pipewire.socket pipewire-pulse.socket

# Start JACK with real-time priority, 128-frame buffer
jackd -R -d alsa -d hw:1 -r 48000 -p 128 &
```

### Frames

In digital audio, a **frame** is one sample per channel at a single point in time. For a stereo output, one frame is 2 float values (left and right). For mono, one frame is 1 float. The term exists so we can talk about time positions in the audio stream regardless of how many channels are in use.

When JACK asks for 128 frames of stereo audio, it needs 256 float values total — but those values represent 128 points in time.

### The JACK Process Callback

The core of any JACK application is the **process callback** — a function that JACK calls at regular intervals, asking you to fill an output buffer with audio samples.

```c
int process_callback(jack_nframes_t nframes, void *arg)
{
    float *output = jack_port_get_buffer(output_port, nframes);
    // Fill 'output' with 'nframes' samples of audio
    return 0;
}
```

At 48kHz sample rate with a 128-frame buffer, this callback fires roughly **375 times per second**. Each invocation must complete in under **2.67 milliseconds** (128 / 48000). If it takes longer, audio drops out.

This is why the process callback has strict rules about what code can run inside it — see Chapter 5.

### Buffer Size and Latency

The buffer size determines the tradeoff between latency and reliability:

| Buffer Size | Latency at 48kHz | Notes |
|-------------|-------------------|-------|
| 64 frames   | 1.3 ms           | Very demanding — needs fast hardware and real-time kernel |
| 128 frames  | 2.7 ms           | Good balance — our target |
| 256 frames  | 5.3 ms           | Safe, still feels responsive |
| 512 frames  | 10.7 ms          | Noticeable delay |
| 1024 frames | 21.3 ms          | Feels sluggish for a keyboard instrument |

The total round-trip latency is roughly **2× the buffer size** (one buffer for input, one for output), plus any hardware latency in the audio interface.

### Real-Time Kernel Configuration

Standard Linux uses a **preemptive** scheduler, but it can still delay audio threads for several milliseconds to service other work. For professional audio:

- The `audio` group gets real-time scheduling privileges via `/etc/security/limits.d/audio.conf`
- JACK runs its process thread at **FIFO scheduling priority**, meaning it preempts almost everything
- The `PREEMPT_RT` kernel patch set reduces worst-case scheduling latency from ~10ms to ~50μs

**Installing the RT kernel on Debian Trixie:**

```bash
sudo apt install linux-image-rt-amd64
```

After a reboot, verify it's active:

```bash
# Check kernel version — look for "-rt-" in the name
uname -r
# Example: 6.12.74+deb13+1-rt-amd64

# Confirm PREEMPT_RT is enabled (1 = active)
cat /sys/kernel/realtime
# 1
```

The standard kernel remains available in GRUB's "Advanced options" as a fallback. Both kernels can coexist.

Our system also configures real-time scheduling privileges via:

```ini
# /etc/security/limits.d/audio.conf
@audio   -  rtprio     95
@audio   -  memlock    unlimited
```

This allows any user in the `audio` group to request real-time scheduling and lock memory (preventing the OS from swapping audio data to disk).

### Linux Capabilities and Privileged Ports

Our organ engine includes a built-in web server for iPad control. The standard HTTP port is **80**, but on Linux only root can bind to ports below 1024. Running the entire engine as root would be a security risk and would complicate JACK and ALSA permissions.

Linux **capabilities** solve this by granting specific privileges to a program without giving it full root access. The `cap_net_bind_service` capability allows a program to bind to privileged ports:

```bash
sudo setcap cap_net_bind_service=+ep organ-engine
```

The flags mean:
- `e` (effective) — the capability is active when the program runs
- `p` (permitted) — the program is allowed to use this capability

This is stored in the file's extended attributes, so it must be re-applied after each recompile. The project's `make install` target handles this automatically.

This is the same mechanism that allows programs like `ping` to send raw network packets without root — each program gets only the specific privileges it needs, following the **principle of least privilege**.

---

## Chapter 3: MIDI — The Language of Musical Instruments

### What Is MIDI?

MIDI (Musical Instrument Digital Interface) is a protocol from 1983 that allows musical instruments, computers, and other devices to communicate. It doesn't carry sound — it carries **instructions**: which note to play, how hard, when to stop.

A MIDI message is typically 1–3 bytes:

| Message      | Status Byte | Data 1      | Data 2    |
|-------------|-------------|-------------|-----------|
| Note On     | 0x90 + ch   | Note number | Velocity  |
| Note Off    | 0x80 + ch   | Note number | Velocity  |
| Control Change | 0xB0 + ch | CC number  | Value     |

- **Channel** (0–15): MIDI supports 16 channels. In our organ, each division (Great, Swell, Pedal) uses a different channel.
- **Note number** (0–127): Middle C is 60. A standard piano spans 21–108. An organ pedalboard typically spans 36–67.
- **Velocity** (0–127): How hard the key was struck. Organ keyboards don't usually have velocity sensitivity (every note plays at full volume), but we support it for flexibility.
- **Control Change (CC)**: Used for expression pedals, stop controls, and other continuous parameters.

### MIDI on Linux: ALSA Sequencer

Linux handles MIDI through the **ALSA sequencer** subsystem. When you plug in a USB MIDI keyboard, the kernel creates an ALSA sequencer client for it. Our organ engine creates its own sequencer client, and we connect them using `aconnect`:

```bash
# List available MIDI ports
aconnect -l

# Connect USB keyboard (client 20, port 0) to organ engine (client 128, port 0)
aconnect 20:0 128:0
```

The MIDI thread in our engine opens an ALSA sequencer port, reads incoming events, and translates them into our internal `MidiEvent` format before pushing them into the ring buffer.

### From Key Press to Sound

The full journey of a single key press:

1. Finger presses key on MIDI keyboard
2. Keyboard sends MIDI Note On message over USB
3. Linux kernel receives USB data, passes to ALSA sequencer
4. Our MIDI thread reads the event from ALSA
5. MIDI thread pushes a `MidiEvent` into the lock-free ring buffer
6. JACK process callback pops the event from the ring buffer
7. Voice pool allocates a voice and begins playing the corresponding sample
8. Mixer sums the voice into the output buffer
9. JACK writes the buffer to the audio interface
10. Audio interface converts digital signal to analog
11. Amplifier drives the speakers
12. Sound reaches the organist's ears

Steps 6–9 happen inside the JACK process callback and must complete in under 2.7ms.

---

## Chapter 4: The Audio Pipeline

### Overview

Our engine has a linear pipeline with clear boundaries between real-time and non-real-time code:

```
[Non-RT]  Config → Sample Loader → MIDI Thread
                                       ↓
                                  Ring Buffer
                                       ↓
[RT]      JACK Callback → Voice Pool → Mixer → Output Buffers → Speakers
```

Everything above the ring buffer can allocate memory, read files, and make system calls. Everything below it must be real-time safe.

### Config (`config.c`)

At startup, the engine reads a TOML configuration file that defines:

- **Audio settings**: sample rate, buffer size, JACK client name
- **Ranks**: each rank maps a sample directory to output channels
- **Divisions**: each division has a MIDI channel, a set of stops (referencing ranks), and an optional expression pedal CC

The config is parsed once at startup using the `tomlc99` library and stored in an `OrganConfig` struct. Most fields are read-only, but stop engaged state and division expression gain are updated at runtime by MIDI CC events in the JACK callback (using simple atomic-safe writes).

### Sample Loader (`sampler.c`)

The sample loader reads WAV files from disk into memory at startup. Each rank has a directory of WAV files, one per note, named by MIDI note number (e.g., `060.wav` for middle C).

Files are read using `libsndfile`, which handles WAV format parsing and converts all samples to 32-bit floating point. The samples are stored in a `SampleBank` — a simple array indexed by MIDI note number for O(1) lookup during playback.

All file I/O and memory allocation happens at startup. Once the engine is running, no more disk access occurs.

### Voice Pool (`voice.c`)

The voice pool manages simultaneous note playback. When a Note On event arrives, the pool allocates a **voice** — a struct that tracks:

- Which sample to play
- Current playback position (how far into the sample we are)
- Velocity (for volume scaling)

When a Note Off arrives, the voice is deactivated and returned to the pool. The pool is a fixed-size array (128 voices), so allocation is just finding the first inactive slot — no `malloc` needed.

In Phase 2, note-off immediately silences the voice. Later phases will add a **release tail** — a gradual decay that mimics how a real organ pipe takes a moment to stop speaking.

### Mixer (`mixer.c`)

The mixer iterates over all active voices, renders each one into the output buffer, and sums them together. Each voice carries a division index, allowing the mixer to apply per-division expression gain (from swell pedal CC). Currently all voices output to a single stereo pair; future multi-channel routing will send divisions to different speaker groups.

### JACK Engine (`jack_engine.c`)

The JACK engine ties everything together in the process callback:

1. **Drain** the ring buffer of any pending MIDI events
2. **Dispatch** each event to the voice pool (note on/off)
3. **Render** all active voices through the mixer
4. **Output** the mixed audio into JACK's port buffers

The engine also handles JACK client setup, port registration, and auto-connecting to system playback ports.

---

## Chapter 5: Lock-Free Programming for Audio

### The Problem

The MIDI thread and the JACK process callback run on different threads and need to exchange data. The obvious solution — a mutex — is forbidden in the audio thread because:

- `pthread_mutex_lock()` can block if the mutex is held by another thread
- While blocked, the audio callback misses its deadline
- Result: audio dropout (a click, pop, or silence)

Even "fast" mutexes can cause **priority inversion**: the high-priority audio thread waits on a low-priority MIDI thread that has been preempted by a medium-priority thread. The audio thread starves.

### The Solution: Lock-Free Ring Buffer

A **ring buffer** (also called a circular buffer) is a fixed-size array that wraps around. One thread writes (the **producer**), another thread reads (the **consumer**), and they never need to coordinate with locks.

```
Write position →  [E] [F] [ ] [ ] [ ] [A] [B] [C] [D]
                                        ↑ Read position
```

The key insight: with a **single producer and single consumer**, you only need two atomic variables — the read position and the write position. The producer only modifies the write position, and the consumer only modifies the read position. C11 `atomic_store` and `atomic_load` provide the necessary memory ordering guarantees without any locks.

```c
bool ring_buffer_push(RingBuffer *rb, const MidiEvent *event)
{
    unsigned w = atomic_load(&rb->write_pos);
    unsigned next = (w + 1) & (RING_BUFFER_SIZE - 1);
    if (next == atomic_load(&rb->read_pos))
        return false;  // full
    rb->events[w] = *event;
    atomic_store(&rb->write_pos, next);
    return true;
}
```

The buffer size must be a power of 2 so that the wrap-around can use a fast bitwise AND instead of a modulo operation.

### Rules for Real-Time Code

Inside the JACK process callback, the following operations are **forbidden**:

| Operation | Why |
|-----------|-----|
| `malloc` / `free` | May acquire internal locks, trigger system calls |
| `printf` / file I/O | Blocks on disk or terminal |
| `pthread_mutex_lock` | Can block indefinitely |
| System calls (`read`, `write`, `open`) | Can block or be interrupted |
| Anything that allocates memory | Even `snprintf` to a new buffer |

What **is** allowed:

- Reading/writing pre-allocated memory
- Atomic operations (`atomic_load`, `atomic_store`)
- Simple arithmetic and logic
- Calling other real-time-safe functions

Our engine enforces this boundary: all heavy work (config parsing, sample loading, MIDI reading, web server) runs on separate threads. The JACK callback only touches pre-allocated structures through lock-free mechanisms.

---

## Chapter 6: Sample Playback and Digital Audio Fundamentals

### Digital Audio Basics

Sound is a continuous wave of air pressure. To store it digitally, we **sample** the wave at regular intervals:

- **Sample rate**: how many measurements per second. CD quality is 44,100 Hz. We use **48,000 Hz**, which is the standard for professional audio and video.
- **Bit depth**: precision of each measurement. We work in **32-bit floating point** internally, which gives us enormous dynamic range and avoids integer overflow during mixing.

A one-second mono audio clip at 48kHz is 48,000 float values = 192 KB of memory.

### How Samples Become Sound

When a note is triggered, the engine:

1. Looks up the WAV file for that MIDI note number in the sample bank
2. Creates a voice that points to that sample's memory buffer
3. Each JACK callback, copies the next `nframes` values from the sample into the output buffer
4. Advances the playback position by `nframes`
5. When the position reaches the end of the sample, the voice is finished

For polyphony, multiple voices render into the same output buffer **additively** — their sample values are summed together. This is how real acoustics work: sound waves from multiple sources add linearly.

### Velocity and Gain

MIDI velocity (0–127) controls how loud a note plays. We convert it to a linear gain:

```c
float gain = velocity / 127.0f;
```

Each sample value is multiplied by this gain during rendering. With multiple voices playing simultaneously, the summed output can exceed the range [-1.0, 1.0], which causes **clipping** (harsh distortion). The mixer applies a master gain (currently 0.10) to reduce the summed level, and uses `tanhf()` soft clipping as a safety net — smoothly compressing any peaks that still exceed ±1.0 instead of hard clipping.

### The Three Phases of a Pipe Sound

A real organ pipe's sound has three distinct phases:

1. **Attack** — the initial transient as wind enters the pipe and the sound builds. This is the "chiff" that gives pipes their characteristic articulation. Duration: 20–200ms depending on the pipe.

2. **Sustain** — the steady-state tone while the key is held. In a virtual organ, this is implemented as a **seamless loop** — a section of the sample that repeats indefinitely. The loop points must be chosen carefully at zero-crossings to avoid clicks.

3. **Release** — the decay after the key is released. The pipe doesn't stop instantly; it rings down over 50–500ms. This release tail is what gives the organ its spacious, reverberant character.

Our engine implements all three phases. Loop points are read from the WAV file's `smpl` chunk at load time via libsndfile's `SF_INSTRUMENT` API. During playback, the voice starts in the attack phase, transitions to sustain looping when it reaches `loop_start`, and enters the release phase when the key is released — playing through the remaining sample data to capture the natural decay. A 64-frame crossfade at the loop boundary prevents clicks at the seam. Samples without loop metadata play linearly from start to finish.

### Test Samples

For development without real organ samples, we generate simple **sine waves** — one per MIDI note from C2 (65.4 Hz) to C6 (1046.5 Hz). The frequency of each note is calculated from the MIDI note number:

```
frequency = 440 × 2^((note - 69) / 12)
```

This formula places A4 (MIDI note 69) at 440 Hz — the standard tuning reference — and spaces notes equally in **equal temperament**, where each semitone is a factor of 2^(1/12) ≈ 1.0595 apart.

Each test sample is 2 seconds long with short fade-in/fade-out ramps to prevent clicks at the start and end.

---

## Chapter 7: Sample Sets — Where Pipe Sounds Come From

### What Is a Sample Set?

A sample set is a collection of audio recordings of a real pipe organ — every individual pipe recorded separately. A typical set includes hundreds or thousands of WAV files organized by rank, plus metadata describing how the files map to notes, stops, and divisions.

Creating a sample set is a painstaking process. A recording team visits a church, silences the blower, and then records each pipe one at a time with high-quality microphones. A medium-sized organ with 30 ranks and 61 notes per rank requires recording nearly 2,000 individual pipes. The process can take several days.

### Anatomy of a Sample

Each pipe recording captures three phases of the sound:

1. **Attack** — the initial transient (50–200ms) as wind enters the pipe
2. **Sustain** — the steady-state tone, from which a seamless loop region is extracted
3. **Release** — the natural decay (100–500ms) after the wind is cut off

A typical pipe sample is **2–6 seconds** long at 44.1 or 48 kHz. Because the sustain portion is looped during playback, a 3-second recording can produce a note that sustains indefinitely.

Loop points are stored as **cue markers inside the WAV file** metadata. The tool **LoopAuditioneer** is commonly used to find good loop points at zero-crossings and add crossfades to eliminate clicks at the loop boundaries.

### Memory Requirements

Sample sets can be large. For reference:

| Organ Size | Ranks | Approx. Size |
|-----------|-------|-------------|
| Small chapel (6 stops) | 7 ranks | 90–200 MB |
| Medium church (20 stops) | 25 ranks | 500 MB – 1 GB |
| Large cathedral (50+ stops) | 50+ ranks | 2–5 GB |

All samples are loaded into RAM at startup for instant access during playback — there is no time to read from disk inside the real-time audio callback. The Minix's 16 GB of RAM can comfortably hold even large sample sets.

### The GrandOrgue Format

Most freely available sample sets are distributed in the **GrandOrgue** format, which consists of:

- **WAV files** — one per pipe, organized into directories by rank
- **An ODF (Organ Definition File)** — an INI-style text file that describes the organ's structure: which ranks belong to which stops, MIDI mappings, division assignments, display layout, and more

A typical directory structure:

```
Burea_Funeral_Chapel/
├── burea_gravkapell.organ          ← ODF file
├── Gedackt8/                       ← Rank: Gedackt 8' (stopped flute)
│   ├── 036-C.wav
│   ├── 037-C#.wav
│   ├── 038-D.wav
│   ├── ...
│   └── 096-C.wav
├── Principal2/                     ← Rank: Principal 2'
│   ├── 036-C.wav
│   └── ...
├── Salicional8/                    ← Rank: Salicional 8' (string)
│   ├── 036-C.wav
│   └── ...
├── Subbas16/                       ← Rank: Subbas 16' (pedal bass)
│   ├── 036-C.wav
│   └── ...
└── ConsoleImages/                  ← GUI graphics (not used by our engine)
```

File naming follows the pattern `NNN-Name.wav` where NNN is the zero-padded MIDI note number and Name is the note letter (with # for sharps). For example, `060-C.wav` is middle C, `061-C#.wav` is C-sharp above middle C.

### Filename Patterns

Different sample sets use different file naming conventions. Rather than hard-coding one convention, our engine uses a configurable **filename pattern** in the TOML config:

```toml
[ranks.gedackt8]
sample_dir = "samples/Burea_Funeral_Chapel/Gedackt8"
filename_pattern = "{note:03d}-{name}.wav"
```

Available placeholders:

| Placeholder | Expands To | Example (middle C) |
|-------------|-----------|-------------------|
| `{note:03d}` | Zero-padded 3-digit MIDI note | `060` |
| `{note:02d}` | Zero-padded 2-digit MIDI note | `60` |
| `{note}` | Unpadded MIDI note number | `60` |
| `{name}` | Note name with # for sharps | `C` |
| `{octave}` | Octave number | `4` |

This allows the engine to load samples from any sample set without renaming files. A few examples:

| Pattern | Result | Used By |
|---------|--------|---------|
| `{note:03d}-{name}.wav` | `060-C.wav` | GrandOrgue / Lars Palo sets |
| `{note:03d}.wav` | `060.wav` | Our generated test samples |
| `{name}{octave}.wav` | `C4.wav` | Some commercial sets |

### Where to Find Sample Sets

**Free sample sets:**

- **Lars Palo** (familjenpalo.se) — 8 high-quality sets of Swedish organs, Creative Commons licensed. Sizes range from 92 MB (Bureå Funeral Chapel, 6 stops) to 2.1 GB (Jukkasjärvi Church). An excellent starting point.
- **Piotr Grabowski** (piotrgrabowski.pl) — 15+ free sets of Polish organs, ranging from small chamber organs to 40-stop instruments.
- **Sonus Paradisi** (sonusparadisi.cz) — Premium commercial producer that offers free demo versions of several organs with 16–24 stops each.
- **Binaural Pipes** (binauralpipes.com) — Free sets recorded with binaural microphone technique, optimized for headphone listening.

**Commercial sample sets:**

- **Sonus Paradisi** — recordings of famous European organs, $50–$300+
- **Inspired Acoustics** — Hauptwerk-format sets (may be encrypted/DRM-protected)
- **Pipeloops** (pipeloops.com) — Various organs, some free demos available

**Important note on formats:** GrandOrgue-format sets (with WAV files and an ODF) work with our engine — we just need the WAV files and the filename pattern. Hauptwerk-format sets may use encrypted or proprietary container formats that cannot be used directly.

### Synthetic Alternatives

Real pipe sounds can also be approximated mathematically using **additive synthesis** — summing sine waves at harmonic frequencies with amplitudes that match the acoustic profile of different pipe families:

- **Principal** — strong fundamental with gradually decreasing harmonics
- **Stopped flute** — strong fundamental, mostly odd harmonics (3rd, 5th, 7th)
- **Open flute** — strong fundamental, very weak upper harmonics
- **String** — weak fundamental, many strong upper harmonics
- **Reed** — strong odd harmonics with complex upper spectrum

Adding a short noise burst at the onset simulates the attack transient ("chiff"), and subtle random pitch/amplitude modulation simulates wind fluctuations. This approach won't rival real recorded samples for realism, but can produce convincing results — and requires no downloads or licensing considerations.

---

## Chapter 8: Multiple Keyboards and MIDI Device Mapping

### The Problem

A real pipe organ has multiple keyboards. A typical setup:

- **Manual I (Great)** — the main keyboard, played with the hands
- **Manual II (Swell)** — a second keyboard above the Great
- **Pedalboard** — a keyboard played with the feet

Each keyboard controls a different **division** of the organ — a separate set of stops and pipes. In our virtual organ, divisions are mapped to MIDI channels: notes arriving on channel 1 trigger the manual division's stops, notes on channel 2 trigger the pedal division's stops, and so on.

The problem is that most MIDI keyboards default to sending on **channel 1**. If you plug in two keyboards, they both send on channel 1 and the engine can't tell them apart.

### How Commercial Software Solves This

Some organists change the MIDI transmit channel in each keyboard's settings menu, but many keyboards bury this option or don't expose it at all.

**Hauptwerk** (the commercial virtual organ) takes a different approach: during setup, it asks the user to play the lowest key and highest key on each keyboard. It identifies keyboards not by MIDI channel but by their **USB device identity** — each keyboard gets a unique ALSA sequencer client when plugged in.

### Our Solution: ALSA Client Name Mapping

We use a similar approach. When a USB MIDI keyboard is plugged into Linux, the kernel creates an ALSA sequencer client with a name derived from the device (e.g., "CH345" or "Digital Piano"). These names are visible with `aconnect -l`:

```
client 20: 'CH345' [type=kernel,card=1]
    0 'CH345 MIDI 1    '
client 24: 'Digital Piano' [type=kernel,card=2]
    0 'Digital Piano MIDI 1'
```

Our engine's `[midi_devices]` config section maps these names to MIDI channels:

```toml
[midi_devices.CH345]
channel = 1

[midi_devices."Digital Piano"]
channel = 2
```

At startup, the MIDI thread scans all ALSA sequencer clients, matches their names against the config (using substring matching, so "Digital Piano" matches "Digital Piano MIDI 1"), and builds an internal lookup table. When a MIDI event arrives, the engine checks which ALSA client sent it and **remaps the channel** before processing.

This means:
- Both keyboards can stay on their default channel 1
- The engine transparently remaps based on which physical device the event came from
- No keyboard configuration changes needed
- Adding a new keyboard is just one line in the TOML config

### Connecting Multiple Keyboards

Each keyboard must be connected to the engine's ALSA sequencer port separately:

```bash
# Start the engine
./organ-engine config.toml --console

# In another terminal, connect both keyboards
aconnect 20:0 128:0    # CH345 → organ-engine
aconnect 24:0 128:0    # Digital Piano → organ-engine
```

The engine prints confirmation of each device mapping at startup:

```
midi: mapped 'CH345' (client 20) → channel 1
midi: mapped 'Digital Piano' (client 24) → channel 2
```

### Console Controls

When using real MIDI keyboards, the `--console` flag provides terminal-based controls for managing stops and volume without needing a GUI:

- `Z X C V B N M` — toggle stops 1–7 in the active division
- `Tab` — cycle which division the stop keys control
- `[` / `]` — expression volume for the active division (like a swell pedal)
- `-` / `=` — master gain (overall volume)
- `Space` — all stops off in the active division
- `` ` `` — toggle coupler(s)

This works over SSH, making it possible to control the organ engine remotely from a laptop while sitting at the console.

---

*This handbook will grow as the project develops. Future chapters will cover the web interface, wind simulation, and system deployment.*
