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

---

## Chapter 1: What Is a Virtual Pipe Organ?

### The Real Thing

A pipe organ is one of the oldest and most complex musical instruments. A large church organ might have thousands of individual pipes, organized into groups called **ranks**. Each rank produces a distinct tone color (called a **stop**), and each pipe in a rank sounds a single pitch. When the organist presses a key, air is directed through the appropriate pipes to produce sound.

Organs are organized into **divisions** — separate sections of the instrument, each with its own keyboard (called a **manual**) and set of stops:

- **Great** — the main division, typically the loudest
- **Swell** — enclosed in a box with shutters controlled by a pedal, allowing dynamic expression
- **Pedal** — played with the feet, providing bass notes
- **Choir** — a softer accompanimental division

The organist selects which ranks sound by pulling **stop knobs** or pressing **stop tabs**. A **registration** is a particular combination of stops — the organ equivalent of a sound preset.

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
- The `PREEMPT_RT` kernel patch set (or Debian's `linux-lowlatency` package) reduces worst-case scheduling latency from ~10ms to ~50μs

Our system configures this via:

```ini
# /etc/security/limits.d/audio.conf
@audio   -  rtprio     95
@audio   -  memlock    unlimited
```

This allows any user in the `audio` group to request real-time scheduling and lock memory (preventing the OS from swapping audio data to disk).

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
- **Ranks**: each rank maps a sample directory to a MIDI channel and output channels

The config is parsed once at startup using the `tomlc99` library and stored in an `OrganConfig` struct that the rest of the engine reads (but never modifies at runtime).

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

The mixer iterates over all active voices, renders each one into the output buffer, and sums them together. In Phase 2, this produces a simple mono mix duplicated to both stereo channels. Later phases will route voices to specific output channels based on their division.

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

Each sample value is multiplied by this gain during rendering. With multiple voices playing simultaneously, the summed output can exceed the range [-1.0, 1.0], which causes **clipping** (harsh distortion). Later phases will add a master limiter to prevent this.

### The Three Phases of a Pipe Sound

A real organ pipe's sound has three distinct phases:

1. **Attack** — the initial transient as wind enters the pipe and the sound builds. This is the "chiff" that gives pipes their characteristic articulation. Duration: 20–200ms depending on the pipe.

2. **Sustain** — the steady-state tone while the key is held. In a virtual organ, this is implemented as a **seamless loop** — a section of the sample that repeats indefinitely. The loop points must be chosen carefully at zero-crossings to avoid clicks.

3. **Release** — the decay after the key is released. The pipe doesn't stop instantly; it rings down over 50–500ms. This release tail is what gives the organ its spacious, reverberant character.

In Phase 2, we play the full sample from start to finish and stop immediately on note-off. Phase 3 will implement proper looping and release tails with crossfades.

### Test Samples

For development without real organ samples, we generate simple **sine waves** — one per MIDI note from C2 (65.4 Hz) to C6 (1046.5 Hz). The frequency of each note is calculated from the MIDI note number:

```
frequency = 440 × 2^((note - 69) / 12)
```

This formula places A4 (MIDI note 69) at 440 Hz — the standard tuning reference — and spaces notes equally in **equal temperament**, where each semitone is a factor of 2^(1/12) ≈ 1.0595 apart.

Each test sample is 2 seconds long with short fade-in/fade-out ramps to prevent clicks at the start and end.

---

*This handbook will grow as the project develops. Future chapters will cover multi-channel audio routing, organ stops and coupling, the web interface, wind simulation, and system deployment.*
