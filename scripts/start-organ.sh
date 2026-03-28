#!/bin/bash
# start-organ.sh — Start the VirtualOrgan appliance
# Launches JACK, organ-engine, and auto-connects MIDI devices.
# Called by systemd user service or run manually.

set -euo pipefail

ORGAN_DIR="/home/brandonb/Dev/VirtualOrgan"
CONFIG="$ORGAN_DIR/test/caen_config.toml"
ENGINE="$ORGAN_DIR/organ-engine"

# JACK settings for Yamaha RX-A8A via HDMI
JACK_DEVICE="hw:0,7"
JACK_RATE=48000
JACK_PERIOD=512
JACK_OUTCHANNELS=8

ORGAN_SEQ_NAME="organ-engine"

log() { echo "[organ] $(date '+%H:%M:%S') $*"; }

# --- 1. Start JACK ---
log "Starting JACK on device $JACK_DEVICE..."
jackd -d alsa \
    -d "$JACK_DEVICE" \
    -r "$JACK_RATE" \
    -p "$JACK_PERIOD" \
    -o "$JACK_OUTCHANNELS" &
JACK_PID=$!

# Wait for JACK to be ready
for i in $(seq 1 30); do
    if jack_lsp &>/dev/null; then
        log "JACK is ready (attempt $i)"
        break
    fi
    if ! kill -0 "$JACK_PID" 2>/dev/null; then
        log "ERROR: JACK exited prematurely"
        exit 1
    fi
    sleep 0.5
done

if ! jack_lsp &>/dev/null; then
    log "ERROR: JACK did not start within 15 seconds"
    kill "$JACK_PID" 2>/dev/null
    exit 1
fi

# --- 2. Start organ-engine ---
log "Starting organ-engine with $CONFIG..."
"$ENGINE" "$CONFIG" &
ENGINE_PID=$!

# --- 3. Wait for organ-engine's ALSA sequencer port, then auto-connect MIDI ---
log "Waiting for organ-engine ALSA sequencer port..."
ORGAN_PORT=""
for i in $(seq 1 30); do
    ORGAN_PORT=$(aconnect -o 2>/dev/null | grep "$ORGAN_SEQ_NAME" | head -1 | sed 's/client \([0-9]*\).*/\1/' || true)
    if [ -n "$ORGAN_PORT" ]; then
        log "organ-engine sequencer port found: client $ORGAN_PORT (attempt $i)"
        break
    fi
    if ! kill -0 "$ENGINE_PID" 2>/dev/null; then
        log "ERROR: organ-engine exited prematurely"
        kill "$JACK_PID" 2>/dev/null
        exit 1
    fi
    sleep 0.5
done

if [ -z "$ORGAN_PORT" ]; then
    log "WARNING: organ-engine sequencer port not found, skipping MIDI connect"
fi

# Connect all hardware MIDI inputs to organ-engine
connect_midi() {
    if [ -z "$ORGAN_PORT" ]; then return; fi

    local clients
    clients=$(aconnect -i 2>/dev/null | grep '^client [0-9]' || true)

    while IFS= read -r line; do
        [ -z "$line" ] && continue
        local cid
        cid=$(echo "$line" | sed 's/client \([0-9]*\).*/\1/')
        # Skip system clients (0=System, 14=Midi Through) and organ-engine itself
        [ "$cid" -le 15 ] && continue
        [ "$cid" = "$ORGAN_PORT" ] && continue
        local cname
        cname=$(echo "$line" | sed "s/client $cid: '\\(.*\\)'.*/\\1/" | xargs)
        log "Connecting '$cname' (client $cid) to organ-engine ($ORGAN_PORT)"
        aconnect "$cid:0" "$ORGAN_PORT:0" 2>/dev/null || true
    done <<< "$clients"
}

connect_midi

# --- 4. Wait for organ-engine to exit, clean up ---
log "Appliance running. PIDs: jackd=$JACK_PID organ-engine=$ENGINE_PID"

cleanup() {
    log "Shutting down..."
    kill "$ENGINE_PID" 2>/dev/null
    wait "$ENGINE_PID" 2>/dev/null || true
    kill "$JACK_PID" 2>/dev/null
    wait "$JACK_PID" 2>/dev/null || true
    log "Done."
}

trap cleanup SIGTERM SIGINT

wait "$ENGINE_PID" || true
log "organ-engine exited, stopping JACK..."
kill "$JACK_PID" 2>/dev/null
wait "$JACK_PID" 2>/dev/null || true
log "Done."
