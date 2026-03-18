# VirtualOrgan — TODO

## Completed

- [x] Project scaffolding (Makefile, directory structure, .gitignore)
- [x] Vendor tomlc99 TOML parser
- [x] TOML config parser (audio settings, ranks)
- [x] Test sample generator (sine waves C2–C6)
- [x] Sample loader with configurable filename patterns
- [x] Lock-free SPSC ring buffer
- [x] Voice pool (note on/off, render)
- [x] Stereo mixer
- [x] JACK engine (client, process callback, auto-connect)
- [x] MIDI input (ALSA sequencer + fake-midi mode)
- [x] Main loop with signal handling
- [x] Downloaded Bureå Funeral Chapel sample set (Lars Palo)
- [x] Tested full pipeline with JACK dummy driver
- [x] Technical handbook (Chapters 1–7)

## Up Next

- [ ] Test with real audio output on the Minix (JACK + ALSA driver)
- [ ] Test with MIDI keyboard (USB MIDI → ALSA → organ engine)
- [ ] Test at Dad's house with speaker system
- [ ] Add remaining Bureå ranks to config (all 7 ranks)
- [ ] Install PREEMPT_RT kernel for low-latency audio

## Phase 3 — Voice Management

- [ ] Sustain looping (seamless loop using WAV cue markers)
- [ ] Release tails (decay after note-off instead of instant silence)
- [ ] Crossfades at loop boundaries
- [ ] Read loop points from WAV file cue metadata

## Phase 4 — Multi-Channel Routing

- [ ] Per-division output channel assignment
- [ ] Multiple JACK output port pairs
- [ ] Load multiple ranks simultaneously

## Phase 5 — Organ Features

- [ ] Stop controls (engage/disengage ranks via MIDI CC)
- [ ] Division grouping (Great, Swell, Pedal)
- [ ] Coupling (one division triggers another)
- [ ] Expression pedals (per-division volume via MIDI CC)

## Phase 6 — Web Interface

- [ ] Embedded HTTP server (microhttpd or mongoose)
- [ ] WebSocket support for real-time state sync
- [ ] Single-page iPad interface (stop console, presets)
- [ ] Avahi/mDNS advertisement as organ.local

## Phase 7 — Polish

- [ ] Presets / registrations (saved stop combinations)
- [ ] Wind simulation (subtle pitch/amplitude LFO)
- [ ] Master limiter to prevent clipping
- [ ] GrandOrgue ODF parser (read organ definitions directly)

## Ideas / Future

- [ ] Synthetic organ sound generator (standalone tool)
- [ ] Real-time additive synthesis in engine (zero RAM usage)
- [ ] Hybrid mode (real attack samples + synthesized sustain)
- [ ] systemd service for appliance mode
- [ ] Auto-connect MIDI devices on plug-in
