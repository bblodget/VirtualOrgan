# Speaker Mapping — Yamaha RX-A8A via HDMI

## Hardware Setup

- **Receiver**: Yamaha RX-A8A (7.1 channel)
- **Connection**: HDMI from Minix Z350 mini PC (ALSA device `hw:0,7`)
- **JACK command**: `jackd -R -d alsa -d hw:0,7 -r 48000 -p 256 -o 8`

## JACK Channel to Speaker Mapping

Determined empirically using `speaker-test -D hw:0,7 -c 8 -t sine -s N`:

| JACK Channel | Speaker           | Position                |
|-------------|-------------------|-------------------------|
| 1           | Front Left (FL)   | Front, left of listener |
| 2           | Front Right (FR)  | Front, right of listener |
| 3           | Rear Left (RL)    | Behind, left            |
| 4           | Rear Right (RR)   | Behind, right           |
| 5           | Front Center (FC) | Front, center           |
| 6           | LFE (Subwoofer)   | Subwoofer (bass only)   |
| 7           | Rear Left Surround (RLS) | Side/rear left   |
| 8           | Rear Right Surround (RRS) | Side/rear right |

**Note:** This mapping differs from the standard HDMI 7.1 channel order
(FL, FR, FC, LFE, RL, RR, RLC, RRC). The receiver may remap channels
internally. The mapping above reflects what was actually measured.

## Suggested Organ Division Routing

For a pipe organ spatial layout that mimics how divisions are physically
located in different parts of a real church:

| Division | Channels | Speakers | Rationale |
|----------|----------|----------|-----------|
| Great    | 1, 2     | FL, FR   | Main sound, front and prominent |
| Swell    | 3, 4     | RL, RR   | Behind the listener, like a swell box at the rear of the organ |
| Pedal    | 6        | LFE      | Deep 16' and 32' bass through subwoofer |
| Choir    | 5        | FC       | Soft accompanimental, center front |
| Ambient  | 7, 8     | RLS, RRS | Room fill, reverb tails |

Example config:

```toml
[routing.great]
source = { division = "great" }
output_channels = [1, 2]

[routing.swell]
source = { division = "swell" }
output_channels = [3, 4]

[routing.pedal]
source = { division = "pedal" }
output_channels = [6]

[routing.choir]
source = { division = "choir" }
output_channels = [5]
```

## Testing

To test individual speakers without the organ engine:

```bash
# Stop JACK first (it holds the ALSA device)
killall jackd

# Play a tone on a specific channel (1-8)
speaker-test -D hw:0,7 -c 8 -t sine -s 1   # Front Left
speaker-test -D hw:0,7 -c 8 -t sine -s 2   # Front Right
# ... etc.
# Ctrl+C to stop each test
```

To test with the organ engine, use per-note routing to send individual
notes to specific speakers. See `test/burea_config.toml` for an example.
