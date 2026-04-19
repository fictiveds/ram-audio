# RAM Audio

[Russian version](README.ru.md)

## Links

- Telegram channel: [@fictive_dsp](https://t.me/fictive_dsp)
- Personal profile: [@fictive_ds](https://t.me/fictive_ds)

RAM Audio is a Linux-native command-line synthesizer that converts live process memory snapshots into procedural audio.

> [!IMPORTANT]
> - Runtime support: **Linux (stable)** and **Windows (experimental)**.
> - Windows build is supported experimentally via WinAPI process-memory backend.
> - Runtime requires elevated privileges: on Linux use **root** (`sudo`) to read `/proc/<pid>/mem` and `/proc/<pid>/maps`; on Windows run terminal as **Administrator** for best process-memory access.
> - CLI/help/log text defaults to **English**. Use `--lang ru` for Russian mode.
> - `--help` and `--list-algorithms` can run without root.

## Features

- C++17 audio engine with a pluggable algorithm registry.
- Two output modes: WAV file export and real-time raw PCM streaming.
- Fine-grained controls for voices, scheduling, scene switching, modulation, novelty guard, and band split.
- Deterministic runs with `--seed`.
- CMake-based build and a telemetry test target.

## Requirements

- Linux with `/proc` process memory interfaces available.
- CMake 3.16 or newer.
- GCC or Clang with C++17 support.
- Optional tools for stream playback: `ffplay` (FFmpeg) or `aplay` (ALSA).

Windows compatibility build:

- CMake 3.16 or newer.
- MSVC (Visual Studio 2019+), or another C++17 toolchain.
- Run terminal as Administrator for better process-memory access.

## Build From Source

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## One-Command Build Scripts

### Linux

```bash
./scripts/build_linux.sh
```

Artifacts:

- `dist/linux/ram_audio`
- `dist/linux/ram_audio_telemetry_test`

Optional environment variables:

- `BUILD_TYPE` (default: `Release`)
- `BUILD_DIR` (default: `build/linux-<build_type>`)
- `OUT_DIR` (default: `dist/linux`)
- `RUN_TESTS` (`1` by default, set `0` to skip)
- `JOBS` (parallel build jobs)

### Windows

Run from PowerShell:

```powershell
./scripts/build_windows.ps1
```

Or from `cmd.exe`:

```bat
scripts\build_windows.bat
```

What the script does:

- Downloads and unpacks `llvm-mingw`, `cmake`, and `ninja` into `.tools/windows` (project-local).
- Builds with CMake + Ninja.
- Runs tests by default.
- Copies runtime DLLs next to `ram_audio.exe` in `dist/windows`.

Artifacts:

- `dist/windows/ram_audio.exe`
- `dist/windows/ram_audio_telemetry_test.exe`
- required runtime DLLs (`libc++.dll`, `libunwind.dll`, and others detected/needed)

PowerShell parameters:

- `-BuildType Release|Debug|RelWithDebInfo|MinSizeRel`
- `-BuildDir <path>`
- `-OutDir <path>`
- `-ToolsDir <path>`
- `-SkipTests`

Build outputs:

- `build/ram_audio` - main application.
- `build/ram_audio_telemetry_test` - telemetry test binary.

## Run

Linux command pattern:

```bash
sudo ./build/ram_audio [options]
```

Windows command pattern (PowerShell, run as Administrator):

```powershell
.\dist\windows\ram_audio.exe [options]
```

### Windows quick examples

Show algorithms:

```powershell
.\dist\windows\ram_audio.exe --list-algorithms
```

Russian UI mode (`UTF-8` console output):

```powershell
.\dist\windows\ram_audio.exe --lang ru --help
```

Short WAV render:

```powershell
.\dist\windows\ram_audio.exe --mode file --output ram_audio_win.wav --duration 30
```

Reproducible render with fixed seed:

```powershell
.\dist\windows\ram_audio.exe --mode file --output ram_audio_seed42_win.wav --duration 60 --seed 42 --algorithms chaotic_lorenz_fm,fractal_byte_terrain,karplus_ram_string
```

Optional stream preview (requires `ffplay` in PATH):

```powershell
.\dist\windows\ram_audio.exe --mode stream --duration 20 --sample-rate 44100 | ffplay -f s16le -ar 44100 -ac 1 -
```

### Mode: `file` (WAV export)

```bash
sudo ./build/ram_audio --mode file --output real_ram_symphony.wav --duration 180
```

### Mode: `stream` (raw PCM to stdout, finite)

```bash
sudo ./build/ram_audio --mode stream --duration 60 --sample-rate 44100 | ffplay -f s16le -ar 44100 -ac 1 -
```

### Mode: `stream` (raw PCM to stdout, infinite)

```bash
sudo ./build/ram_audio --mode stream --infinite --buffer-ms 1000 --sample-rate 44100 | aplay -f S16_LE -r 44100 -c 1
```

### Show algorithms

```bash
./build/ram_audio --list-algorithms
```

### Algorithm selection examples

Use comma-separated IDs in `--algorithms` (no spaces).

Single algorithm (infinite stream):

```bash
sudo ./build/ram_audio --mode stream --infinite --buffer-ms 900 --algorithms chaotic_lorenz_fm | aplay -f S16_LE -r 44100 -c 1
```

Microtonal/melodic set:

```bash
sudo ./build/ram_audio --mode stream --infinite --buffer-ms 1000 --algorithms microtonal_glitch_grid,polymeter_euclidean_micro,tritave_odd_meter_chords,pointer_walk_melody | aplay -f S16_LE -r 44100 -c 1
```

Rhythmic/noise set:

```bash
sudo ./build/ram_audio --mode stream --infinite --buffer-ms 1000 --algorithms percussive_rhythm_triggers,cellular_automata_noise,ring_mod_bitplanes,spectral_hole_puncher | aplay -f S16_LE -r 44100 -c 1
```

Spectral/feedback-heavy set:

```bash
sudo ./build/ram_audio --mode stream --infinite --buffer-ms 1200 --algorithms fdn_prime_feedback,spectral_freeze_permuter,modal_mesh_exciter,inharmonic_resonator_swarm | aplay -f S16_LE -r 44100 -c 1
```

WAV export with a curated set:

```bash
sudo ./build/ram_audio --mode file --output ram_audio_curated.wav --duration 240 --algorithms pointer_walk_melody,granular_freeze_scrub,ram_wavelet_scanner,bytebeat_formula_evolver
```

Reproducible render (fixed seed):

```bash
sudo ./build/ram_audio --mode file --output ram_audio_seed42.wav --duration 180 --seed 42 --algorithms chaotic_lorenz_fm,fractal_byte_terrain,karplus_ram_string
```

## Supported Modes

- Output mode (`--mode`): `file`, `stream`.
- Timing mode (`--timing-mode`): `uniform`, `lognormal`, `powerlaw`, `auto`.
- Scene switch mode (`--switch-mode`): `timer`, `entropy-triggered`.
- Mix mode (`--mix-mode`): `smoothed` (currently the only implementation).

## Full CLI Reference

### Core I/O and execution

- `--help`, `-h` - print help and exit.
- `--lang` (`en` or `ru`, default `en`) - CLI/help/log language.
- `--quiet`, `-q` - disable progress logs.
- `--mode`, `-m` (`file` or `stream`, default `file`) - select output mode.
- `--output`, `-o` (default `real_ram_symphony.wav`) - WAV file path for `file` mode.
- `--duration`, `-d` (default `180`) - generation length in seconds (`> 0` when not using `--infinite`).
- `--infinite` - run until interrupted (`Ctrl+C`), valid only with `--mode stream`.
- `--sample-rate`, `-r` (default `44100`) - sample rate in Hz.
- `--buffer-ms` (default `500`) - stream buffer in milliseconds (`0` disables buffering).
- `--max-memory-mb` (default `60`) - maximum memory snapshot size per process.
- `--algorithms <id1,id2,...>` - restrict algorithm pool to specific IDs.
- `--list-algorithms` - print available algorithms and exit.
- `--seed` (default `0`) - fixed RNG seed (`0` means random seed).

### Polyphony and scheduler intervals

- `--min-voices` (default `2`) - minimum active voices (`> 0`).
- `--max-voices` (default `6`) - maximum active voices (`> 0`, `>= min-voices`).
- `--memory-switch-min` (default `15`) - minimum seconds between process switches (`> 0`).
- `--memory-switch-max` (default `40`) - maximum seconds between process switches (`> 0`, `>= memory-switch-min`).
- `--voice-spawn-min` (default `2`) - minimum seconds between voice spawns (`> 0`).
- `--voice-spawn-max` (default `8`) - maximum seconds between voice spawns (`> 0`, `>= voice-spawn-min`).

### Timing and genetic behavior

- `--timing-mode` (default `uniform`) - `uniform`, `lognormal`, `powerlaw`, or `auto`.
- `--timing-log-sigma` (default `0.60`) - range `[0.05, 2.5]`.
- `--timing-power-alpha` (default `1.80`) - range `[1.05, 3.5]`.
- `--timing-auto-chaos` (default `0.55`) - range `[0, 1]`.
- `--genetic-mutation-rate` (default `0.28`) - range `[0, 1]`.
- `--genetic-mutation-depth` (default `0.35`) - range `[0, 1]`.
- `--genetic-algo-mutation` (default `0.18`) - range `[0, 1]`.

### Modulation, ghost buffer, and transient shaping

- `--mod-matrix-enable` - enable modulation matrix.
- `--mod-matrix-depth` (default `0.22`) - range `[0, 1]`.
- `--mod-feedback-limit` (default `0.55`) - range `[0, 0.95]`.
- `--mod-wavefold` (default `0.18`) - range `[0, 1]`.
- `--ghost-depth` (default `0.20`) - range `[0, 1]`.
- `--ghost-decay` (default `0.996`) - range `[0.90, 0.9999]`.
- `--ghost-grain-ms` (default `60`) - range `[5, 4000]`.
- `--transient-threshold` (default `0.010`) - range `[0.0001, 0.2]`.
- `--transient-hysteresis` (default `0.004`) - range `[0, 0.1]`.
- `--transient-attack-ms` (default `5`) - range `[1, 200]`.
- `--transient-release-ms` (default `70`) - range `[5, 1000]`.
- `--transient-gain` (default `1.12`) - range `[0.2, 3.0]`.
- `--sustain-gain` (default `0.94`) - range `[0.2, 3.0]`.
- `--transient-shape` (default `0.35`) - range `[0, 1]`.
- `--sustain-shape` (default `0.20`) - range `[0, 1]`.

### Scene switching, novelty control, and loudness

- `--switch-mode` (default `timer`) - `timer` or `entropy-triggered`.
- `--mix-mode` (default `smoothed`) - currently only `smoothed`.
- `--entropy-delta-up` (default `0.015`) - range `(0, 1)`.
- `--entropy-delta-down` (default `0.015`) - range `(0, 1)`.
- `--entropy-hysteresis` (default `0.004`) - range `[0, 0.25)`.
- `--switch-cooldown` (default `2`) - range `[0, +inf)`.
- `--scene-macro-min` (default `30`) - minimum macro-scene duration in seconds (`> 0`).
- `--scene-macro-max` (default `180`) - maximum macro-scene duration in seconds (`> 0`, `>= scene-macro-min`).
- `--scene-micro-min` (default `300`) - minimum micro-phase duration in ms (`> 0`).
- `--scene-micro-max` (default `4000`) - maximum micro-phase duration in ms (`> 0`, `>= scene-micro-min`).
- `--target-rms` (default `9000`) - range `(100, 20000]`.
- `--limiter-ceiling` (default `28000`) - range `(1000, 32767]`.
- `--limiter-max-gain` (default `1.8`) - range `[0.25, 4.0]`.
- `--min-scene-time` (default `8`) - range `[0, 300]`.
- `--crossfade-ms` (default `140`) - range `[0, 5000]`.
- `--switch-prob-base` (default `0.22`) - range `[0, 1]`.
- `--switch-prob-energy` (default `0.28`) - range `[0, 1]`.
- `--switch-prob-novelty` (default `0.36`) - range `[0, 1]`.
- `--switch-prob-hyst` (default `0.08`) - range `[0, 1]`.
- `--hmm-tabu-window` (default `3`) - range `[0, 16]`.
- `--hmm-novelty-bias` (default `0.22`) - range `[0, 1]`.
- `--novelty-threshold` (default `0.93`) - range `[0, 1]`.
- `--novelty-history` (default `48`) - range `[8, 4096]`.
- `--novelty-cooldown` (default `6`) - range `[0, 300]`.
- `--novelty-spawn-extra` (default `2`) - range `[0, 8]`.

### Band split controls

- `--band-low-hz` (default `220`) - range `[40, 1200]`.
- `--band-high-hz` (default `2600`) - range `[800, 12000]`, must be `> band-low-hz`.
- `--band-drift-hz` (default `90`) - range `[0, 2000]`.
- `--band-pin-families` - pin voice families to fixed band ranges.

## Available Algorithm IDs

- `hilbert_drone` - Hilbert Drone
- `bit_slicing_arpeggios` - Bit-Slicing Arpeggios
- `wavefolding_delta_bass` - Wavefolding Delta Bass
- `wavetable_granular_loop` - Wavetable Granular Loop
- `low_pass_float_aliasing` - Low-pass Float Aliasing
- `memory_phase_modulation` - Memory Phase Modulation
- `percussive_rhythm_triggers` - Percussive Rhythm Triggers
- `bytebeat_processor` - Byte-beat Processor
- `pointer_walk_melody` - Pointer Walk Melody
- `granular_freeze_scrub` - Granular Freeze Scrub
- `cellular_automata_noise` - Cellular Automata Noise
- `fractal_byte_terrain` - Fractal Byte Terrain
- `chaotic_lorenz_fm` - Chaotic Lorenz FM
- `resonator_bank_entropy` - Resonator Bank Entropy
- `ring_mod_bitplanes` - Ring Mod Bitplanes
- `karplus_ram_string` - Karplus RAM String
- `microtonal_glitch_grid` - Microtonal Glitch Grid
- `polymeter_euclidean_micro` - Polymeter Euclidean Micro
- `tritave_odd_meter_chords` - Tritave Odd Meter Chords
- `ram_wavelet_scanner` - RAM Wavelet Scanner
- `markov_byte_lattice` - Markov Byte Lattice
- `fdn_prime_feedback` - FDN Prime Feedback
- `spectral_freeze_permuter` - Spectral Freeze Permuter
- `modal_mesh_exciter` - Modal Mesh Exciter
- `bytebeat_formula_evolver` - Bytebeat Formula Evolver
- `inharmonic_resonator_swarm` - Inharmonic Resonator Swarm
- `fdn_topology_chaos` - FDN Topology Chaos
- `spectral_hole_puncher` - Spectral Hole Puncher
- `granular_pointer_shredder` - Granular Pointer Shredder

## Notes

- Stream mode writes binary `s16le` mono PCM to `stdout`; logs go to `stderr`.
- If `--mod-matrix-enable` is set and `--buffer-ms > 200`, buffer size is internally reduced to `120 ms`.
- WAV output is 16-bit mono PCM.

## License

Released under the MIT License. See `LICENSE`.
