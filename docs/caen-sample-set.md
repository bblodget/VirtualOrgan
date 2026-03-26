# Caen St. Étienne — Cavaillé-Coll Demo Sample Set

## About the Organ

The organ at St. Étienne in Caen, France was built by **Aristide Cavaillé-Coll** (1882), the most celebrated organ builder of the French Romantic era. This is a large symphonic instrument with multiple divisions and a rich palette of foundation, reed, and mutation stops.

The sample set is a **free demo** from **Sonus Paradisi**, one of the premier commercial organ sample producers. It includes a selection of stops from each division — enough to play and demonstrate the instrument, though not the complete organ.

- **Source**: Sonus Paradisi (sonusparadisi.cz)
- **Download**: Free, requires account — add to cart (price 0) and checkout at:
  https://www.sonusparadisi.cz/en/organs/france/caen-st-etienne/caen-organ-model-surround-383.html
- **Format**: Hauptwerk installation package
- **File**: `Caen_Demo.CompPkg.Hauptwerk.rar` (7.0 GB compressed, ~23 GB extracted)
- **Total WAV files**: 13,123
- **Sample format**: 48 kHz, 32-bit float (stereo) or 24-bit (mono)

## Hauptwerk Package Structure

```
Caen_Demo/
├── Caen_Demo.CompPkg.Hauptwerk.rar          ← original archive
├── OrganDefinitions/
│   └── Caen St. Etienne, Cavaille-Coll, Demo.Organ_Hauptwerk_xml
└── OrganInstallationPackages/
    └── 001704/
        ├── pipe/         ← all audio samples
        ├── manual/       ← keyboard graphics (bitmaps)
        ├── pedal/        ← pedalboard graphics
        ├── stop*/        ← stop tab graphics
        ├── button*/      ← button graphics
        ├── cont/         ← console background images
        ├── html/         ← info pages
        └── ...           ← other GUI assets
```

The `pipe/` directory contains all the audio. Everything else is Hauptwerk GUI assets (bitmaps for the virtual console) which we don't use.

## Pipe Directory Layout

```
pipe/
├── goDiff/          ← Grand Orgue (Great), diffuse microphone
├── goRear/          ← Grand Orgue (Great), rear microphone
├── recDiff/         ← Récit (Swell), diffuse microphone
├── recRear/         ← Récit (Swell), rear microphone
├── posDiff/         ← Positif (Choir), diffuse microphone
├── posRear/         ← Positif (Choir), rear microphone
├── pedDiff/         ← Pédale, diffuse microphone
├── pedRear/         ← Pédale, rear microphone
├── kl1_suchy/       ← Keyboard 1 mechanical noise (dry)
├── kl1_mokry/       ← Keyboard 1 mechanical noise (wet)
├── kl2_suchy/       ← Keyboard 2 mechanical noise (dry)
├── kl2_mokry/       ← Keyboard 2 mechanical noise (wet)
├── kl3_suchy/       ← Keyboard 3 mechanical noise (dry)
├── kl3_mokry/       ← Keyboard 3 mechanical noise (wet)
├── ped_suchy/       ← Pedalboard mechanical noise (dry)
├── ped_mokry/       ← Pedalboard mechanical noise (wet)
├── apels_suchy/     ← Appel (stop action) noise (dry)
├── apels_mokry/     ← Appel (stop action) noise (wet)
├── tahla_sucha/     ← Tracker action noise (dry)
├── tahla_mokra/     ← Tracker action noise (wet)
├── trem_suchy/      ← Tremulant noise (dry)
├── trem_mokry/      ← Tremulant noise (wet)
└── mech/            ← Mechanical noise
```

### Naming Convention

- `suchy` / `mokry` — Czech for **dry** / **wet** (without / with room reverb)
- `Diff` / `Rear` — **Diffuse** and **Rear** microphone positions
- `kl1`, `kl2`, `kl3` — keyboard 1, 2, 3 (mechanical key noise, not pipe sound)
- `ped` — pedalboard

**Important**: The `kl*` and `ped_*` directories contain short (0.52s) mechanical
noise samples — the sound of the keys and tracker mechanism, not organ pipes.
The actual pipe sounds are in the named stop directories under `goDiff`, `recDiff`, etc.

## Stop Directories

Each microphone perspective directory contains subdirectories for individual stops:

```
goDiff/                          ← Grand Orgue, diffuse mic
├── Montre8/                     ← stop: Montre 8'
│   ├── 036-C.wav                ← attack + sustain sample (6.87s, stereo)
│   ├── 037-C#.wav
│   ├── ...
│   ├── 096-C.wav
│   ├── rel00160/                ← release samples (held < 160ms)
│   │   ├── 036-C.wav
│   │   └── ...
│   ├── rel00500/                ← release samples (held 160–500ms)
│   │   ├── 036-C.wav
│   │   └── ...
│   └── rel99999/                ← release samples (held > 500ms)
│       ├── 036-C.wav
│       └── ...
├── Bourdon8/
├── FluteHarmonique8/
├── Gambe8/
├── Trompette8/
└── Bombarde16/
```

### Main Samples (attack + sustain)

The WAV files directly in each stop directory are the primary playback samples:

- **Naming**: `NNN-Note.wav` (e.g., `060-C.wav` for middle C)
- **Content**: Attack transient + sustain with embedded loop points
- **Format**: Stereo, 48 kHz, 32-bit float
- **Duration**: ~5–7 seconds per sample
- **Loop points**: Stored in the WAV `smpl` chunk, read by libsndfile

These are what our engine loads. The filename pattern `{note:03d}-{name}.wav` works directly.

### Release Samples (Hauptwerk-specific)

The `relNNNNN` subdirectories contain separate release (decay) samples, selected
by Hauptwerk based on how long the note was held:

| Directory   | Selection Criteria |
|-------------|--------------------|
| `rel00160`  | Note held less than 160ms |
| `rel00500`  | Note held 160–500ms |
| `rel99999`  | Note held longer than 500ms |

Our engine currently uses the release tail embedded in the main sample (after
the loop end point) rather than these separate release files. This is adequate
for basic playback. Loading duration-dependent release samples would be a
future enhancement.

### Tremulant Variants

Some stops in the Récit have `trem` subdirectories and `L` suffix variants
(e.g., `Bassonhautbois8L`, `CornetL`). These contain samples recorded with the
tremulant engaged — the wavering pitch/volume effect. The `L` variants may be
a lighter tremulant setting.

## Available Stops by Division

### Grand Orgue (Great) — `goDiff`/`goRear`

| Stop | Type | Notes |
|------|------|-------|
| Montre 8' | Principal | Foundation stop, bright and clear |
| Bourdon 8' | Flute (stopped) | Soft, round tone |
| Flûte Harmonique 8' | Flute (overblowing) | Clear, singing tone |
| Gambe 8' | String | Thin, slightly buzzy |
| Trompette 8' | Reed | Brilliant, brassy |
| Bombarde 16' | Reed | Powerful bass reed |

### Récit (Swell) — `recDiff`/`recRear`

| Stop | Type | Notes |
|------|------|-------|
| Diapason 8' | Principal | Warm foundation |
| Basson-Hautbois 8' | Reed | Orchestral oboe-like |
| Flûte Octaviante 4' | Flute | Bright 4' flute |
| Nasard 2⅔' | Mutation | Reinforces 3rd harmonic |
| Octavin 2' | Principal | Bright, high |
| Cornet | Compound | Multiple ranks combined |

### Positif (Choir) — `posDiff`/`posRear`

| Stop | Type | Notes |
|------|------|-------|
| Bourdon 16' | Flute (stopped) | Deep, soft bass |
| Cor de Nuit 8' | Flute (stopped) | Gentle, dark |
| Salicional 8' | String | Delicate string |

### Pédale — `pedDiff`/`pedRear`

| Stop | Type | Notes |
|------|------|-------|
| Contrebasse 16' | String | Deep, sustained bass |
| Grosse Flûte 8' | Flute | Warm pedal flute |

## Known Issues

### Stereo Channel Asymmetry

The stereo WAV files in this sample set do **not** represent conventional
left/right stereo. Instead, the two channels appear to represent two
different microphone signals. Measured RMS levels for middle C (note 60):

| Sample | Ch 0 (Left) | Ch 1 (Right) |
|--------|-------------|--------------|
| goDiff/Montre8 | 0.0738 | 0.0899 |
| goDiff/Bourdon8 | 0.0280 | 0.0168 |
| recDiff/Diapason8 | **0.0000** | 0.0725 |
| recDiff/Bassonhautbois8 | **0.0000** | 0.0700 |
| recDiff/Cornet | **0.0000** | 0.0755 |

The **Récit (Swell) diffuse samples have a completely silent left channel**.
All audio is in the right channel only. The Grand Orgue samples have audio
in both channels but at different levels (not a simple L/R stereo image).

This is likely intentional in the Hauptwerk format — the two channels may
represent two microphone positions that Hauptwerk routes independently.
When using these samples with our engine, the Récit stops will only produce
sound on the "right" output channel of each routed speaker pair.

**Workaround needed:** The engine should detect mono-in-stereo samples and
duplicate the active channel, or provide a config option to select which
sample channel to use for each rank.

### Lowercase Filenames for Low Notes

Pedal samples use **lowercase** note names for notes below C2 (e.g.,
`024-c.wav`, `035-b.wav`) but uppercase for C2 and above (`036-C.wav`).
The engine's sampler handles this with an automatic lowercase fallback —
if the uppercase filename is not found, it retries with a lowercase version.

## Using with VirtualOrgan Engine

Point the rank `sample_dir` at the stop directory under the desired microphone
perspective. The `Diff` (diffuse) perspective is a good default:

```toml
[ranks.montre8]
sample_dir = "samples/Caen_Demo/OrganInstallationPackages/001704/pipe/goDiff/Montre8"
filename_pattern = "{note:03d}-{name}.wav"
```

See `test/caen_config.toml` for a complete working configuration with all
available stops mapped to four divisions.

## Microphone Perspectives

Each division has two recording perspectives:

| Suffix | Microphone | Character |
|--------|-----------|-----------|
| `Diff` | Diffuse / close | More direct, detailed sound |
| `Rear` | Rear of church | More reverberant, spacious |

For multi-channel setups, you could load both perspectives and route them to
different speaker groups (close mics to front speakers, rear mics to surround
speakers) for an immersive spatial experience.
