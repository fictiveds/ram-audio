# RAM Audio

[English version](README.md)

RAM Audio - это Linux-приложение командной строки, которое преобразует снимки памяти запущенных процессов в процедурный звук.

> [!IMPORTANT]
> - Сейчас поддерживается только **Linux**.
> - Для запуска генерации нужны **права суперпользователя** (`sudo` или root), так как движок читает `/proc/<pid>/mem` и `/proc/<pid>/maps`.
> - `--help` и `--list-algorithms` можно выполнять без root.

## Возможности

- C++17 движок с расширяемым реестром алгоритмов.
- Два режима вывода: экспорт WAV и потоковый raw PCM в реальном времени.
- Тонкая настройка голосов, таймингов, переключения сцен, модуляции, novelty-guard и band split.
- Повторяемые запуски через `--seed`.
- Сборка через CMake и тест телеметрии.

## Требования

- Linux с доступными интерфейсами памяти процессов в `/proc`.
- CMake 3.16 или новее.
- GCC или Clang с поддержкой C++17.
- Опционально для прослушивания стрима: `ffplay` (FFmpeg) или `aplay` (ALSA).

## Полный процесс сборки

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Артефакты сборки:

- `build/ram_audio` - основное приложение.
- `build/ram_audio_telemetry_test` - бинарник теста телеметрии.

## Запуск

Базовый шаблон команды:

```bash
sudo ./build/ram_audio [options]
```

### Режим `file` (экспорт WAV)

```bash
sudo ./build/ram_audio --mode file --output real_ram_symphony.wav --duration 180
```

### Режим `stream` (raw PCM в stdout, ограниченный)

```bash
sudo ./build/ram_audio --mode stream --duration 60 --sample-rate 44100 | ffplay -f s16le -ar 44100 -ac 1 -
```

### Режим `stream` (raw PCM в stdout, бесконечный)

```bash
sudo ./build/ram_audio --mode stream --infinite --buffer-ms 1000 --sample-rate 44100 | aplay -f S16_LE -r 44100 -c 1
```

### Список алгоритмов

```bash
./build/ram_audio --list-algorithms
```

### Примеры выбора алгоритмов

В `--algorithms` передавайте ID через запятую (без пробелов).

Один алгоритм (бесконечный стрим):

```bash
sudo ./build/ram_audio --mode stream --infinite --buffer-ms 900 --algorithms chaotic_lorenz_fm | aplay -f S16_LE -r 44100 -c 1
```

Микротональный/мелодический набор:

```bash
sudo ./build/ram_audio --mode stream --infinite --buffer-ms 1000 --algorithms microtonal_glitch_grid,polymeter_euclidean_micro,tritave_odd_meter_chords,pointer_walk_melody | aplay -f S16_LE -r 44100 -c 1
```

Ритмический/шумовой набор:

```bash
sudo ./build/ram_audio --mode stream --infinite --buffer-ms 1000 --algorithms percussive_rhythm_triggers,cellular_automata_noise,ring_mod_bitplanes,spectral_hole_puncher | aplay -f S16_LE -r 44100 -c 1
```

Спектральный/feedback-набор:

```bash
sudo ./build/ram_audio --mode stream --infinite --buffer-ms 1200 --algorithms fdn_prime_feedback,spectral_freeze_permuter,modal_mesh_exciter,inharmonic_resonator_swarm | aplay -f S16_LE -r 44100 -c 1
```

Экспорт WAV с curated-набором:

```bash
sudo ./build/ram_audio --mode file --output ram_audio_curated.wav --duration 240 --algorithms pointer_walk_melody,granular_freeze_scrub,ram_wavelet_scanner,bytebeat_formula_evolver
```

Повторяемый рендер (фиксированный seed):

```bash
sudo ./build/ram_audio --mode file --output ram_audio_seed42.wav --duration 180 --seed 42 --algorithms chaotic_lorenz_fm,fractal_byte_terrain,karplus_ram_string
```

## Поддерживаемые режимы

- Режим вывода (`--mode`): `file`, `stream`.
- Режим таймингов (`--timing-mode`): `uniform`, `lognormal`, `powerlaw`, `auto`.
- Режим переключения сцен (`--switch-mode`): `timer`, `entropy-triggered`.
- Режим микширования (`--mix-mode`): `smoothed` (пока единственная реализация).

## Полный справочник CLI

### Основные параметры I/O и запуска

- `--help`, `-h` - показать справку и выйти.
- `--quiet`, `-q` - отключить прогресс-лог.
- `--mode`, `-m` (`file` или `stream`, по умолчанию `file`) - режим вывода.
- `--output`, `-o` (по умолчанию `real_ram_symphony.wav`) - путь к WAV для `file`.
- `--duration`, `-d` (по умолчанию `180`) - длительность генерации в секундах (`> 0`, если не задан `--infinite`).
- `--infinite` - генерация до остановки (`Ctrl+C`), только для `--mode stream`.
- `--sample-rate`, `-r` (по умолчанию `44100`) - частота дискретизации, Гц.
- `--buffer-ms` (по умолчанию `500`) - буфер стрима в мс (`0` отключает буферизацию).
- `--max-memory-mb` (по умолчанию `60`) - максимальный размер снимка памяти процесса.
- `--algorithms <id1,id2,...>` - ограничить пул алгоритмов указанными ID.
- `--list-algorithms` - вывести доступные алгоритмы и выйти.
- `--seed` (по умолчанию `0`) - фиксированный seed (`0` = случайный).

### Полифония и интервалы планировщика

- `--min-voices` (по умолчанию `2`) - минимум активных голосов (`> 0`).
- `--max-voices` (по умолчанию `6`) - максимум активных голосов (`> 0`, `>= min-voices`).
- `--memory-switch-min` (по умолчанию `15`) - минимум секунд между сменами процесса (`> 0`).
- `--memory-switch-max` (по умолчанию `40`) - максимум секунд между сменами процесса (`> 0`, `>= memory-switch-min`).
- `--voice-spawn-min` (по умолчанию `2`) - минимум секунд между появлением голосов (`> 0`).
- `--voice-spawn-max` (по умолчанию `8`) - максимум секунд между появлением голосов (`> 0`, `>= voice-spawn-min`).

### Тайминги и генетическое поведение

- `--timing-mode` (по умолчанию `uniform`) - `uniform`, `lognormal`, `powerlaw` или `auto`.
- `--timing-log-sigma` (по умолчанию `0.60`) - диапазон `[0.05, 2.5]`.
- `--timing-power-alpha` (по умолчанию `1.80`) - диапазон `[1.05, 3.5]`.
- `--timing-auto-chaos` (по умолчанию `0.55`) - диапазон `[0, 1]`.
- `--genetic-mutation-rate` (по умолчанию `0.28`) - диапазон `[0, 1]`.
- `--genetic-mutation-depth` (по умолчанию `0.35`) - диапазон `[0, 1]`.
- `--genetic-algo-mutation` (по умолчанию `0.18`) - диапазон `[0, 1]`.

### Модуляция, ghost-buffer и transient shaping

- `--mod-matrix-enable` - включить modulation matrix.
- `--mod-matrix-depth` (по умолчанию `0.22`) - диапазон `[0, 1]`.
- `--mod-feedback-limit` (по умолчанию `0.55`) - диапазон `[0, 0.95]`.
- `--mod-wavefold` (по умолчанию `0.18`) - диапазон `[0, 1]`.
- `--ghost-depth` (по умолчанию `0.20`) - диапазон `[0, 1]`.
- `--ghost-decay` (по умолчанию `0.996`) - диапазон `[0.90, 0.9999]`.
- `--ghost-grain-ms` (по умолчанию `60`) - диапазон `[5, 4000]`.
- `--transient-threshold` (по умолчанию `0.010`) - диапазон `[0.0001, 0.2]`.
- `--transient-hysteresis` (по умолчанию `0.004`) - диапазон `[0, 0.1]`.
- `--transient-attack-ms` (по умолчанию `5`) - диапазон `[1, 200]`.
- `--transient-release-ms` (по умолчанию `70`) - диапазон `[5, 1000]`.
- `--transient-gain` (по умолчанию `1.12`) - диапазон `[0.2, 3.0]`.
- `--sustain-gain` (по умолчанию `0.94`) - диапазон `[0.2, 3.0]`.
- `--transient-shape` (по умолчанию `0.35`) - диапазон `[0, 1]`.
- `--sustain-shape` (по умолчанию `0.20`) - диапазон `[0, 1]`.

### Переключение сцен, novelty-контроль и громкость

- `--switch-mode` (по умолчанию `timer`) - `timer` или `entropy-triggered`.
- `--mix-mode` (по умолчанию `smoothed`) - пока только `smoothed`.
- `--entropy-delta-up` (по умолчанию `0.015`) - диапазон `(0, 1)`.
- `--entropy-delta-down` (по умолчанию `0.015`) - диапазон `(0, 1)`.
- `--entropy-hysteresis` (по умолчанию `0.004`) - диапазон `[0, 0.25)`.
- `--switch-cooldown` (по умолчанию `2`) - диапазон `[0, +inf)`.
- `--scene-macro-min` (по умолчанию `30`) - минимальная длина macro-сцены в секундах (`> 0`).
- `--scene-macro-max` (по умолчанию `180`) - максимальная длина macro-сцены в секундах (`> 0`, `>= scene-macro-min`).
- `--scene-micro-min` (по умолчанию `300`) - минимальная длина micro-фазы в мс (`> 0`).
- `--scene-micro-max` (по умолчанию `4000`) - максимальная длина micro-фазы в мс (`> 0`, `>= scene-micro-min`).
- `--target-rms` (по умолчанию `9000`) - диапазон `(100, 20000]`.
- `--limiter-ceiling` (по умолчанию `28000`) - диапазон `(1000, 32767]`.
- `--limiter-max-gain` (по умолчанию `1.8`) - диапазон `[0.25, 4.0]`.
- `--min-scene-time` (по умолчанию `8`) - диапазон `[0, 300]`.
- `--crossfade-ms` (по умолчанию `140`) - диапазон `[0, 5000]`.
- `--switch-prob-base` (по умолчанию `0.22`) - диапазон `[0, 1]`.
- `--switch-prob-energy` (по умолчанию `0.28`) - диапазон `[0, 1]`.
- `--switch-prob-novelty` (по умолчанию `0.36`) - диапазон `[0, 1]`.
- `--switch-prob-hyst` (по умолчанию `0.08`) - диапазон `[0, 1]`.
- `--hmm-tabu-window` (по умолчанию `3`) - диапазон `[0, 16]`.
- `--hmm-novelty-bias` (по умолчанию `0.22`) - диапазон `[0, 1]`.
- `--novelty-threshold` (по умолчанию `0.93`) - диапазон `[0, 1]`.
- `--novelty-history` (по умолчанию `48`) - диапазон `[8, 4096]`.
- `--novelty-cooldown` (по умолчанию `6`) - диапазон `[0, 300]`.
- `--novelty-spawn-extra` (по умолчанию `2`) - диапазон `[0, 8]`.

### Параметры band split

- `--band-low-hz` (по умолчанию `220`) - диапазон `[40, 1200]`.
- `--band-high-hz` (по умолчанию `2600`) - диапазон `[800, 12000]`, должен быть `> band-low-hz`.
- `--band-drift-hz` (по умолчанию `90`) - диапазон `[0, 2000]`.
- `--band-pin-families` - закрепить семейства голосов за фиксированными полосами.

## Доступные ID алгоритмов

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

## Примечания

- В режиме `stream` приложение пишет бинарный `s16le` mono PCM в `stdout`, а лог - в `stderr`.
- Если включен `--mod-matrix-enable` и указан `--buffer-ms > 200`, буфер автоматически уменьшается до `120 ms`.
- WAV выводится в формате 16-bit mono PCM.

## Лицензия

Проект распространяется по лицензии MIT. См. `LICENSE`.
